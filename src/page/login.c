/* See login.h. */

#include "page/login.h"

#include "base/log.h"
#include "base/worker.h"
#include "io/http.h"
#include "jelly/api.h"
#include "model/catalog.h"
#include "page/boot.h"
#include "page/keys.h"
#include "port/input.h"
#include "port/platform.h"
#include "view/element.h"
#include "view/page_chrome.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keyboard tags beside the rows. */
#define TAG_USER 10
#define TAG_PASS 11

/* Quick Connect: asked every two seconds, for as long as a code is good. */
#define QC_POLL_US  2000000u
#define QC_GIVE_UP_US (5u * 60u * 1000000u)

static const struct {
    const char *label, *help;
} kRow[LOGIN_ROW_N] = {
    [LOGIN_ROW_SERVER]   = {"Server", "An IP address, or a name such as jellyfin.example.com"},
    [LOGIN_ROW_PORT]     = {"Port", "8096 on the home network; 443 through a reverse proxy"},
    [LOGIN_ROW_HTTPS]    = {"HTTPS", "On for an address reached over the internet"},
    [LOGIN_ROW_QUICK]    = {"Sign in with Quick Connect", "Approve a code from a signed-in Jellyfin app: no typing"},
    [LOGIN_ROW_PASSWORD] = {"Sign in with a password", "User name and password on the console's keyboard"},
};

/* The worker's one job at a time. Static, not page state: the worker outlives
   the page, which a successful sign-in replaces. */
typedef enum { JOB_NONE = 0, JOB_QUICK, JOB_PASSWORD } job_kind;

static struct {
    volatile job_kind kind;
    volatile int      cancel;
    volatile int      done;
    volatile jf_err   result;
    char              code[JF_QC_CODE_LEN];
    char              user[JF_USER_LEN];
    char              pass[JF_PASS_LEN];
} g_job;

static worker g_worker;

static int job_pass(void *user) {
    (void)user;
    if (g_job.kind == JOB_NONE || g_job.done) return 0;

    if (g_job.kind == JOB_PASSWORD) {
        g_job.result = jf_sign_in(g_job.user, g_job.pass);
        memset(g_job.pass, 0, sizeof(g_job.pass));
    } else {
        char     secret[JF_QC_SECRET_LEN];
        unsigned began;
        int      approved = 0;
        jf_err   e        = jf_quick_connect_begin(g_job.code, sizeof(g_job.code), secret, sizeof(secret));

        began = platform_clock_us();
        while (e == JF_OK && !approved && !g_job.cancel) {
            if (platform_clock_us() - began > QC_GIVE_UP_US) {
                e = JF_ERR_AUTH;
                break;
            }
            platform_sleep_us(QC_POLL_US);
            if (!g_job.cancel) e = jf_quick_connect_check(secret, &approved);
        }
        if (e == JF_OK && approved && !g_job.cancel) e = jf_quick_connect_finish(secret);
        memset(secret, 0, sizeof(secret));
        g_job.result = g_job.cancel ? JF_ERR_AUTH : e;
    }
    g_job.done = 1;
    return 1;
}

typedef struct {
    jf_conn conn;
    char    status[96];
    char    value[LOGIN_ROW_N][JF_HOST_LEN];
    int     busy;     /* a job is out */
    char    user[JF_USER_LEN];
    unsigned frame;
} login_state;

static login_state *now(void) {
    static login_state none;

    return screen_top_state() ? (login_state *)screen_top_state() : &none;
}

const char *login_page_value(int row) { return (row >= 0 && row < LOGIN_ROW_N) ? now()->value[row] : ""; }
const char *login_page_status(void) { return now()->status; }

/* What the viewer last used, so changing one field is not typing them all. */
static void enter(void *st, const void *arg, unsigned arg_len) {
    login_state *s = (login_state *)st;
    int          has_token;

    (void)arg;
    (void)arg_len;
    if (jf_load_account(&s->conn, &has_token) != 0 && jf_load_conn(&s->conn) != 0) {
        memset(&s->conn, 0, sizeof(s->conn));
        s->conn.port = 8096;
    }
    memset(s->conn.pass, 0, sizeof(s->conn.pass));
    snprintf(s->user, sizeof(s->user), "%s", s->conn.user);
    if (!worker_running(&g_worker)) worker_start(&g_worker, "login", job_pass, 0, 100000);
}

static int start(login_state *s, job_kind kind) {
    if (!s->conn.host[0]) {
        snprintf(s->status, sizeof(s->status), "Enter the server first");
        return -1;
    }
    if (s->conn.https && !http_tls_available()) {
        snprintf(s->status, sizeof(s->status), "This build cannot use HTTPS");
        return -1;
    }
    jf_use(&s->conn);
    memset(g_job.code, 0, sizeof(g_job.code));
    g_job.cancel = 0;
    g_job.done   = 0;
    g_job.kind   = kind;
    s->busy      = 1;
    s->status[0] = 0;
    return 0;
}

static const char *failure(jf_err e, int quick) {
    switch (e) {
    case JF_ERR_OFFLINE: return "Nothing answered at that address";
    case JF_ERR_AUTH: return quick ? "The code was not approved in time" : "Wrong user name or password";
    case JF_ERR_MISSING: return quick ? "Quick Connect is off on this server" : "That is not a Jellyfin server";
    case JF_ERR_GARBLED: return "Something answered, but not Jellyfin";
    default: return "The server refused";
    }
}

void login_take_address(const char *typed, jf_conn *c) {
    char  text[JF_HOST_LEN];
    char *p = text, *slash, *colon;

    snprintf(text, sizeof(text), "%s", typed ? typed : "");
    while (*p == ' ') p++;
    /* A pasted address often carries its scheme and port: take them. */
    if (!strncmp(p, "https://", 8)) {
        c->https = 1;
        if (c->port == 8096) c->port = 443;
        p += 8;
    } else if (!strncmp(p, "http://", 7)) {
        c->https = 0;
        if (c->port == 443) c->port = 8096;
        p += 7;
    }
    slash = strchr(p, '/');
    if (slash) *slash = 0;
    colon = strrchr(p, ':');
    if (colon) {
        *colon = 0;
        if (atoi(colon + 1) > 0 && atoi(colon + 1) <= 65535) c->port = atoi(colon + 1);
    }
    {
        char *e = p + strlen(p);

        while (e > p && e[-1] == ' ') *--e = 0;
    }
    snprintf(c->host, sizeof(c->host), "%s", p);
}

static void collect_keys(login_state *s) {
    char text[JF_HOST_LEN];

    if (keys_take(LOGIN_ROW_SERVER, text, sizeof(text))) login_take_address(text, &s->conn);
    if (keys_take(LOGIN_ROW_PORT, text, sizeof(text))) {
        int p = atoi(text);

        if (p > 0 && p <= 65535) s->conn.port = p;
        else snprintf(s->status, sizeof(s->status), "%.40s is not a port", text);
    }
    if (keys_take(TAG_USER, text, sizeof(text)) && text[0]) {
        snprintf(s->user, sizeof(s->user), "%.*s", (int)sizeof(s->user) - 1, text);
        keys_ask_for(TAG_PASS, "Password", "", JF_PASS_LEN - 1);
    }
    if (keys_take(TAG_PASS, text, sizeof(text))) {
        snprintf(g_job.user, sizeof(g_job.user), "%s", s->user);
        snprintf(g_job.pass, sizeof(g_job.pass), "%.*s", (int)sizeof(g_job.pass) - 1, text);
        memset(text, 0, sizeof(text));
        if (start(s, JOB_PASSWORD) != 0) memset(g_job.pass, 0, sizeof(g_job.pass));
    }
}

static void frame(ui_frame *ui, void *st) {
    login_state              *s       = (login_state *)st;
    static const ui_hint_item FORM[]  = {{UI_BTN_CROSS, "Select"}};
    static const ui_hint_item WAIT[]  = {{UI_BTN_CIRCLE, "Cancel"}};
    int                       has, focus_row = -1;
    ui_id                     id = ui_frame_focus(ui, &has);

    s->frame++;
    collect_keys(s);

    /* A job finished: in, or say why not. */
    if (s->busy && g_job.done) {
        int quick = g_job.kind == JOB_QUICK;

        s->busy    = 0;
        g_job.kind = JOB_NONE;
        if (g_job.result == JF_OK) {
            log_printf("login: signed in as %s", jf_user());
            session_retry();
            screen_reset(&boot_page_screen);
            return;
        }
        /* "Nothing answered" alone cannot tell a name that does not resolve
           from a firewall or a certificate: say what the transport said. */
        if (g_job.result == JF_ERR_OFFLINE && jf_last_reason()[0])
            snprintf(s->status, sizeof(s->status), "%.90s", jf_last_reason());
        else
            snprintf(s->status, sizeof(s->status), "%s", failure(g_job.result, quick));
    }

    if (has && (int)id < LOGIN_ROW_N) focus_row = (int)id;
    if (!s->busy && focus_row >= 0 && (ui->pressed & PAD_CROSS)) {
        switch (focus_row) {
        case LOGIN_ROW_SERVER: keys_ask_for(LOGIN_ROW_SERVER, "Server address", s->conn.host, JF_HOST_LEN - 1); break;
        case LOGIN_ROW_PORT: {
            char p[8];

            snprintf(p, sizeof(p), "%d", s->conn.port);
            keys_ask_for(LOGIN_ROW_PORT, "Port", p, 5);
            break;
        }
        case LOGIN_ROW_HTTPS:
            s->conn.https = !s->conn.https;
            /* The usual ports follow the switch; a chosen one stays. */
            if (s->conn.https && s->conn.port == 8096) s->conn.port = 443;
            else if (!s->conn.https && s->conn.port == 443) s->conn.port = 8096;
            break;
        case LOGIN_ROW_QUICK: (void)start(s, JOB_QUICK); break;
        case LOGIN_ROW_PASSWORD: keys_ask_for(TAG_USER, "User name", s->user, JF_USER_LEN - 1); break;
        default: break;
        }
    }

    snprintf(s->value[LOGIN_ROW_SERVER], sizeof(s->value[0]), "%s", s->conn.host[0] ? s->conn.host : "not set");
    snprintf(s->value[LOGIN_ROW_PORT], sizeof(s->value[0]), "%d", s->conn.port);
    snprintf(s->value[LOGIN_ROW_HTTPS], sizeof(s->value[0]), "%s", s->conn.https ? "On" : "Off");
    s->value[LOGIN_ROW_QUICK][0]    = 0;
    s->value[LOGIN_ROW_PASSWORD][0] = 0;

    ui_page_busy(s->busy);
    if (s->busy && g_job.kind == JOB_QUICK) {
        char head[48];

        ui_page_begin(&ui->layout, UI_PAGE_TEXT, "Sign in", 0, -1);
        if (g_job.code[0]) {
            snprintf(head, sizeof(head), "Code  %s", g_job.code);
            (void)ui_notice(ui, 0, head,
                            "On a signed-in Jellyfin app or the web page, open Quick Connect "
                            "(under your profile) and enter this code.",
                            0);
        } else {
            ui_waiting(&ui->layout, "Asking the server for a code", s->frame);
        }
        ui_page_end(&ui->layout, 0, 0, WAIT, 1);
        return;
    }

    ui_page_begin(&ui->layout, UI_PAGE_ROWS, "Sign in", s->status[0] ? s->status : (s->busy ? "Signing in" : 0), -1);
    (void)ui_option(ui, LOGIN_ROW_SERVER, kRow[LOGIN_ROW_SERVER].label, s->value[LOGIN_ROW_SERVER], UI_OPTION_NONE, 0, 0, 0);
    (void)ui_option(ui, LOGIN_ROW_PORT, kRow[LOGIN_ROW_PORT].label, s->value[LOGIN_ROW_PORT], UI_OPTION_NONE, 0, 0, 0);
    (void)ui_option(ui, LOGIN_ROW_HTTPS, kRow[LOGIN_ROW_HTTPS].label, 0, UI_OPTION_SWITCH, s->conn.https, 0, 1);
    (void)ui_option(ui, LOGIN_ROW_QUICK, kRow[LOGIN_ROW_QUICK].label, s->value[LOGIN_ROW_QUICK], UI_OPTION_NONE, 0, 0, 0);
    (void)ui_option(ui, LOGIN_ROW_PASSWORD, kRow[LOGIN_ROW_PASSWORD].label, s->value[LOGIN_ROW_PASSWORD], UI_OPTION_NONE, 0, 0, 0);
    ui_caption(&ui->layout, focus_row >= 0 ? kRow[focus_row].help : "");
    ui_page_end(&ui->layout, FORM, 1, 0, 0);
}

/* Circle cancels a Quick Connect wait; on the form it does nothing, because
   underneath is a library this viewer is not signed in to. */
static int back(void *st) {
    login_state *s = (login_state *)st;

    if (s->busy && g_job.kind == JOB_QUICK) {
        g_job.cancel = 1;
        snprintf(s->status, sizeof(s->status), "Cancelled");
    }
    return 1;
}

static void describe(void *st, char *out, unsigned n) {
    const login_state *s = (const login_state *)st;

    snprintf(out, n, "server: %s\nport: %s\nhttps: %s\nbusy: %d\ncode: %s\nstatus: %s\n", s->value[LOGIN_ROW_SERVER],
             s->value[LOGIN_ROW_PORT], s->value[LOGIN_ROW_HTTPS], s->busy, g_job.code[0] ? g_job.code : "none",
             s->status[0] ? s->status : "none");
}

const screen_def login_page_screen = {"login", sizeof(login_state), 0, enter, frame, back, 0, describe};

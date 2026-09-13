#include "model/session.h"

#include "base/log.h"
#include "base/standby.h"
#include "io/http.h"
#include "io/net.h"
#include "port/platform.h"

#include <stdio.h>
#include <string.h>

/* At 96 the start-up messages were cut mid-word. */
#define SESSION_ERR_MAX 160

/* A healthy join plus sign-in measures about four seconds. */
#define BOUND_LINK_US 40000000u

/* One dropped packet on a busy access point is not offline. */
#define LINK_MISS_MAX 2

/* The firmware reported the association gone for 2.6 s while a film streamed
   over it. Coming back is believed at once. */
#define LINK_DOWN_HOLD_US 5000000u

static volatile lib_stage g_stage;
static volatile int       g_connected;
static char               g_error[SESSION_ERR_MAX];
static unsigned           g_link_sent_us;
static volatile int       g_want_connect;

static volatile int g_link_down;
static int          g_radio_down;
static unsigned     g_down_since;
static volatile int g_link_miss;
static int          g_link_came_back;
static int          g_link_went;
static int          g_was_lost;
static unsigned     g_epoch_seen = 1;

static void set_error(const char *what) { snprintf(g_error, sizeof(g_error), "%s", what ? what : ""); }

void session_init(void) {
    g_stage        = LIB_STAGE_STARTING;
    g_want_connect = 1;
}

lib_stage   session_stage(void) { return g_stage; }
const char *session_error(void) { return g_error; }
int         session_connected(void) { return g_connected; }

int session_lost(void) { return g_link_down || g_link_miss >= LINK_MISS_MAX; }

const char *session_address(void) {
    static char at[JF_HOST_LEN + 8];
    char        host[JF_HOST_LEN];
    int         port = 0;

    jf_address(host, sizeof(host), &port);
    if (!host[0]) return "";
    snprintf(at, sizeof(at), "%s:%d", host, port);
    return at;
}

const char *session_stage_text(lib_stage s) {
    switch (s) {
    case LIB_STAGE_LINKING: return "Joining the network";
    case LIB_STAGE_SIGNIN: return "Signing in";
    case LIB_STAGE_READY: return "Connected";
    case LIB_STAGE_STOPPED: return "Stopped";
    default: return "Starting";
    }
}

void session_note_reached(void) {
    g_connected = 1;
    g_link_miss = 0;
}

void session_note_failure(jf_err e) {
    set_error(jf_err_text(e));
    if (e != JF_ERR_OFFLINE || !g_connected) {
        g_link_miss = 0;
        return;
    }
    if (g_link_miss < LINK_MISS_MAX) g_link_miss++;
}

int session_wants_connect(void) {
    int want = g_want_connect;

    g_want_connect = 0;
    return want;
}

void session_connect(void) {
    jf_conn conn;
    jf_err  e;
    int     profile, rc;
    char    ip[24];

    g_link_sent_us = platform_clock_us();
    g_stage        = LIB_STAGE_LINKING;

    if (jf_load_conn(&conn) != 0) {
        session_note_stopped("There is no connection file. Put jellyfin.txt beside the program, with a line reading: host 192.168.0.2");
        return;
    }
    jf_use(&conn);
    /* net_profile() is 0 until the viewer picks, which sends start-up to the
       dialog; after that a wake rejoins without asking. */
    profile = conn.profile > 0 ? conn.profile : net_profile();

    rc = net_start();
    if (rc != 0) {
        session_note_stopped(rc == NET_START_NO_SWITCH
                                 ? "The WLAN switch on the side of the console is off. Slide it on, then try again."
                                 : "The console's network stack would not start. pocketfin.log says which call refused.");
        return;
    }

    ip[0] = 0;
    if (net_connect(profile, ip, sizeof(ip)) != 0) {
        char why[SESSION_ERR_MAX];

        if (profile < 1)
            snprintf(why, sizeof(why), "Choose a network to use.");
        else
            snprintf(why, sizeof(why), "Saved connection %d would not join. Check it in the console's own network settings.", profile);
        session_note_stopped(why);
        return;
    }
    log_printf("session: linked on connection %d, %s", net_profile(), net_address()[0] ? net_address() : "no address");

    g_stage = LIB_STAGE_SIGNIN;
    e       = jf_connect();
    if (e != JF_OK) {
        char why[SESSION_ERR_MAX];

        session_sign_in_text(e, session_address(), why, sizeof(why));
        session_note_stopped(why);
        return;
    }

    set_error("");
    session_note_reached();
    g_stage = LIB_STAGE_READY;
}

/* The request's own words -- "the session is no longer valid" -- read the same
   for a mistyped password as for a token that expired. */
void session_sign_in_text(jf_err e, const char *address, char *out, unsigned n) {
    switch (e) {
    case JF_ERR_AUTH: snprintf(out, n, "The server refused the user name or password in jellyfin.txt."); break;
    case JF_ERR_OFFLINE:
        snprintf(out, n, "Nothing answered at %s. Check the host and port in jellyfin.txt, and that the server is running.", address);
        break;
    case JF_ERR_MISSING:
    case JF_ERR_GARBLED: snprintf(out, n, "Something answered at %s, but not Jellyfin. Check the port in jellyfin.txt.", address); break;
    default: snprintf(out, n, "The server at %s could not sign in. Its own log says why.", address); break;
    }
}

void session_note_stopped(const char *why) {
    set_error(why);
    g_stage = LIB_STAGE_STOPPED;
}

void session_note_no_choice(void) { session_note_stopped("No connection was chosen. Try again and pick one from the list."); }

void session_retry(void) {
    g_link_miss = 0;
    g_stage     = LIB_STAGE_STARTING;
    set_error("");

    /* Retrying only the sign-in went out over an association a sleep had made
       stale. */
    if (net_state() == NET_UP) (void)net_revalidate();
    g_want_connect = 1;
}

static void watch_connect(void) {
    if (g_stage != LIB_STAGE_LINKING && g_stage != LIB_STAGE_SIGNIN) return;
    if (platform_clock_us() - g_link_sent_us < BOUND_LINK_US) return;

    log_printf("session: %s gave up after %u ms", g_stage == LIB_STAGE_LINKING ? "the link" : "the sign-in", BOUND_LINK_US / 1000u);
    session_note_stopped(g_stage == LIB_STAGE_LINKING ? "The network did not answer. Check the WLAN switch and try again."
                                                      : "The server did not answer the sign-in in time.");
}

void session_watch(void) {
    int was = g_link_down;
    int lost;

    g_link_came_back = 0;
    watch_connect();

    /* Only over an association this run made. */
    if (standby_epoch() != g_epoch_seen) {
        g_epoch_seen = standby_epoch();
        if (g_connected && net_joined()) {
            log_line("session: the machine slept -- rejoining");
            /* Requests on sockets the sleep killed otherwise wait out their
               bound: 43 s measured. Cleared at once, or the reconnect's own
               requests are cancelled too. */
            http_cancel();
            http_cancel_clear();
            session_retry();
        }
    }

    /* Not before the first link, or the offline screen covers the boot
       screen. */
    {
        int down = g_connected && net_state() != NET_UP;

        if (!down) {
            g_link_down = 0;
        } else {
            if (!g_radio_down) g_down_since = platform_clock_us();
            g_link_down = platform_clock_us() - g_down_since >= LINK_DOWN_HOLD_US;
        }
        g_radio_down = down;
    }

    if (was && !g_link_down) {
        log_line("session: the link is back");
        g_link_miss      = 0;
        g_link_came_back = 1;
    } else if (!was && g_link_down) {
        log_printf("session: the link went away (%s)", net_state_text(net_state()));
    }

    lost        = session_lost();
    g_link_went = lost && !g_was_lost;
    g_was_lost  = lost;
}

int session_link_returned(void) { return g_link_came_back; }
int session_link_went(void) { return g_link_went; }

#ifdef POCKETFIN_CHECKS

void session_fake_stage(lib_stage s, const char *fault) {
    g_stage     = s;
    g_connected = (s == LIB_STAGE_READY);
    /* Or an earlier standby check reads as a sleep here. */
    g_epoch_seen = standby_epoch();
    set_error(fault ? fault : "");
}

void session_fake_lost(int lost) {
    g_link_miss = lost ? LINK_MISS_MAX : 0;
    g_link_down = 0;
}

#endif

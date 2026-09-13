/* See remote.h. No file operations here: the link thread owns the transport
 * and this posts to it, so a caller never waits and two subsystems can never
 * be in the transport at once. */

#include "tools/remote.h"

#include "io/link.h"
#include "port/input.h"
#include "base/log.h"
#include "port/platform.h"

#include <stdio.h>
#include <string.h>

static int g_on;

static unsigned   g_last_seq; /* highest number completed */
static remote_cmd g_pending;
static unsigned   g_pending_seq;
static char       g_pending_arg[REMOTE_ARG_MAX];

static const char *g_cmd   = "cmd.txt";
static const char *g_ack   = "ack.txt";
static const char *g_state = "state.txt";

static remote_cmd g_in_flight;
static unsigned   g_in_flight_seq;

void remote_start(void) {
    if (!hostfs_up()) return;

    if (hostfs_watch(g_cmd) != 0) {
        log_line("remote: nothing left to watch cmd.txt with");
        return;
    }
    g_on = 1;
    hostfs_put(g_ack, "0\n");
    hostfs_put(g_state, "");
    log_line("remote: on");
}

int remote_on(void) { return g_on; }

void remote_use_channel(const char *cmd, const char *ack, const char *state) {
    g_cmd   = cmd ? cmd : "cmd.txt";
    g_ack   = ack ? ack : "ack.txt";
    g_state = state ? state : "state.txt";
    g_on    = 0;
}

unsigned remote_pad_named(const char *name) {
    if (!name) return 0;
    if (!strcmp(name, "up")) return PAD_UP;
    if (!strcmp(name, "down")) return PAD_DOWN;
    if (!strcmp(name, "left")) return PAD_LEFT;
    if (!strcmp(name, "right")) return PAD_RIGHT;
    if (!strcmp(name, "cross")) return PAD_CROSS;
    if (!strcmp(name, "circle")) return PAD_CIRCLE;
    if (!strcmp(name, "triangle")) return PAD_TRIANGLE;
    if (!strcmp(name, "square")) return PAD_SQUARE;
    if (!strcmp(name, "start")) return PAD_START;
    if (!strcmp(name, "select")) return PAD_SELECT;
    if (!strcmp(name, "l")) return PAD_L;
    if (!strcmp(name, "r")) return PAD_R;
    return 0;
}

static remote_cmd word_to_cmd(const char *w) {
    static const char *const kWord[REMOTE_QUIT + 1] = {
        [REMOTE_PING] = "ping",   [REMOTE_STATUS] = "status", [REMOTE_SHOT] = "shot",   [REMOTE_PAGE] = "page",
        [REMOTE_FOCUS] = "focus", [REMOTE_BACK] = "back",     [REMOTE_PRESS] = "press", [REMOTE_QUIT] = "quit"};
    int i;

    for (i = REMOTE_PING; i <= REMOTE_QUIT; i++)
        if (!strcmp(w, kWord[i])) return (remote_cmd)i;
    return REMOTE_NONE;
}

/* A line that will not run, answered anyway: refused only in the log, the
 * sender waits on an ack that never comes, indistinguishable from a hung
 * application. */
static void refuse(unsigned seq) {
    char n[24];

    snprintf(n, sizeof(n), "%u\n", seq);
    hostfs_put(g_ack, n);
    g_last_seq = seq;
}

/* The link hands a line over only when it has changed, so a command file left
 * in place is read once. The sequence number is the second guard, for a file
 * rewritten with a number already used. */
static void take_line(void) {
    char     line[HOSTFS_LINE_MAX], word[REMOTE_WORD_MAX];
    char     arg[REMOTE_ARG_MAX];
    unsigned seq = 0;

    if (!hostfs_line(line, sizeof(line))) return;

    word[0] = 0;
    arg[0]  = 0;
    if (sscanf(line, "%u %31s %63[^\n]", &seq, word, arg) < 2) {
        log_printf("remote: \"%s\" is not a command", line);
        if (seq > g_last_seq) refuse(seq);
        return;
    }
    /* A sender here writes CR LF, so the argument arrives as "home.ppm\r":
       EINVAL from the C runtime, a page name no strcmp matches. Trimmed here
       since three of the four readers fail silently. */
    while (arg[0] && (unsigned char)arg[strlen(arg) - 1] <= ' ') arg[strlen(arg) - 1] = 0;

    /* Said out loud: a command silently ignored is a script waiting for an
     * ack that will never come. */
    if (seq <= g_last_seq) {
        log_printf("remote: %u already done (last %u)", seq, g_last_seq);
        return;
    }
    if (seq == g_in_flight_seq) return;

    g_pending = word_to_cmd(word);
    if (g_pending == REMOTE_NONE) {
        log_printf("remote: %u \"%s\" is not a word this build knows", seq, word);
        refuse(seq);
        return;
    }
    g_pending_seq = seq;
    snprintf(g_pending_arg, sizeof(g_pending_arg), "%s", arg);
}

remote_cmd remote_take(char *arg, unsigned arg_len) {
    if (!g_on || g_in_flight != REMOTE_NONE) return REMOTE_NONE;

    take_line();
    if (g_pending == REMOTE_NONE) return REMOTE_NONE;

    g_in_flight     = g_pending;
    g_in_flight_seq = g_pending_seq;
    g_pending       = REMOTE_NONE;
    if (arg && arg_len) snprintf(arg, arg_len, "%s", g_pending_arg);
    return g_in_flight;
}

void remote_publish(const char *text) {
    if (g_on) hostfs_put(g_state, text);
}

void remote_done(void) {
    char n[24];

    if (!g_on || g_in_flight == REMOTE_NONE) return;
    snprintf(n, sizeof(n), "%u\n", g_in_flight_seq);
    hostfs_put(g_ack, n);
    g_last_seq      = g_in_flight_seq;
    g_in_flight     = REMOTE_NONE;
    g_in_flight_seq = 0;
}

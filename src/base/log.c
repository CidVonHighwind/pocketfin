/* See log.h. */

#include "base/log.h"

#include "port/platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define STAMP_MAX     16
#define ROOM_WAIT_US  50000u /* a couple of link-thread passes */
#define FLUSH_WAIT_US 120000u

/* A card write costs the same however few lines it carries. */
#define CARD_EVERY_US 100000u

static platform_lock *g_lock;
static unsigned       g_t0;
static log_stats      g_stats;

static unsigned (*g_post)(const void *bytes, unsigned n);
static int (*g_idle)(void);
/* So a link that has gone away costs one wait, not one per line. */
static int g_cable_stuck;

static char         g_card_path[128];
static char         g_card[LOG_BUF_BYTES];
static unsigned     g_card_used;
static long         g_card_size;
static char         g_scratch[LOG_BUF_BYTES];
static volatile int g_to_card, g_hurry, g_writer_on, g_writer_ended;
#ifdef POCKETFIN_CHECKS
static volatile unsigned g_card_faults;
#endif

static int ready(void) {
    if (!g_lock) {
        g_lock = platform_lock_new("log");
        g_t0   = platform_clock_us();
    }
    return g_lock != NULL;
}

static unsigned stamp(char *out, unsigned n) {
    unsigned ms = (platform_clock_us() - g_t0) / 1000u;
    int      sn = snprintf(out, n, "[%3u.%03u] ", ms / 1000u, ms % 1000u);

    return (sn < 0 || (unsigned)sn >= n) ? 0u : (unsigned)sn;
}

/* The files are appended to across power-cycles, so a run's start has to be
   findable. */
static unsigned banner(char *out, unsigned n) {
    char     st[STAMP_MAX];
    unsigned sn  = stamp(st, sizeof(st));
    int      len = snprintf(out, n, "\n%.*s======== a new run starts here ========\n", (int)sn, st);

    return (len < 0 || (unsigned)len >= n) ? 0u : (unsigned)len;
}

/* Caller holds the lock, so two threads' lines cannot interleave when the
   cable takes one in pieces. */
static void post_cable(const char *bytes, unsigned n) {
    unsigned sent = g_post(bytes, n), waited = 0;

    while (sent < n && !g_cable_stuck && waited < ROOM_WAIT_US) {
        platform_sleep_us(2000);
        waited += 2000;
        sent += g_post(bytes + sent, n - sent);
    }
    g_cable_stuck = sent < n;
    if (sent < n) g_stats.cable_dropped++;
}

/* A failed close is a failed write: a write a suspend interrupted once reported
   success at fwrite and lost its only copy. */
static int card_write(const char *bytes, unsigned n) {
    FILE  *f;
    size_t wrote;
    long   end;

#ifdef POCKETFIN_CHECKS
    if (g_card_faults) {
        g_card_faults--;
        return -1;
    }
#endif
    f = fopen(g_card_path, g_card_size > LOG_KEEP_BYTES ? "wb" : "ab");
    if (!f) return -1;
    wrote = fwrite(bytes, 1, n, f);
    end   = ftell(f);
    if (fclose(f) != 0 || wrote != n) return -1;
    g_card_size = end;
    return 0;
}

static void drain(void) {
    unsigned n;

    platform_lock_take(g_lock);
    n = g_card_used;
    memcpy(g_scratch, g_card, n);
    platform_lock_give(g_lock);
    if (!n || card_write(g_scratch, n) != 0) return;

    platform_lock_take(g_lock);
    g_card_used -= n;
    memmove(g_card, g_card + n, g_card_used);
    platform_lock_give(g_lock);
}

static int writer(void *arg) {
    unsigned last = platform_clock_us();

    (void)arg;
    while (g_writer_on) {
        if (g_hurry || platform_clock_us() - last >= CARD_EVERY_US) {
            g_hurry = 0;
            last    = platform_clock_us();
            drain();
        }
        platform_sleep_us(5000);
    }
    drain();
    g_writer_ended = 1;
    return 0;
}

static int card_empty(void) {
    int empty;

    platform_lock_take(g_lock);
    empty = !g_card_used || !g_writer_on;
    platform_lock_give(g_lock);
    return empty;
}

void log_cable(unsigned (*post)(const void *bytes, unsigned n), int (*idle)(void)) {
    char     line[64];
    unsigned n;

    if (!ready()) return;
    n = banner(line, sizeof(line));
    platform_lock_take(g_lock);
    g_post = post;
    g_idle = idle;
    if (post && n) post_cable(line, n);
    platform_lock_give(g_lock);
}

int log_open(const char *path) {
    FILE *f;

    if (!path || !path[0] || g_card_path[0] || !ready()) return -1;

    /* Appended: truncating would wipe a frozen run's evidence in the act of
       recovering from it. */
    f = fopen(path, "ab");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    g_card_size = ftell(f);
    fclose(f);

    snprintf(g_card_path, sizeof(g_card_path), "%s", path);
    g_card_used    = banner(g_card, sizeof(g_card));
    g_writer_ended = 0;
    g_writer_on    = 1;
    if (platform_thread_start("logwriter", writer, NULL) == 0) return 0;
    g_writer_on    = 0;
    g_card_path[0] = 0;
    return -1;
}

static void emit(const char *text, int forced) {
    char     line[STAMP_MAX + LOG_LINE_MAX];
    unsigned n, sn;
    int      cut;

    if (!text || !g_lock) return;

    sn  = stamp(line, STAMP_MAX);
    n   = (unsigned)strlen(text);
    cut = n > LOG_LINE_MAX - 2u;
    if (cut) n = LOG_LINE_MAX - 2u;
    memcpy(line + sn, text, n);
    n += sn;
    line[n++] = '\n';

    platform_lock_take(g_lock);
    g_stats.lines++;
    if (cut) g_stats.truncated++;
    if (g_card_path[0] && (g_to_card || forced)) {
        if (g_card_used + n <= sizeof(g_card)) {
            memcpy(g_card + g_card_used, line, n);
            g_card_used += n;
            g_stats.card_lines++;
        } else {
            g_stats.card_dropped++;
        }
    }
    if (g_post) post_cable(line, n);
    platform_lock_give(g_lock);
}

void log_line(const char *text) { emit(text, 0); }

void log_printf(const char *fmt, ...) {
    char    line[LOG_LINE_MAX];
    va_list ap;

    if (!fmt) return;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    log_line(line);
}

void log_mark(const char *line) {
    unsigned t0 = platform_clock_us();

    emit(line, 1);
    while (g_lock && !card_empty() && platform_clock_us() - t0 <= FLUSH_WAIT_US) {
        g_hurry = 1;
        platform_sleep_us(2000);
    }
}

void log_flush(void) {
    unsigned t0 = platform_clock_us();

    if (!g_lock) return;
    while (!card_empty() || (g_idle && !g_idle())) {
        if (platform_clock_us() - t0 > FLUSH_WAIT_US) return;
        g_hurry = 1;
        platform_sleep_us(2000);
    }
}

void log_close(void) {
    unsigned t0;

    if (!g_lock) return;
    /* Through the log, not printf: pspsh block-buffers its shell stream. */
    if (g_stats.cable_dropped || g_stats.card_dropped)
        log_printf("log: dropped %u lines from the cable, %u from the card", g_stats.cable_dropped, g_stats.card_dropped);
    log_flush();

    g_writer_on = 0;
    t0          = platform_clock_us();
    while (g_card_path[0] && !g_writer_ended && platform_clock_us() - t0 <= FLUSH_WAIT_US) platform_sleep_us(2000);

    platform_lock_take(g_lock);
    g_post         = NULL;
    g_card_path[0] = 0;
    platform_lock_give(g_lock);
}

const char *log_card_path(void) { return g_card_path; }

void log_to_card(int on) { g_to_card = on ? 1 : 0; }

void log_get_stats(log_stats *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!g_lock) return;
    platform_lock_take(g_lock);
    *out = g_stats;
    platform_lock_give(g_lock);
}

#ifdef POCKETFIN_CHECKS
void log_fault_card(unsigned n) { g_card_faults = n; }

void log_reset_stats(void) {
    if (!g_lock) return;
    platform_lock_take(g_lock);
    memset(&g_stats, 0, sizeof(g_stats));
    platform_lock_give(g_lock);
}
#endif

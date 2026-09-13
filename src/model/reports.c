#include "model/reports.h"

#include "model/catalog.h"
#include "model/session.h"
#include "base/log.h"
#include "base/prefs.h"
#include "base/worker.h"
#include "port/platform.h"

#include <stdio.h>
#include <string.h>

/* A seek's worth: the stop, the encoder teardown, the start and one progress.
   Deeper than the player can fill before the worker drains it. */
#define REPORTS_MAX 4

#define IDLE_US 20000u

static play_job       g_q[REPORTS_MAX];
static volatile int   g_n;
static platform_lock *g_lock;
static worker         g_worker;
static char           g_reply[4096];
static jf_buf         g_rb = JF_BUF(g_reply);

static platform_lock *lock(void) {
    if (!g_lock) g_lock = platform_lock_new("reports");
    return g_lock;
}

void reports_send(play_report what, const char *item_id, const char *session, unsigned long long ticks, int paused) {
    int i;

    /* Not gated on the connection: that is the drain's question, and a stop
       arriving mid-fetch must not be dropped. */
    if (!item_id || !item_id[0]) return;
    if (what != REPORT_ENCODING_OFF && !pref(PREF_REPORT_PLAYBACK)) return;

    platform_lock_take(lock());
    for (i = 0; i < g_n; i++)
        if (what == REPORT_PROGRESS && g_q[i].what == REPORT_PROGRESS) break;
    if (i == g_n && g_n < REPORTS_MAX) g_n++;
    if (i < g_n) {
        g_q[i].what = what;
        snprintf(g_q[i].id, sizeof(g_q[i].id), "%s", item_id);
        snprintf(g_q[i].session, sizeof(g_q[i].session), "%s", session ? session : "");
        g_q[i].ticks  = ticks;
        g_q[i].paused = paused ? 1 : 0;
    }
    platform_lock_give(lock());
}

int reports_take(play_job *out) {
    int got;

    platform_lock_take(lock());
    got = g_n > 0;
    if (got) {
        *out = g_q[0];
        memmove(g_q, g_q + 1, (unsigned)(g_n - 1) * sizeof(g_q[0]));
        g_n--;
    }
    platform_lock_give(lock());
    return got;
}

int reports_pending(void) { return g_n; }

/* Oldest first: a seek's stop and start must reach the server in order. */
static int serve(void *arg) {
    play_job j;

    (void)arg;
    if (!g_n || session_stage() != LIB_STAGE_READY) return 0;
    while (reports_take(&j)) {
        jf_err e;

        switch (j.what) {
        case REPORT_START: e = jf_report_start(&g_rb, j.id, j.session, j.ticks); break;
        case REPORT_PROGRESS: e = jf_report_progress(&g_rb, j.id, j.session, j.ticks, j.paused); break;
        case REPORT_STOP:
            e = jf_report_stop(&g_rb, j.id, j.session, j.ticks);
            if (e == JF_OK) library_note_stopped(j.id, j.ticks);
            break;
        default: e = jf_stop_encoding(&g_rb, j.session); break;
        }
        /* Not session_note_failure: a missed report is not the link going
           down. */
        if (e != JF_OK) log_printf("play: report %d -- %s", j.what, jf_err_text(e));
    }
    return 1;
}

int reports_start(void) {
    if (worker_running(&g_worker)) return 0;
    if (!lock()) return -1;
    return worker_start(&g_worker, "reports", serve, 0, IDLE_US);
}

void reports_stop(void) { (void)worker_stop(&g_worker, 20000000u); }

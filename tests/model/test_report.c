/* See model/reports.h. The queue, not the POSTs: those write to the viewer's
 * real library with no undo. */

#include "model/reports.h"

#include "base/prefs.h"
#include "tools/selftest.h"

#include <stdio.h>

/* No worker runs in a check, so what is pushed stays pushed. */
static void drain(void) {
    play_job j;

    while (reports_take(&j)) {}
}

static int on(int yes) {
    int was = pref(PREF_REPORT_PLAYBACK);

    pref_set(PREF_REPORT_PLAYBACK, yes);
    return was;
}

/* One goes out every ten seconds, and a link that is not answering must not
   build a backlog of positions the viewer left minutes ago. */
static int t_a_progress_replaces_a_progress(char *note, unsigned n) {
    play_job j;
    int      was = on(1), pending;

    drain();
    reports_send(REPORT_PROGRESS, "item", "sess", 100, 0);
    reports_send(REPORT_PROGRESS, "item", "sess", 200, 0);
    reports_send(REPORT_PROGRESS, "item", "sess", 300, 1);
    pending = reports_pending();
    reports_take(&j);
    drain();
    on(was);

    if (pending != 1) {
        snprintf(note, n, "three progress reports queued %d, not 1", pending);
        return 1;
    }
    if (j.what != REPORT_PROGRESS || j.ticks != 300 || !j.paused) {
        snprintf(note, n, "the survivor was %d at %llu paused %d", (int)j.what, j.ticks, j.paused);
        return 1;
    }
    snprintf(note, n, "three became one, the newest");
    return 0;
}

/* A seek stops one session and starts another in the same frame: a start
   over the stop leaves the viewer watching the same film twice, a stop over
   the start reports against a session the server never opened. */
static int t_a_stop_is_never_displaced(char *note, unsigned n) {
    play_job j;
    int      was = on(1), pending;

    drain();
    reports_send(REPORT_STOP, "item", "old", 500, 0);
    reports_send(REPORT_START, "item", "new", 500, 0);
    reports_send(REPORT_PROGRESS, "item", "new", 600, 0);
    pending = reports_pending();
    reports_take(&j);
    drain();
    on(was);

    if (pending != 3) {
        snprintf(note, n, "a seek's three reports queued %d", pending);
        return 1;
    }
    if (j.what != REPORT_STOP) {
        snprintf(note, n, "the first one out was %d, not the stop", (int)j.what);
        return 1;
    }
    snprintf(note, n, "stop, start, progress -- in that order");
    return 0;
}

/* These move the resume point and can mark an item watched on every client.
   The teardown writes nothing to the library, and a server left encoding
   makes the next film start slowly. */
static int t_the_viewers_switch_is_the_gate(char *note, unsigned n) {
    int was = on(0), gated, teardown;

    drain();
    reports_send(REPORT_START, "item", "sess", 0, 0);
    reports_send(REPORT_PROGRESS, "item", "sess", 100, 0);
    reports_send(REPORT_STOP, "item", "sess", 200, 0);
    gated = reports_pending();

    reports_send(REPORT_ENCODING_OFF, "item", "sess", 0, 0);
    teardown = reports_pending();
    drain();
    on(was);

    if (gated != 0) {
        snprintf(note, n, "%d reports queued with reporting off", gated);
        return 1;
    }
    if (teardown != 1) {
        snprintf(note, n, "the teardown was gated too");
        return 1;
    }
    snprintf(note, n, "three refused, the teardown allowed");
    return 0;
}

/* The developer's page plays with no item. */
static int t_an_item_with_no_id_reports_nothing(char *note, unsigned n) {
    int was = on(1), pending;

    drain();
    reports_send(REPORT_START, "", "sess", 0, 0);
    pending = reports_pending();
    drain();
    on(was);

    if (pending != 0) {
        snprintf(note, n, "an empty id queued %d", pending);
        return 1;
    }
    snprintf(note, n, "nothing queued");
    return 0;
}

/* It only fills when the link has stopped answering, and what is already
   queued is older and more important than what is arriving. */
static int t_a_full_queue_keeps_what_it_has(char *note, unsigned n) {
    play_job j;
    int      was = on(1), pending, i;

    drain();
    reports_send(REPORT_STOP, "item", "sess", 1, 0);
    for (i = 0; i < 10; i++) reports_send(REPORT_START, "item", "sess", (unsigned long long)i, 0);
    pending = reports_pending();
    reports_take(&j);
    drain();
    on(was);

    if (pending > 4) {
        snprintf(note, n, "the queue grew to %d", pending);
        return 1;
    }
    if (j.what != REPORT_STOP) {
        snprintf(note, n, "the stop was pushed out by %d", (int)j.what);
        return 1;
    }
    snprintf(note, n, "held at %d, the stop still first", pending);
    return 0;
}

void test_report_register(void) {
    selftest_add("report", "a progress replaces a progress", t_a_progress_replaces_a_progress);
    selftest_add("report", "a stop is never displaced", t_a_stop_is_never_displaced);
    selftest_add("report", "the viewer's switch is the gate", t_the_viewers_switch_is_the_gate);
    selftest_add("report", "an item with no id reports nothing", t_an_item_with_no_id_reports_nothing);
    selftest_add("report", "a full queue keeps what it has", t_a_full_queue_keeps_what_it_has);
}

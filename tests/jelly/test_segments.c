/* See jelly/segments.h. A 404 is "the encoder is behind" or "the film ended"
 * depending only on where it happened; reading every one as the end marks
 * items watched and rolls on -- a stream that stumbles walks a season in
 * seconds. */

#include "jelly/segments.h"

#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

/* Shared: a megabyte per check, of the console's twenty-four, held for the
   life of the run. */
static unsigned char g_stage[SEG_STAGE_MIN];

/* A film long enough that the slack is not the whole of it. */
#define RUN_TICKS (600ull * ITEM_TICKS_PER_S) /* ten minutes -> 599 segments */

static void fake(segments *g, unsigned char *stage, unsigned cap, unsigned long long run_ticks) {
    jf_hls h;

    memset(&h, 0, sizeof(h));
    snprintf(h.dir, sizeof(h.dir), "/videos/abc");
    snprintf(h.query, sizeof(h.query), "api_key=x");
    seg_open(g, &h, run_ticks, stage, cap);
}

static void serve_at(int port) {
    jf_conn c;

    memset(&c, 0, sizeof(c));
    snprintf(c.host, sizeof(c.host), "127.0.0.1");
    c.port = port;
    jf_use(&c);
}

/* Live transcodes answer 404 for a segment the encoder has not reached;
   asking again is the whole of the fix. */
static int t_a_404_early_is_the_encoder_being_behind(char *note, unsigned n) {
    segments      g;
    seg_result    r;

    fake(&g, g_stage, sizeof(g_stage), RUN_TICKS);
    r = seg_verdict(&g, 3, 404, HTTP_OK, 2048);
    if (r != SEG_NOT_READY) {
        snprintf(note, n, "segment 3 of %d read as %s", g.last_seg, seg_result_text(r));
        return 1;
    }
    snprintf(note, n, "3 of %d: %s", g.last_seg, seg_result_text(r));
    return 0;
}

/* Runtime and the encoder's real length disagree -- measured serving live
   data two segments past ceil(runtime) -- so inside the slack the server is
   taken at its word. */
static int t_a_404_at_the_end_is_the_end(char *note, unsigned n) {
    segments      g;
    int           last, ended, not_ready;

    fake(&g, g_stage, sizeof(g_stage), RUN_TICKS);
    last      = g.last_seg;
    ended     = seg_verdict(&g, last, 404, HTTP_OK, 2048) == SEG_END;
    not_ready = seg_verdict(&g, last - SEG_END_SLACK - 1, 404, HTTP_OK, 2048) == SEG_NOT_READY;

    if (!ended || !not_ready) {
        snprintf(note, n, "at %d: %s, at %d: %s", last, seg_result_text(seg_verdict(&g, last, 404, HTTP_OK, 2048)), last - SEG_END_SLACK - 1,
                 seg_result_text(seg_verdict(&g, last - SEG_END_SLACK - 1, 404, HTTP_OK, 2048)));
        return 1;
    }
    snprintf(note, n, "the last %d are the end, before that it is not ready", SEG_END_SLACK + 1);
    return 0;
}

/* The safe direction to be wrong in: a film that stops early can be started
   again, and one that never stops never marks anything. */
static int t_without_a_runtime_a_404_ends_it(char *note, unsigned n) {
    segments      g;

    fake(&g, g_stage, sizeof(g_stage), 0);
    if (seg_verdict(&g, 1, 404, HTTP_OK, 2048) != SEG_END) {
        snprintf(note, n, "with no runtime, segment 1 read as %s", seg_result_text(seg_verdict(&g, 1, 404, HTTP_OK, 2048)));
        return 1;
    }
    if (seg_verdict(&g, -1, 404, HTTP_OK, 2048) != SEG_FAILED) {
        snprintf(note, n, "a missing init segment read as %s", seg_result_text(seg_verdict(&g, -1, 404, HTTP_OK, 2048)));
        return 1;
    }
    snprintf(note, n, "the end, and the init segment is a fault");
    return 0;
}

/* A run that ends on a bad moment reports a film watched that was not. */
static int t_a_server_error_is_not_an_ending(char *note, unsigned n) {
    segments      g;
    int           i;
    static const int STATUS[] = {500, 503, 403, 0};

    fake(&g, g_stage, sizeof(g_stage), RUN_TICKS);
    for (i = 0; i < 4; i++)
        if (seg_verdict(&g, 3, STATUS[i], HTTP_OK, 2048) != SEG_FAILED) {
            snprintf(note, n, "HTTP %d read as %s", STATUS[i], seg_result_text(seg_verdict(&g, 3, STATUS[i], HTTP_OK, 2048)));
            return 1;
        }
    snprintf(note, n, "500, 503, 403 and no status all failed");
    return 0;
}

/* Measured: resuming a film catches up with the transcode about twenty
   segments later, and it answers 404, 416, or a connection held open
   answering nothing -- read as a fault, playback stopped dead 19 s after
   every resume. */
static int t_outrunning_the_encoder_is_never_a_fault(char *note, unsigned n) {
    segments g;
    int      mid, i;
    static const struct {
        int         status;
        http_result rc;
        unsigned    len;
        const char *what;
    } NO[] = {{404, HTTP_OK, 0, "404"}, {416, HTTP_OK, 0, "416"}, {0, HTTP_NO_REPLY, 0, "no reply at all"}, {200, HTTP_OK, 0, "an empty 200"}};

    fake(&g, g_stage, sizeof(g_stage), RUN_TICKS);
    mid = g.last_seg / 2;

    for (i = 0; i < 4; i++) {
        seg_result r = seg_verdict(&g, mid, NO[i].status, NO[i].rc, NO[i].len);

        if (r != SEG_NOT_READY) {
            snprintf(note, n, "%s mid-film read as %s", NO[i].what, seg_result_text(r));
            return 1;
        }
    }

    /* Near the end the encoder cannot be the cause, so silence is the link. */
    if (seg_verdict(&g, g.last_seg, 416, HTTP_OK, 0) != SEG_END) {
        snprintf(note, n, "416 at the end read as %s", seg_result_text(seg_verdict(&g, g.last_seg, 416, HTTP_OK, 0)));
        return 1;
    }
    if (seg_verdict(&g, g.last_seg, 0, HTTP_NO_REPLY, 0) != SEG_FAILED) {
        snprintf(note, n, "a silent connection at the end read as %s", seg_result_text(seg_verdict(&g, g.last_seg, 0, HTTP_NO_REPLY, 0)));
        return 1;
    }
    snprintf(note, n, "404, 416, silence and an empty 200 are all \"not yet\" at %d of %d", mid, g.last_seg);
    return 0;
}

/* The key the server put in the query may since have been revoked; the rest of
   the query is the server's and must survive. */
static int t_a_url_carries_the_token_in_force(char *note, unsigned n) {
    jf_hls h;
    char   url[JF_HLS_URL_LEN];

    memset(&h, 0, sizeof(h));
    snprintf(h.dir, sizeof(h.dir), "/videos/abc");
    snprintf(h.query, sizeof(h.query), "MediaSourceId=m1&api_key=DEAD&PlaySessionId=p9&segmentContainer=mp4");

    jf_hls_url(&h, 4, url, sizeof(url));
    if (!strstr(url, "api_key=") || strstr(url, "DEAD")) {
        snprintf(note, n, "the old key survived: \"%s\"", url);
        return 1;
    }
    if (!strstr(url, "MediaSourceId=m1") || !strstr(url, "PlaySessionId=p9") || !strstr(url, "segmentContainer=mp4")) {
        snprintf(note, n, "the rest of the query did not survive: \"%s\"", url);
        return 1;
    }

    snprintf(h.query, sizeof(h.query), "ApiKey=DEAD&x=1");
    jf_hls_url(&h, 4, url, sizeof(url));
    if (!strstr(url, "ApiKey=") || strstr(url, "DEAD") || !strstr(url, "x=1")) {
        snprintf(note, n, "ApiKey was not replaced: \"%s\"", url);
        return 1;
    }
    snprintf(note, n, "both spellings replaced, the rest of the query untouched");
    return 0;
}

static int t_the_url_carries_what_the_server_demands(char *note, unsigned n) {
    jf_hls h;
    char   url[JF_HLS_URL_LEN], init[JF_HLS_URL_LEN];

    memset(&h, 0, sizeof(h));
    snprintf(h.dir, sizeof(h.dir), "/videos/abc");
    snprintf(h.query, sizeof(h.query), "api_key=x");

    jf_hls_url(&h, 7, url, sizeof(url));
    jf_hls_url(&h, -1, init, sizeof(init));

    if (!strstr(url, "/hls1/main/7.mp4")) {
        snprintf(note, n, "segment 7 is at \"%s\"", url);
        return 1;
    }
    /* Leaving either off is an HTTP 400, whatever their values. */
    if (!strstr(url, "runtimeTicks=") || !strstr(url, "actualSegmentLengthTicks=")) {
        snprintf(note, n, "the parameters the server demands are missing: \"%s\"", url);
        return 1;
    }
    if (!strstr(init, "/hls1/main/-1.mp4") || !strstr(init, "actualSegmentLengthTicks=0")) {
        snprintf(note, n, "the init segment is at \"%s\"", init);
        return 1;
    }
    snprintf(note, n, "7.mp4 and -1.mp4, both with the length parameters");
    return 0;
}

#ifndef __PSP__

#include "../io/test_http.h"
#include "base/standby.h"

/* A truncated fragment leaves the demuxer waiting for an mdat that never
   comes: a 526,185-byte segment against a 512 kB stage ended playback 17.6 s
   in, every time. */
static int t_a_segment_that_does_not_fit_is_dropped_whole(char *note, unsigned n) {
    static char reply[3000];
    segments    g;
    seg_result  r;
    unsigned    body = 2000, i, head;

    head = (unsigned)snprintf(reply, sizeof(reply), "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n", body);
    for (i = 0; i < body; i++) reply[head + i] = 'x';

    if (!fake_http_start_n(reply, head + body, 0, 2)) {
        snprintf(note, n, "could not start a server");
        return -1;
    }
    fake(&g, g_stage, sizeof(g_stage), RUN_TICKS);
    g.stage_cap = 512;

    serve_at(fake_http_port());
    r = seg_fetch(&g, 0);
    fake_http_wait();

    if (r != SEG_FAILED) {
        snprintf(note, n, "%u bytes into a 512-byte stage came back %s", body, seg_result_text(r));
        return 1;
    }
    if (g.len != 0 || !strstr(g.err, "does not fit")) {
        snprintf(note, n, "it kept %u bytes of it: \"%s\"", g.len, g.err);
        return 1;
    }
    snprintf(note, n, "dropped whole, nothing handed on");
    return 0;
}

/* Not an arrival: the demuxer would stall on a well-formed nothing. Not a
   fault: a live transcode has been measured answering empty for a segment it
   hasn't reached yet, and a fault ends a film with nothing wrong with it. */
static int t_an_empty_200_is_not_ready(char *note, unsigned n) {
    static const char EMPTY[] = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    segments          g;
    seg_result        r;

    if (!fake_http_start_n(EMPTY, (unsigned)strlen(EMPTY), 0, 2)) {
        snprintf(note, n, "could not start a server");
        return -1;
    }
    fake(&g, g_stage, sizeof(g_stage), RUN_TICKS);
    serve_at(fake_http_port());
    r = seg_fetch(&g, 0);
    fake_http_wait();

    if (r != SEG_NOT_READY) {
        snprintf(note, n, "an empty 200 mid-film came back %s", seg_result_text(r));
        return 1;
    }
    if (seg_verdict(&g, g.last_seg, 200, HTTP_OK, 0) != SEG_END) {
        snprintf(note, n, "an empty 200 at the end came back %s", seg_result_text(seg_verdict(&g, g.last_seg, 200, HTTP_OK, 0)));
        return 1;
    }
    snprintf(note, n, "mid-film it is not ready; at the end it is the end");
    return 0;
}

/* The counts make the next change in the encoder's output visible on the
   first run. */
static int t_a_segment_arrives_whole(char *note, unsigned n) {
    static char reply[3000];
    segments    g;
    seg_result  r;
    unsigned    body = 1500, i, head;

    head = (unsigned)snprintf(reply, sizeof(reply), "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n", body);
    for (i = 0; i < body; i++) reply[head + i] = (char)('a' + (i % 26));

    /* In pieces: the stage is filled across calls. */
    if (!fake_http_start_n(reply, head + body, 64, 2)) {
        snprintf(note, n, "could not start a server");
        return -1;
    }
    fake(&g, g_stage, sizeof(g_stage), RUN_TICKS);
    serve_at(fake_http_port());
    r = seg_fetch(&g, 0);
    fake_http_wait();

    if (r != SEG_ARRIVED) {
        snprintf(note, n, "%s: %s", seg_result_text(r), g.err);
        return 1;
    }
    if (g.len != body || g.bytes != body) {
        snprintf(note, n, "%u bytes of %u, %llu counted", g.len, body, g.bytes);
        return 1;
    }
    for (i = 0; i < body; i++)
        if (g_stage[i] != (unsigned char)('a' + (i % 26))) {
            snprintf(note, n, "byte %u came out %02x", i, g_stage[i]);
            return 1;
        }
    snprintf(note, n, "%u bytes, exact, across 64-byte pieces", g.len);
    return 0;
}


/* The server takes 80 ms to think before a first byte, so asking only after
   the last one finished wastes a tenth of every second. Reference measured the
   same trick on artwork: twelve posters, 325 ms to 167 ms. */
static int t_the_next_segment_is_asked_for_early(char *note, unsigned n) {
    static char reply[3000];
    segments    g;
    unsigned    body = 900, i, head;

    head = (unsigned)snprintf(reply, sizeof(reply), "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n", body);
    for (i = 0; i < body; i++) reply[head + i] = 'x';

    /* Twice: a server that answers once cannot show a socket already sent on. */
    if (!fake_http_start_n(reply, head + body, 8, 2)) {
        snprintf(note, n, "could not start a server");
        return -1;
    }
    fake(&g, g_stage, sizeof(g_stage), RUN_TICKS);
    serve_at(fake_http_port());

    if (seg_fetch(&g, 0) != SEG_ARRIVED) {
        snprintf(note, n, "the first segment did not arrive: %s", g.err);
        seg_close(&g);
        return 1;
    }
    if (g.pipelined) {
        snprintf(note, n, "the first fetch claimed a socket nobody had sent");
        seg_close(&g);
        return 1;
    }
    if (seg_fetch(&g, 1) != SEG_ARRIVED) {
        snprintf(note, n, "the second segment did not arrive: %s", g.err);
        seg_close(&g);
        return 1;
    }
    fake_http_wait();
    if (g.pipelined != 1) {
        snprintf(note, n, "the second fetch opened its own connection");
        seg_close(&g);
        return 1;
    }
    seg_close(&g);
    snprintf(note, n, "the second segment was already on its way");
    return 0;
}

/* After a seek the connection opened for the old next segment is worth
   nothing, and left open it holds a transcode. */
static int t_a_wrong_guess_is_dropped(char *note, unsigned n) {
    static char reply[3000];
    segments    g;
    unsigned    body = 900, i, head;

    head = (unsigned)snprintf(reply, sizeof(reply), "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n", body);
    for (i = 0; i < body; i++) reply[head + i] = 'x';

    if (!fake_http_start_n(reply, head + body, 8, 2)) {
        snprintf(note, n, "could not start a server");
        return -1;
    }
    fake(&g, g_stage, sizeof(g_stage), RUN_TICKS);
    serve_at(fake_http_port());

    if (seg_fetch(&g, 0) != SEG_ARRIVED) {
        snprintf(note, n, "the first segment did not arrive: %s", g.err);
        seg_close(&g);
        return 1;
    }
    /* Whether this arrives is the loopback server's business; the socket sent
       for segment 1 must not be handed segment 40, a wrong film rather than a
       slow one. */
    (void)seg_fetch(&g, 40);
    fake_http_wait();
    if (g.pipelined) {
        snprintf(note, n, "a fetch after a seek used a socket sent for somewhere else");
        seg_close(&g);
        return 1;
    }
    seg_close(&g);
    snprintf(note, n, "the guess was closed rather than used");
    return 0;
}

/* A standby as the loop sees it, with nothing to release or restore. */
static void sleep_once(void) {
    int i;

    standby_watch(0, 0, 0);
    standby_note_going();
    standby_step();
    standby_note_back();
    for (i = 0; i < 4 && standby_state() != STANDBY_UP; i++) standby_step();
}

/* Asked for before a standby, the connection may not have survived it: reused
   after the wake, a dead one held the worker for its 15 s read bound while the
   picture froze, and a live one only by luck did not. */
static int t_a_guess_from_before_a_standby_is_not_used(char *note, unsigned n) {
    static char reply[3000];
    segments    g;
    seg_result  r;
    unsigned    body = 900, i, head;

    head = (unsigned)snprintf(reply, sizeof(reply), "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n", body);
    for (i = 0; i < body; i++) reply[head + i] = 'x';

    /* The fetch, the guess sent during it, the fetch after the wake and its
       own guess. */
    if (!fake_http_start_n(reply, head + body, 8, 4)) {
        snprintf(note, n, "could not start a server");
        return -1;
    }
    fake(&g, g_stage, sizeof(g_stage), RUN_TICKS);
    serve_at(fake_http_port());

    if (seg_fetch(&g, 0) != SEG_ARRIVED) {
        snprintf(note, n, "the first segment did not arrive: %s", g.err);
        seg_close(&g);
        return 1;
    }
    sleep_once();
    r = seg_fetch(&g, 1);
    seg_close(&g);
    fake_http_wait();

    if (r != SEG_ARRIVED) {
        snprintf(note, n, "after the wake the segment came back %s: %s", seg_result_text(r), g.err);
        return 1;
    }
    if (g.pipelined) {
        snprintf(note, n, "the fetch after a standby read the connection asked for before it");
        return 1;
    }
    snprintf(note, n, "the guess from before the standby was closed, the segment fetched fresh");
    return 0;
}

#endif

/* Past its end Jellyfin keeps answering 200 with a body: measured fetching
   648 segments beyond a 24-minute episode and replaying them. The port is
   closed, so reaching the network at all would answer SEG_FAILED. */
static int t_the_runtime_ends_the_film_not_the_server(char *note, unsigned n) {
    segments g;
    int      last;

    serve_at(1);

    fake(&g, g_stage, sizeof(g_stage), RUN_TICKS);
    last = g.last_seg;

    if (seg_fetch(&g, last + 1) != SEG_END) {
        snprintf(note, n, "%d of %d went to the server", last + 1, last);
        return 1;
    }
    /* The last one is a real part of the film. */
    if (seg_verdict(&g, last, 200, HTTP_OK, 2048) != SEG_ARRIVED) {
        snprintf(note, n, "the last segment %d was refused", last);
        return 1;
    }
    snprintf(note, n, "%d arrives, %d is the end, without asking", last, last + 1);
    return 0;
}

void test_segments_register(void) {
    selftest_add("segments", "the runtime ends the film, not the server", t_the_runtime_ends_the_film_not_the_server);
    selftest_add("segments", "a 404 early is the encoder being behind", t_a_404_early_is_the_encoder_being_behind);
    selftest_add("segments", "a 404 at the end is the end", t_a_404_at_the_end_is_the_end);
    selftest_add("segments", "without a runtime a 404 ends it", t_without_a_runtime_a_404_ends_it);
    selftest_add("segments", "a server error is not an ending", t_a_server_error_is_not_an_ending);
    selftest_add("segments", "outrunning the encoder is never a fault", t_outrunning_the_encoder_is_never_a_fault);
    selftest_add("segments", "a url carries the token in force", t_a_url_carries_the_token_in_force);
    selftest_add("segments", "the url carries what the server demands", t_the_url_carries_what_the_server_demands);
#ifndef __PSP__
    selftest_add("segments", "a segment that does not fit is dropped whole", t_a_segment_that_does_not_fit_is_dropped_whole);
    selftest_add("segments", "an empty 200 is not ready", t_an_empty_200_is_not_ready);
    selftest_add("segments", "a segment arrives whole", t_a_segment_arrives_whole);
    selftest_add("segments", "the next segment is asked for early", t_the_next_segment_is_asked_for_early);
    selftest_add("segments", "a wrong guess is dropped", t_a_wrong_guess_is_dropped);
    selftest_add("segments", "a guess from before a standby is not used", t_a_guess_from_before_a_standby_is_not_used);
#endif
}

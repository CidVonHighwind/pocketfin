/* Against the real server: can this console, on this access point, pull a
 * second of film in less than a second? If not, it looks like a decoder fault.
 *
 * Timed through the playback path, not a static file: a live transcode arrives
 * in spurts and a static file does not -- 4.1 Mbit/s against 2.7 was measured
 * on the same console, in that order.
 *
 * Skipped, never red, without a server: a check that goes red because a
 * machine in the next room is off teaches everyone to ignore it. */

#include "jelly/segments.h"

#include "io/http.h"

#include "model/fmp4.h"
#include "port/decode.h"

#include "base/log.h"
#include "base/prefs.h"
#include "io/net.h"
#include "jelly/api.h"
#include "jelly/item.h"
#include "port/mem.h"
#include "port/platform.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

#define SEGMENTS 6

/* A named film at a fixed minute: a quarter of the way into whatever sorted
 * first landed on a dark stretch of 88 kB segments where the app sees 600 kB,
 * and said the link was fine while the player starved. */
#define BENCH_FILM   "King Kong"
#define BENCH_AT_MIN 10ull
#define BENCH_AT     (BENCH_AT_MIN * 60ull * ITEM_TICKS_PER_S)

/* The ceiling, not the viewer's setting: a measurement taken at 2,800 cannot
   say whether 4,400 would have arrived just as fast. */
#define BENCH_KBPS 4400u

/* A segment is 1.001 s of film. Fetching one in longer than that is a run
   that falls behind for ever, whatever the ring holds. */
#define BUDGET_MS 1001u

static char   g_reply[JF_LIST_BUF];
static jf_buf g_rb = JF_BUF(g_reply);
static item   g_rows[JF_LIST_MAX];

static int g_up;
/* The clock measurement drops the radio and brings it back between passes. */
static int g_profile = 1;

/* Every film a check opens is stopped, or the server keeps its transcode
   running under the application's own device id. */
static char g_session[JF_SESSION_LEN];

static void link_down(void) {
    if (!g_up) return;
    if (g_session[0]) (void)jf_stop_encoding(&g_rb, g_session);
    g_session[0] = 0;
    net_disconnect();
    g_up = 0;
}

/* The radio up on the saved connection, or 0 with `why` filled in. */
static int link_up(jf_conn *c, char *why, unsigned n) {
    char ip[24];

    if (jf_load_conn(c) != 0) {
        snprintf(why, n, "no jellyfin.txt -- nothing to ask");
        return 0;
    }
    jf_use(c);
    /* Nobody to ask, so not the console's dialog in front of a scripted run:
       without a profile line, the first saved connection that joins. */
    if (net_start() != 0) {
        snprintf(why, n, "the network stack would not start");
        return 0;
    }
    for (g_profile = c->profile < 1 ? 1 : c->profile; net_connect(g_profile, ip, sizeof(ip)) != 0; g_profile++)
        if (c->profile > 0 || g_profile == 10) {
            snprintf(why, n, "profile %d did not associate -- is the radio on?", c->profile > 0 ? c->profile : g_profile);
            return 0;
        }
    g_up = 1;
    return 1;
}

/* A signed-in server and a film to ask for, or 0 with `why` filled in. */
static int a_film(char *id, unsigned idlen, unsigned long long *run_ticks, char *why, unsigned n) {
    int     views = 0, v;
    jf_conn c;

    if (!link_up(&c, why, n)) return 0;

    if (jf_connect() != JF_OK || jf_views(&g_rb, g_rows, JF_LIST_MAX, &views) != JF_OK || views <= 0) {
        snprintf(why, n, "%s:%d did not answer", c.host, c.port);
        return 0;
    }

    /* A film over an episode: a cartoon episode transcodes to 82 kB a second
       where a film fills the 350 kB the cap allows, which is how a "passing"
       link check sat over a starving player. A series and a season carry a
       runtime too, and PlaybackInfo on either is refused. */
    {
        item found;
        int  have = 0;
        /* Copied out first: jf_items() refills g_rows, and the second pass
           asked for an item's id as a library's. */
        char parents[8][ITEM_ID_LEN];
        int  np = views < 8 ? views : 8;

        memset(&found, 0, sizeof(found));
        for (v = 0; v < np; v++) snprintf(parents[v], sizeof(parents[v]), "%.*s", (int)sizeof(g_rows[v].id) - 1, g_rows[v].id);

        for (v = 0; v < np; v++) {
            const char *parent = parents[v];
            int         got    = 0, i;

            if (jf_items(&g_rb, parent, ITEM_SORT_NAME, 0, ITEM_FILTER_ALL, g_rows, JF_LIST_MAX, &got, 0) != JF_OK) continue;
            for (i = 0; i < got; i++) {
                int rank = g_rows[i].kind == ITEM_KIND_MOVIE ? 2 : (g_rows[i].kind == ITEM_KIND_EPISODE ? 1 : 0);

                if (!rank || g_rows[i].run_ticks <= (unsigned long long)60 * ITEM_TICKS_PER_S) continue;
                /* So a measurement repeats against the same footage. */
                if (strstr(g_rows[i].name, BENCH_FILM)) rank = 3;
                /* Among equals the longest: length tracks bitrate closely
                   enough to pick the demanding case. */
                if (rank > have || (rank == have && g_rows[i].run_ticks > found.run_ticks)) {
                    found = g_rows[i];
                    have  = rank;
                }
            }
        }
        if (have) {
            snprintf(id, idlen, "%s", found.id);
            *run_ticks = found.run_ticks;
            snprintf(why, n, "%s", found.name);
            return 1;
        }
    }
    snprintf(why, n, "nothing playable in %d libraries", views);
    return 0;
}

/* Budget: the 1.001 s a segment carries, shared by fetch + demux +
   decode-for-25-pictures. Reference measured 4.1 Mbit/s, 0 starves, 5 s of
   buffer at a 2.8 Mbit/s cap; anything far off that here is the regression. */
#define BENCH_SEGMENTS 30
#define BENCH_SAMPLES  64

static struct {
    const unsigned char *data;
    unsigned             len;
} g_samp[BENCH_SAMPLES];
static int g_samp_n;

static int keep_video(const fmp4_sample *s, void *user) {
    (void)user;
    if (s->track != FMP4_VIDEO || g_samp_n >= BENCH_SAMPLES) return 0;
    g_samp[g_samp_n].data = s->data;
    g_samp[g_samp_n].len  = s->len;
    g_samp_n++;
    return 0;
}

static int t_every_stage_has_its_number(char *note, unsigned n) {
    jf_hls             h;
    segments           g;
    fmp4               m;
    char               id[JF_ID_LEN], what[96];
    unsigned long long run_ticks = 0, bytes = 0;
    unsigned char     *stage;
    unsigned           fetch_ms = 0, demux_us = 0, decode_us = 0;
    unsigned           worst_fetch = 0, largest = 0;
    int                segs = 0, pics = 0, i, stalls = 0;

    if (!a_film(id, sizeof(id), &run_ticks, what, sizeof(what))) {
        snprintf(note, n, "%s", what);
        link_down();
        return -1;
    }
    stage = (unsigned char *)mem_reserve(MEM_SEG_STAGE);
    if (!stage) {
        snprintf(note, n, "no segment stage");
        link_down();
        return 1;
    }
    {
        /* Never at zero: the logo and titles are near-black and transcode to
           a fraction of the cap, so measuring there says the link is four
           times slower than it is. */
        jf_err e = jf_hls_open(&g_rb, id, BENCH_AT, BENCH_KBPS * 1000u, -1, -1, &h, g_session, sizeof(g_session));

        if (e != JF_OK || seg_open(&g, &h, run_ticks, stage, mem_size(MEM_SEG_STAGE)) != 0) {
            snprintf(note, n, "\"%s\" would not open: %s", what, jf_err_text(e));
            link_down();
            return 1;
        }
    }

    /* The decoder cannot open without the init segment's parameter sets, and
       the init comes after a segment (seg_fetch()). Not timed. */
    for (i = 0; i < 3 && seg_fetch(&g, h.first_seg) != SEG_ARRIVED; i++) platform_sleep_us(200000);
    for (i = 0; i < 3 && seg_fetch(&g, -1) != SEG_ARRIVED; i++) platform_sleep_us(200000);
    if (g.init_len == 0 || fmp4_init(&m, g.init, g.init_len) != 0) {
        snprintf(note, n, "no initialisation segment: %s", g.err[0] ? g.err : "nothing arrived");
        seg_close(&g);
        link_down();
        return 1;
    }
    if (decode_available() && decode_open(m.video.sps, m.video.sps_len, m.video.pps, m.video.pps_len, m.video.nal_len_size,
                                          (int)m.video.width, (int)m.video.height) != 0) {
        snprintf(note, n, "the decoder refused this stream: %s", decode_error());
        seg_close(&g);
        link_down();
        return 1;
    }

    /* A transcode that has just started encodes at about real time, so the
       first segments come back at the encoder's pace: measured here, four
       segments is enough for it to get ahead. */
#define BENCH_WARMUP 4

    for (i = 0; i < BENCH_SEGMENTS + BENCH_WARMUP; i++) {
        unsigned   t0 = platform_clock_us(), ms;
        seg_result r  = seg_fetch(&g, h.first_seg + i);
        int        s;

        ms = (platform_clock_us() - t0) / 1000u;
        if (r == SEG_NOT_READY && ++stalls < 20) {
            platform_sleep_us(200000);
            i--;
            continue;
        }
        if (r != SEG_ARRIVED) break;
        if (i < BENCH_WARMUP) continue;

        /* The worst segment decides a stall and a mean hides it: 600 kB at
           3 Mbit/s is 1.6 s of link for 1.001 s of film. */
        log_printf("stage: seg %d  %u kB in %u ms", h.first_seg + i, g.len >> 10, ms);
        fetch_ms += ms;
        bytes += g.len;
        if (ms > worst_fetch) worst_fetch = ms;
        if (g.len > largest) largest = g.len;
        segs++;

        g_samp_n = 0;
        t0       = platform_clock_us();
        if (fmp4_fragment(&m, stage, g.len, keep_video, 0) <= 0) break;
        demux_us += platform_clock_us() - t0;

        if (!decode_available()) continue;
        for (s = 0; s < g_samp_n; s++) {
            decode_picture pic;
            int            got;

            memset(&pic, 0, sizeof(pic));
            t0  = platform_clock_us();
            got = decode_sample(g_samp[s].data, g_samp[s].len, &pic);
            decode_us += platform_clock_us() - t0;
            if (got > 0) pics++;
        }
    }
    if (decode_available()) decode_close();
    seg_close(&g);
    link_down();

    if (segs < BENCH_SEGMENTS) {
        snprintf(note, n, "%d of %d segments arrived: %s", segs, BENCH_SEGMENTS, g.err[0] ? g.err : "the link gave up");
        return 1;
    }

    {
        unsigned fetch_each = fetch_ms / (unsigned)segs;
        unsigned demux_each = demux_us / (unsigned)segs / 1000u;
        unsigned dec_each   = pics ? decode_us / (unsigned)pics : 0;
        unsigned kbit       = fetch_ms ? (unsigned)(bytes * 8ull / fetch_ms) : 0;
        unsigned spent      = fetch_each + demux_each + (pics ? (decode_us / (unsigned)segs) / 1000u : 0);

        /* Whatever the verdict: the only place these are measured together on
           the machine that has to do it. */
        log_printf("stage: \"%s\" %d segments, largest %u kB", what, segs, largest >> 10);
        log_printf("stage: fetch  %u ms a segment (worst %u), %u kbit/s", fetch_each, worst_fetch, kbit);
        log_printf("stage: demux  %u ms a segment", demux_each);
        log_printf("stage: decode %u us a picture, %d pictures over %d segments", dec_each, pics, segs);
        log_printf("stage: %u ms of the 1001 ms each segment carries", spent);

        if (spent >= BUDGET_MS) {
            snprintf(note, n, "%u ms spent on 1001 ms of film: fetch %u, demux %u, decode %u us a picture", spent, fetch_each, demux_each,
                     dec_each);
            return 1;
        }
        snprintf(note, n, "%u ms of 1001: fetch %u (%u kbit/s), demux %u, dec %u us x%d", spent, fetch_each, kbit, demux_each, dec_each,
                 pics / (segs ? segs : 1));
    }
    return 0;
}

/* The player's own opening, far into a film: resumed at 1:55:56, King Kong
   once waited 175 s for its init segment. */
#define START_FAR_AT   (6956ull * JF_HLS_SEG_TICKS)
#define START_BOUND_MS 10000u

static int t_a_film_far_in_starts_in_seconds(char *note, unsigned n) {
    jf_hls             h;
    segments           g;
    char               id[JF_ID_LEN], what[96];
    unsigned long long run_ticks = 0;
    unsigned char     *stage;
    unsigned           t0, ms;
    seg_result         r;
    jf_err             e;

    if (!a_film(id, sizeof(id), &run_ticks, what, sizeof(what))) {
        snprintf(note, n, "%s", what);
        link_down();
        return -1;
    }
    if (run_ticks <= START_FAR_AT) {
        snprintf(note, n, "\"%s\" ends before 1:55:56", what);
        link_down();
        return -1;
    }
    stage = (unsigned char *)mem_reserve(MEM_SEG_STAGE);
    e     = jf_hls_open(&g_rb, id, START_FAR_AT, (unsigned)pref(PREF_BITRATE_KBPS) * 1000u, -1, -1, &h, g_session, sizeof(g_session));
    if (!stage || e != JF_OK || seg_open(&g, &h, run_ticks, stage, mem_size(MEM_SEG_STAGE)) != 0) {
        snprintf(note, n, "\"%s\" would not open: %s", what, jf_err_text(e));
        link_down();
        return 1;
    }

    t0 = platform_clock_us();
    r  = seg_fetch(&g, h.first_seg);
    if (r == SEG_ARRIVED) r = seg_fetch(&g, -1);
    ms = (platform_clock_us() - t0) / 1000u;

    seg_close(&g);
    link_down();

    if (r != SEG_ARRIVED || ms > START_BOUND_MS) {
        snprintf(note, n, "the opening of \"%s\" at segment %d: %s after %u ms", what, h.first_seg, seg_result_text(r), ms);
        return 1;
    }
    snprintf(note, n, "\"%s\" first segment and init in %u ms at segment %d", what, ms, h.first_seg);
    return 0;
}

/* Segments already encoded are fetched again, so what's timed is the link and
   not the encoder's pace. */
#define CLOCK_SEGMENTS 20

/* 540 ms for an 88 kB segment is either a slow link or per-request overhead,
   and those want opposite fixes: a bigger read, or fewer connections. */
static unsigned g_ttfb_us, g_body_us;

static unsigned pull(segments *g, int first, unsigned long long *bytes) {
    unsigned t0    = platform_clock_us(), spent;
    unsigned ttfb0 = g->ttfb_ms, body0 = g->body_ms;
    int      i;

    *bytes = 0;
    for (i = 0; i < CLOCK_SEGMENTS; i++) {
        if (seg_fetch(g, first + i) != SEG_ARRIVED) return 0;
        *bytes += g->len;
    }
    g_ttfb_us = (g->ttfb_ms - ttfb0) * 1000u;
    g_body_us = (g->body_ms - body0) * 1000u;
    spent = platform_clock_us() - t0;
    return spent ? spent : 1;
}

static int t_the_link_is_faster_at_333(char *note, unsigned n) {
    jf_hls             h;
    segments           g;
    char               id[JF_ID_LEN], what[96];
    unsigned long long run_ticks = 0, bytes = 0;
    unsigned char     *stage;
    unsigned           us222 = 0, us333 = 0, kbit222 = 0, kbit333 = 0;
    int                was, got222, got333;

    /* With one clock, what the development machine pulls over Ethernet is the
       server's ceiling: whether the console's 4.2 Mbit/s is its radio or the
       other end. */
    was = platform_cpu_mhz();
    if (!a_film(id, sizeof(id), &run_ticks, what, sizeof(what))) {
        snprintf(note, n, "%s", what);
        link_down();
        return -1;
    }
    stage = (unsigned char *)mem_reserve(MEM_SEG_STAGE);
    if (!stage || jf_hls_open(&g_rb, id, BENCH_AT, BENCH_KBPS * 1000u, -1, -1, &h, g_session, sizeof(g_session)) != JF_OK ||
        seg_open(&g, &h, run_ticks, stage, mem_size(MEM_SEG_STAGE)) != 0) {
        snprintf(note, n, "\"%s\" would not open", what);
        link_down();
        return 1;
    }

    /* Thrown away: this pass only makes the segments exist. */
    if (!pull(&g, h.first_seg, &bytes)) {
        snprintf(note, n, "the transcode would not produce %d segments", CLOCK_SEGMENTS);
        seg_close(&g);
        link_down();
        return 1;
    }

    got222 = platform_set_cpu_mhz(222);
    us222  = pull(&g, h.first_seg, &bytes);
    if (us222) kbit222 = (unsigned)(bytes * 8000ull / us222);
    log_printf("clock: at %d MHz -- first byte %u us, body %u us, over %d segments", got222, g_ttfb_us, g_body_us, CLOCK_SEGMENTS);

    /* The radio goes down before the clock goes up: scePowerSetClockFrequency
     * returns success and changes nothing once sceNetInit has run -- measured
     * here repeatedly, in both call forms, while the same call at application
     * startup works every time. */
    if (was <= 0) {
        log_printf("clock: %llu bytes in %u us -- %u kbit/s (one clock)", bytes, us222, kbit222);
        log_printf("clock: first byte %u us, body %u us, over %d segments", g_ttfb_us, g_body_us, CLOCK_SEGMENTS);
        seg_close(&g);
        link_down();
        snprintf(note, n, "%u kbit/s end to end, %u kbit/s body, over %d segments", kbit222,
                 g_body_us ? (unsigned)(bytes * 8000ull / g_body_us) : 0u, CLOCK_SEGMENTS);
        return 0;
    }

    /* Not link_down(): the transcode has to outlive the radio coming back. */
    net_disconnect();
    g_up = 0;
    net_stop();
    platform_sleep_us(300000);
    got333 = platform_set_cpu_mhz(333);
    {
        char ip[24];

        if (net_start() != 0 || net_connect(g_profile, ip, sizeof(ip)) != 0) {
            snprintf(note, n, "the radio would not come back at %d MHz", got333);
            seg_close(&g);
            return 1;
        }
        g_up = 1;
    }
    us333 = pull(&g, h.first_seg, &bytes);
    if (us333) kbit333 = (unsigned)(bytes * 8000ull / us333);
    log_printf("clock: at %d MHz -- first byte %u us, body %u us, over %d segments", got333, g_ttfb_us, g_body_us, CLOCK_SEGMENTS);

    (void)platform_set_cpu_mhz(was);
    seg_close(&g);
    link_down();

    log_printf("clock: %llu bytes at %d MHz in %u us -- %u kbit/s", bytes, got222, us222, kbit222);
    log_printf("clock: %llu bytes at %d MHz in %u us -- %u kbit/s", bytes, got333, us333, kbit333);

    if (got333 != 333) {
        snprintf(note, n, "333 MHz was asked for and the clock stayed at %d -- %u kbit/s is all this machine will say", got333, kbit222);
        return 1;
    }
    if (!kbit222 || !kbit333) {
        snprintf(note, n, "no throughput measured at one of the clocks");
        return 1;
    }
    snprintf(note, n, "%u kbit/s at 222, %u at 333 -- %+d%% at 333", kbit222, kbit333, (int)(kbit333 * 100u / kbit222) - 100);
    return 0;
}

/* The console numbers sockets upward without reusing them: a connect that
   waited in select() had its 54th refused, socket 64 being past fd_set. A film
   opens one a segment. */
#define SOCKETS 100

static int t_a_hundred_connections_all_connect(char *note, unsigned n) {
    char      host[JF_HOST_LEN], err[96];
    jf_conn   c;
    http_sock s, most = 0;
    int       port = 0, i;

    if (!link_up(&c, note, n)) {
        link_down();
        return -1;
    }
    jf_address(host, sizeof(host), &port);
    for (i = 0; i < SOCKETS; i++) {
        s = http_sock_open(host, port, 0, err, sizeof(err));
        if (s == HTTP_SOCK_NONE) break;
        if (s > most) most = s;
        http_sock_close(s);
    }
    link_down();

    if (i < SOCKETS) {
        snprintf(note, n, "connection %d of %d: %s (highest socket so far %ld)", i + 1, SOCKETS, err, most);
        return 1;
    }
    snprintf(note, n, "%d connections, highest socket %ld", SOCKETS, most);
    return 0;
}

void test_link_register(void) {
    selftest_add("link", "every stage has its number", t_every_stage_has_its_number);
    selftest_add("start", "a film far in starts in seconds", t_a_film_far_in_starts_in_seconds);
    selftest_add("clock", "the link is faster at 333", t_the_link_is_faster_at_333);
    selftest_add("sockets", "a hundred connections all connect", t_a_hundred_connections_all_connect);
}

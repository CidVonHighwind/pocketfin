/* See model/posters.h. Both faults are silent: without the pack every picture
 * is fetched over 802.11b on every launch and after every eviction, and with a
 * plain queue a flick through a listing fetches every row it flew past before
 * the row it stopped on.
 *
 * The pack is the checks' own file, so none passes on a picture an earlier
 * check put there. */

#include "model/posters.h"

#include "port/platform.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

static char g_path[160];

static const char *pack_path(void) {
    if (!g_path[0]) snprintf(g_path, sizeof(g_path), "%spocketfin-art-check.dat", platform_data_dir());
    return g_path;
}

/* Counts, and can refuse: the point of the pack is that a second run does not
   call the source. */
static int      g_calls;
static int      g_refuse;
static unsigned g_delay_us;

static int counting_source(const char *id, int out_w, int out_h, unsigned short *dst, int dst_w, int dst_h, int *got_w, int *got_h) {
    unsigned tint = 0;
    int      x, y, w, h;

    g_calls++;
    if (g_delay_us) platform_sleep_us(g_delay_us);
    if (g_refuse) return -1;

    while (id && *id) tint = tint * 31u + (unsigned char)*id++;
    w = out_w > dst_w ? dst_w : out_w;
    h = out_h > dst_h ? dst_h : out_h;
    /* A pack that wrote at the wrong stride would still round-trip a flat
       colour. */
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) dst[y * dst_w + x] = (unsigned short)(0x1000u + ((tint + (unsigned)(x * 7 + y * 3)) & 0x0FFFu));
    if (got_w) *got_w = w;
    if (got_h) *got_h = h;
    return 0;
}

static const unsigned short *wait_for(const char *id, int w, int h, unsigned bound_us) {
    unsigned waited = 0;

    for (;;) {
        const unsigned short *px = poster_get(id, w, h, 0, 0, 0, 0, 0);

        if (px) return px;
        if (waited >= bound_us) return 0;
        platform_sleep_us(2000);
        waited += 2000;
        poster_frame();
    }
}

static int fresh(char *note, unsigned n, unsigned cap) {
    posters_stop();
    remove(pack_path());
    g_calls    = 0;
    g_refuse   = 0;
    g_delay_us = 0;
    if (posters_start(counting_source, pack_path(), cap) != 0) {
        snprintf(note, n, "the store would not start");
        return -1;
    }
    return 0;
}

/* The source refuses after the restart, so only the file can supply it. */
static int t_a_picture_survives_a_restart(char *note, unsigned n) {
    const unsigned short *px;
    unsigned short        first[28 * 40];
    unsigned              bytes   = 0;
    int                   records = 0, y, calls_cold, gw = 0, gh = 0, tw = 0;
    int                   th = 0;

    if (fresh(note, n, 0) != 0) return -1;

    if (!wait_for("cold", 28, 40, 2000000u)) {
        snprintf(note, n, "the picture never arrived from the source");
        return 1;
    }
    calls_cold = g_calls;
    px         = poster_get("cold", 28, 40, &tw, &th, &gw, &gh, 0);
    if (!px) {
        snprintf(note, n, "it arrived and then vanished");
        return 1;
    }
    for (y = 0; y < gh; y++) memcpy(first + (size_t)y * 28u, px + (size_t)y * (size_t)tw, (size_t)gw * sizeof(*px));

    posters_cache_stats(&bytes, &records);
    if (records != 1 || bytes != 68u + 28u * 40u * 2u) {
        snprintf(note, n, "the pack holds %d records in %u bytes, wanted 1 in %u", records, bytes, 68u + 28u * 40u * 2u);
        return 1;
    }

    g_calls  = 0;
    g_refuse = 1;
    if (posters_start(counting_source, pack_path(), 0) != 0) {
        snprintf(note, n, "the store would not start a second time");
        return -1;
    }

    px = wait_for("cold", 28, 40, 2000000u);
    if (!px) {
        snprintf(note, n, "cold start cost %d source call(s); after a restart the picture did not come back at all", calls_cold);
        return 1;
    }
    if (g_calls != 0) {
        snprintf(note, n, "the source was called %d time(s) for a picture the pack already held", g_calls);
        return 1;
    }
    px = poster_get("cold", 28, 40, &tw, &th, &gw, &gh, 0);
    if (!px || gw != 28 || gh != 40) {
        snprintf(note, n, "it came back %dx%d, not 28x40", gw, gh);
        return 1;
    }
    for (y = 0; y < gh; y++)
        if (memcmp(first + (size_t)y * 28u, px + (size_t)y * (size_t)tw, (size_t)gw * sizeof(*px)) != 0) {
            snprintf(note, n, "row %d came back different from what was stored", y);
            return 1;
        }

    posters_stop();
    remove(pack_path());
    snprintf(note, n, "1 source call cold, 0 after a restart, 28x40 identical in %u bytes", bytes);
    return 0;
}

/* Read up to the damage and no further. */
static int t_a_corrupt_pack_serves_nothing_bad(char *note, unsigned n) {
    unsigned bytes = 0, before = 0;
    int      records = 0, kept = 0;
    FILE    *f;

    if (fresh(note, n, 0) != 0) return -1;
    if (!wait_for("good", 28, 40, 2000000u)) {
        snprintf(note, n, "the first picture never arrived");
        return 1;
    }
    if (!wait_for("after", 28, 40, 2000000u)) {
        snprintf(note, n, "the second picture never arrived");
        return 1;
    }
    posters_cache_stats(&before, &kept);
    posters_stop();

    /* Over the SECOND record's header, leaving the first whole. */
    f = fopen(pack_path(), "r+b");
    if (!f) {
        snprintf(note, n, "the pack would not open");
        return -1;
    }
    if (fseek(f, (long)(68u + 28u * 40u * 2u), SEEK_SET) != 0) {
        fclose(f);
        snprintf(note, n, "could not seek to the second record");
        return -1;
    }
    {
        unsigned char junk[96];

        memset(junk, 0xA5, sizeof(junk));
        (void)fwrite(junk, 1, sizeof(junk), f);
    }
    fclose(f);

    g_calls  = 0;
    g_refuse = 1; /* nothing may come from the source */
    if (posters_start(counting_source, pack_path(), 0) != 0) {
        snprintf(note, n, "the store would not start on a damaged pack");
        return -1;
    }
    posters_cache_stats(&bytes, &records);
    if (records != 1 || bytes != 68u + 28u * 40u * 2u) {
        snprintf(note, n, "%d records in %u bytes survived the damage, wanted 1 in %u (%d in %u before)", records, bytes,
                 68u + 28u * 40u * 2u, kept, before);
        return 1;
    }

    if (!wait_for("good", 28, 40, 400000u)) {
        snprintf(note, n, "the intact record before the damage was thrown away with it");
        return 1;
    }
    /* The source is refusing, so a picture here could only be noise off the
       damaged part of the pack. */
    if (wait_for("after", 28, 40, 400000u)) {
        snprintf(note, n, "a record behind the damage was served");
        return 1;
    }

    posters_stop();
    remove(pack_path());
    snprintf(note, n, "%u bytes good of %u, the record in front served, nothing past the damage", bytes, before);
    return 0;
}

/* A cache must not become the reason the card is full. */
static int t_the_cap_is_honoured(char *note, unsigned n) {
    /* Room for two 28x40 records and not a third: 68 + 2,240 is 2,308. */
    const unsigned CAP     = 2u * (68u + 28u * 40u * 2u) + 32u;
    unsigned       bytes   = 0;
    int            records = 0, i;
    char           id[16];

    if (fresh(note, n, CAP) != 0) return -1;

    for (i = 0; i < 6; i++) {
        snprintf(id, sizeof(id), "cap-%d", i);
        if (!wait_for(id, 28, 40, 2000000u)) {
            snprintf(note, n, "picture %d never arrived", i);
            return 1;
        }
    }
    posters_cache_stats(&bytes, &records);
    posters_stop();
    remove(pack_path());

    if (bytes > CAP) {
        snprintf(note, n, "the pack reached %u bytes against a %u cap", bytes, CAP);
        return 1;
    }
    if (records != 2) {
        snprintf(note, n, "%d records in %u bytes under a %u cap, wanted 2", records, bytes, CAP);
        return 1;
    }
    snprintf(note, n, "6 pictures offered, %d kept in %u bytes of a %u cap", records, bytes, CAP);
    return 0;
}

/* Settings clears the pack from the drawing thread; the worker does it. */
static int t_clearing_empties_the_pack(char *note, unsigned n) {
    unsigned bytes   = 1, waited;
    int      records = 1;

    if (fresh(note, n, 0) != 0) return -1;
    if (!wait_for("gone", 28, 40, 2000000u)) {
        snprintf(note, n, "the picture never arrived");
        return 1;
    }
    posters_cache_clear();
    for (waited = 0; waited < 400000u; waited += 2000) {
        posters_cache_stats(&bytes, &records);
        if (!records) break;
        platform_sleep_us(2000);
    }
    posters_stop();
    remove(pack_path());

    if (records != 0 || bytes != 0) {
        snprintf(note, n, "%d records in %u bytes after clearing", records, bytes);
        return 1;
    }
    snprintf(note, n, "empty within %u ms of asking", waited / 1000u);
    return 0;
}

/* A scroll asks last for the row it has just brought into view, the one the
   viewer is looking at; a plain queue serves it last. */
static int g_order[8];
static int g_order_n;

static int recording_source(const char *id, int out_w, int out_h, unsigned short *dst, int dst_w, int dst_h, int *got_w, int *got_h) {
    if (g_order_n < 8) {
        int v = 0;

        while (*id && (*id < '0' || *id > '9')) id++;
        while (*id >= '0' && *id <= '9') v = v * 10 + (*id++ - '0');
        g_order[g_order_n++] = v;
    }
    platform_sleep_us(30000); /* long enough to queue behind it */
    return counting_source("x", out_w, out_h, dst, dst_w, dst_h, got_w, got_h);
}

static int t_the_newest_want_is_served_first(char *note, unsigned n) {
    int  i;
    char id[16];

    g_order_n = 0;
    g_calls   = 0;
    g_refuse  = 0;
    if (posters_start(recording_source, 0, 0) != 0) {
        snprintf(note, n, "the store would not start");
        return -1;
    }

    /* The worker takes the first straight away; the order of the other three
       is what this measures. All four stay asked for, so nothing expires and
       only which was asked last decides the order. */
    for (i = 0; i < 120 && g_order_n < 4; i++) {
        int k;

        for (k = 0; k < 4; k++) {
            snprintf(id, sizeof(id), "want-%d", k);
            (void)poster_get(id, 28, 40, 0, 0, 0, 0, 0);
        }
        poster_frame();
        platform_sleep_us(4000);
    }
    posters_stop();

    if (g_order_n < 4) {
        snprintf(note, n, "only %d of 4 were served", g_order_n);
        return 1;
    }
    /* The first out is whichever the worker picked up while the table was
       still filling. */
    for (i = 2; i < 4; i++)
        if (g_order[i] > g_order[i - 1]) {
            snprintf(note, n, "served %d %d %d %d -- a want was overtaken by an older one", g_order[0], g_order[1], g_order[2], g_order[3]);
            return 1;
        }
    snprintf(note, n, "served %d %d %d %d: newest first", g_order[0], g_order[1], g_order[2], g_order[3]);
    return 0;
}

/* A fast scroll costs the window it stopped on, not every window it crossed.
   With 300 ms fetches, 750 ms has room for the one already started and the one
   still asked for; a third means a row scrolled past was served. */
#define STALE_JOB_US 300000u

static int t_a_want_not_re_asked_is_dropped(char *note, unsigned n) {
    int      calls, i;
    unsigned t0;

    g_calls    = 0;
    g_refuse   = 0;
    g_delay_us = STALE_JOB_US;
    if (posters_start(counting_source, 0, 0) != 0) {
        snprintf(note, n, "the store would not start");
        return -1;
    }

    (void)poster_get("held", 28, 40, 0, 0, 0, 0, 0);
    for (i = 0; i < 6; i++) {
        char id[16];

        snprintf(id, sizeof(id), "flew-past-%d", i);
        (void)poster_get(id, 28, 40, 0, 0, 0, 0, 0);
    }
    /* KEEP_FRAMES is 2: three frames puts the six past it. */
    for (i = 0; i < 3; i++) {
        poster_frame();
        (void)poster_get("held", 28, 40, 0, 0, 0, 0, 0);
    }

    /* A clock, not a count of sleeps: Sleep(2) is a 15.6 ms tick on Windows. */
    t0 = platform_clock_us();
    while (platform_clock_us() - t0 < 2u * STALE_JOB_US + STALE_JOB_US / 2u) platform_sleep_us(2000);
    calls = g_calls;

    posters_stop();
    g_delay_us = 0;

    if (calls > 2) {
        snprintf(note, n, "7 wants, 6 unasked for 3 frames, and %d fetched in %u ms of %u ms fetches", calls,
                 (2u * STALE_JOB_US + STALE_JOB_US / 2u) / 1000u, STALE_JOB_US / 1000u);
        return 1;
    }
    snprintf(note, n, "7 wants, %d fetched: the six scrolled past were left alone", calls);
    return 0;
}

void test_poster_cache_register(void) {
    selftest_add("poster-cache", "a picture survives a restart", t_a_picture_survives_a_restart);
    selftest_add("poster-cache", "a corrupt pack serves nothing bad", t_a_corrupt_pack_serves_nothing_bad);
    selftest_add("poster-cache", "the cap is honoured", t_the_cap_is_honoured);
    selftest_add("poster-cache", "clearing empties the pack", t_clearing_empties_the_pack);
    selftest_add("poster-cache", "the newest want is served first", t_the_newest_want_is_served_first);
    selftest_add("poster-cache", "a want not re-asked is dropped", t_a_want_not_re_asked_is_dropped);
}

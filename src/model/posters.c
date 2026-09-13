#include "model/posters.h"

#include "base/align.h"
#include "base/log.h"
#include "jelly/api.h"
#include "port/platform.h"

#include <stdio.h>
#include <string.h>

/* The engine blits power-of-two textures only, and one class big enough for
   the detail poster would spend 128 kB on a thumbnail. */
#define CLASS_N 4

static const struct {
    short w, h, count;
} CLASS[CLASS_N] = {
    {32, 64, 16},  /* listing thumbnails, 28x40  */
    {128, 64, 24}, /* rail cards, 80x45          */
    {128, 128, 8}, /* library tiles, 120x68      */
    {256, 256, 2}  /* the detail poster, 200x212 */
};

#define SLOT_N (16 + 24 + 8 + 2)

static POCKETFIN_ALIGN16 unsigned short g_px0[16][32 * 64];
static POCKETFIN_ALIGN16 unsigned short g_px1[24][128 * 64];
static POCKETFIN_ALIGN16 unsigned short g_px2[8][128 * 128];
static POCKETFIN_ALIGN16 unsigned short g_px3[2][256 * 256];

/* A want not renewed for this many frames is neither served nor kept. */
#define KEEP_FRAMES 2u

#define IDLE_US 2000u

typedef enum { FREE = 0, WANTED, RUNNING, READY, NONE } slot_state;

typedef struct {
    char            id[JF_ID_LEN];
    short           w, h;
    short           tex_w, tex_h;
    unsigned short *px;
    int             got_w, got_h;
    slot_state      state;
    unsigned        asked, seq;
} slot;

static slot           g_slot[SLOT_N];
static poster_source  g_source;
static platform_lock *g_lock;
static volatile int   g_run, g_left, g_clear_pack;
static unsigned       g_frame, g_seq;
static unsigned       g_from_pack, g_from_source, g_pack_us, g_source_us;

/* A record: magic, width, height, byte count, key, then the rows at their own
   width. Little-endian by hand, so either machine reads the other's pack. */
#define PACK_MAGIC 0x4B434150u
#define KEY_LEN    56
#define HDR_LEN    (12 + KEY_LEN)
#define PACK_CAP   (32u * 1024u * 1024u)

/* Without an index a lookup walked the pack a header at a time: 89 ms a
   picture against 62 ms to fetch it over 802.11b. The pack never holds more
   records than the index. */
#define PACK_RECORDS 2048

/* Once the worker runs, only it touches these. */
static FILE    *g_pack;
static char     g_pack_path[160];
static unsigned g_pack_end, g_pack_cap;
static int      g_pack_n;
static struct {
    unsigned hash, at;
} g_index[PACK_RECORDS];

static unsigned rd32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static void wr32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static unsigned key_hash(const char *k) {
    unsigned h = 2166136261u;

    while (*k) {
        h ^= (unsigned char)*k++;
        h *= 16777619u;
    }
    return h;
}

static void key_of(char *out, const char *id, int w, int h) { snprintf(out, KEY_LEN, "%.*s_%dx%d", JF_ID_LEN - 1, id, w, h); }

/* Leaves the file on the record's pixels. 0 for anything that is not a whole
   record inside `len`. */
static int read_hdr(unsigned at, unsigned len, char *key, unsigned *w, unsigned *h) {
    unsigned char hdr[HDR_LEN];
    unsigned      bytes;

    if (fseek(g_pack, (long)at, SEEK_SET) != 0 || fread(hdr, 1, sizeof(hdr), g_pack) != sizeof(hdr)) return 0;
    *w    = (unsigned)hdr[4] | ((unsigned)hdr[5] << 8);
    *h    = (unsigned)hdr[6] | ((unsigned)hdr[7] << 8);
    bytes = rd32(hdr + 8);
    if (rd32(hdr) != PACK_MAGIC || !*w || !*h || bytes != *w * *h * 2u || hdr[HDR_LEN - 1] != 0 || at + HDR_LEN + bytes > len) return 0;
    memcpy(key, hdr + 12, KEY_LEN);
    return 1;
}

/* Stops at the first bad record, a tail half-written when the battery came
   out, and the next append overwrites it. */
static void pack_open(const char *path, unsigned cap) {
    char     key[KEY_LEN];
    unsigned at = 0, len, w, h;
    long     end;

    snprintf(g_pack_path, sizeof(g_pack_path), "%s", path);
    g_pack_cap = cap ? cap : PACK_CAP;
    g_pack_end = 0;
    g_pack_n   = 0;
    g_pack     = fopen(g_pack_path, "r+b");
    if (!g_pack) g_pack = fopen(g_pack_path, "w+b");
    if (!g_pack) {
        log_printf("art: no pack at %s", g_pack_path);
        return;
    }
    if (fseek(g_pack, 0, SEEK_END) == 0 && (end = ftell(g_pack)) > 0) {
        len = (unsigned)end;
        while (g_pack_n < PACK_RECORDS && read_hdr(at, len, key, &w, &h)) {
            g_index[g_pack_n].hash = key_hash(key);
            g_index[g_pack_n].at   = at;
            g_pack_n++;
            at += HDR_LEN + w * h * 2u;
        }
        g_pack_end = at;
        if (at != len) log_printf("art: pack %u KB good of %u KB, %d pictures", at / 1024u, len / 1024u, g_pack_n);
    }
    log_printf("art: pack %s, %d pictures in %u KB of %u KB", g_pack_path, g_pack_n, g_pack_end / 1024u, g_pack_cap / 1024u);
}

static void pack_close(void) {
    if (g_pack) fclose(g_pack);
    g_pack     = 0;
    g_pack_end = 0;
    g_pack_n   = 0;
}

/* A hash collision costs a seek, never a wrong picture. */
static int pack_find(const char *key, unsigned *w, unsigned *h) {
    char     found[KEY_LEN];
    unsigned want = key_hash(key);
    int      i;

    if (!g_pack) return 0;
    for (i = 0; i < g_pack_n; i++)
        if (g_index[i].hash == want && read_hdr(g_index[i].at, g_pack_end, found, w, h) && strcmp(found, key) == 0) return 1;
    return 0;
}

static int pack_get(const char *key, unsigned short *px, int stride, int rows, int *got_w, int *got_h) {
    unsigned w, h, y = 0;

    if (!pack_find(key, &w, &h) || (int)w > stride || (int)h > rows) return 0;
    while (y < h && fread(px + y * (unsigned)stride, 2, w, g_pack) == w) y++;
    if (y < h) {
        log_printf("art: pack record %s is short", key);
        return 0;
    }
    *got_w = (int)w;
    *got_h = (int)h;
    return 1;
}

static void pack_put(const char *key, const unsigned short *px, int stride, int w, int h) {
    unsigned char hdr[HDR_LEN];
    unsigned      bytes = (unsigned)(w * h) * 2u;
    int           y     = 0;

    if (!g_pack || g_pack_n >= PACK_RECORDS || g_pack_end + HDR_LEN + bytes > g_pack_cap) return;
    memset(hdr, 0, sizeof(hdr));
    wr32(hdr, PACK_MAGIC);
    hdr[4] = (unsigned char)w;
    hdr[5] = (unsigned char)(w >> 8);
    hdr[6] = (unsigned char)h;
    hdr[7] = (unsigned char)(h >> 8);
    wr32(hdr + 8, bytes);
    snprintf((char *)hdr + 12, KEY_LEN, "%s", key);
    if (fseek(g_pack, (long)g_pack_end, SEEK_SET) == 0 && fwrite(hdr, 1, sizeof(hdr), g_pack) == sizeof(hdr))
        while (y < h && fwrite(px + y * stride, 2, (size_t)w, g_pack) == (size_t)w) y++;
    fflush(g_pack);
    if (y < h) return;
    g_index[g_pack_n].hash = key_hash(key);
    g_index[g_pack_n].at   = g_pack_end;
    g_pack_n++;
    g_pack_end += HDR_LEN + bytes;
}

static int class_for(int w, int h) {
    int c;

    for (c = 0; c < CLASS_N; c++)
        if (w <= CLASS[c].w && h <= CLASS[c].h) return c;
    return -1;
}

static int first_of(int c) {
    int i, k = 0;

    for (i = 0; i < c; i++) k += CLASS[i].count;
    return k;
}

/* The two below, and next_want(), are called with g_lock held. */
static slot *find(int c, const char *id, int w, int h) {
    int i, k = first_of(c);

    for (i = 0; i < CLASS[c].count; i++) {
        slot *s = &g_slot[k + i];

        if (s->state != FREE && s->w == (short)w && s->h == (short)h && strcmp(s->id, id) == 0) return s;
    }
    return 0;
}

/* Never one being decoded into. */
static slot *claim(int c) {
    int   i, k = first_of(c);
    slot *best = 0;

    for (i = 0; i < CLASS[c].count; i++) {
        slot *s = &g_slot[k + i];

        if (s->state == FREE) return s;
        if (s->state == RUNNING || g_frame - s->asked < KEEP_FRAMES) continue;
        if (!best || s->asked < best->asked) best = s;
    }
    return best;
}

/* Newest first: a scroll asks last for the row it has just brought into view,
   the one the viewer is looking at. */
static slot *next_want(void) {
    slot *best = 0;
    int   i;

    for (i = 0; i < SLOT_N; i++) {
        slot *s = &g_slot[i];

        if (s->state == WANTED && g_frame - s->asked <= KEEP_FRAMES && (!best || s->seq > best->seq)) best = s;
    }
    if (best) best->state = RUNNING;
    return best;
}

static int worker(void *arg) {
    (void)arg;
    while (g_run) {
        char            id[JF_ID_LEN] = "", key[KEY_LEN];
        unsigned short *px            = 0;
        int             w = 0, h = 0, stride = 0, rows = 0, got_w = 0, got_h = 0, rc;
        unsigned        t0;
        slot           *s;

        if (g_clear_pack) {
            g_clear_pack = 0;
            if (g_pack) {
                pack_close();
                remove(g_pack_path);
                pack_open(g_pack_path, g_pack_cap);
            }
        }

        platform_lock_take(g_lock);
        s = next_want();
        if (s) {
            memcpy(id, s->id, sizeof(id));
            w      = s->w;
            h      = s->h;
            px     = s->px;
            stride = s->tex_w;
            rows   = s->tex_h;
        }
        platform_lock_give(g_lock);
        if (!s) {
            platform_sleep_us(IDLE_US);
            continue;
        }

        key_of(key, id, w, h);
        t0 = platform_clock_us();
        if (pack_get(key, px, stride, rows, &got_w, &got_h)) {
            rc = 0;
            g_from_pack++;
            g_pack_us += platform_clock_us() - t0;
        } else {
            rc = g_source(id, w, h, px, stride, rows, &got_w, &got_h);
            g_source_us += platform_clock_us() - t0;
            g_from_source++;
            if (rc == 0 && got_w > 0 && got_h > 0) pack_put(key, px, stride, got_w, got_h);
        }

        platform_lock_take(g_lock);
        s->got_w = got_w;
        s->got_h = got_h;
        /* Freed on a failure worth another go, so the next ask fetches again: a
           server still starting answers 503 for a few seconds. */
        s->state = (rc == 0 && got_w > 0) ? READY : rc == JF_POSTER_NONE ? NONE : FREE;
        platform_lock_give(g_lock);
    }
    g_left = 1;
    return 0;
}

int posters_start(poster_source source, const char *pack_path, unsigned pack_cap) {
    int i, k = 0, c;

    posters_stop();
    if (!source) return -1;
    if (!g_lock) g_lock = platform_lock_new("posters");
    if (!g_lock) return -1;

    memset(g_slot, 0, sizeof(g_slot));
    for (c = 0; c < CLASS_N; c++)
        for (i = 0; i < CLASS[c].count; i++, k++) {
            g_slot[k].tex_w = CLASS[c].w;
            g_slot[k].tex_h = CLASS[c].h;
            g_slot[k].px    = c == 0 ? g_px0[i] : c == 1 ? g_px1[i] : c == 2 ? g_px2[i] : g_px3[i];
        }
    g_frame     = 1;
    g_seq       = 0;
    g_from_pack = g_from_source = g_pack_us = g_source_us = 0;
    g_clear_pack                                          = 0;
    g_source                                              = source;
    if (pack_path) pack_open(pack_path, pack_cap);

    g_left = 0;
    g_run  = 1;
    if (platform_thread_start("posters", worker, 0) != 0) {
        g_run = 0;
        log_line("posters: the worker would not start");
        return -1;
    }
    return 0;
}

void posters_stop(void) {
    unsigned t0 = platform_clock_us();

    if (g_run) {
        g_run = 0;
        /* The worker may be writing into a slot. */
        while (!g_left && platform_clock_us() - t0 < 2000000u) platform_sleep_us(2000);
        if (!g_left) log_line("posters: THE WORKER DID NOT LEAVE");
    }
    pack_close();
}

void poster_frame(void) {
    static unsigned said_us, said_pack, said_source;
    unsigned        now = platform_clock_us();

    g_frame++;
    if (now - said_us < 1000000u) return;
    said_us = now;
    if (g_from_pack == said_pack && g_from_source == said_source) return;
    said_pack   = g_from_pack;
    said_source = g_from_source;
    log_printf("art: %u from the pack in %u us, %u over the link in %u us", g_from_pack, g_pack_us, g_from_source, g_source_us);
}

const unsigned short *poster_get(const char *id, int w, int h, int *tex_w, int *tex_h, int *got_w, int *got_h, unsigned *waiting) {
    const unsigned short *px = 0;
    int                   c  = class_for(w, h);
    slot                 *s;

    if (tex_w) *tex_w = 0;
    if (tex_h) *tex_h = 0;
    if (got_w) *got_w = 0;
    if (got_h) *got_h = 0;
    if (waiting) *waiting = 0;
    if (!id || !id[0] || c < 0) return 0;

    platform_lock_take(g_lock);
    s = find(c, id, w, h);
    if (!s && g_run && (s = claim(c)) != 0) {
        memset(s->px, 0, (unsigned)(s->tex_w * s->tex_h) * sizeof(*s->px));
        snprintf(s->id, sizeof(s->id), "%s", id);
        s->w     = (short)w;
        s->h     = (short)h;
        s->got_w = s->got_h = 0;
        s->state            = WANTED;
        s->seq              = ++g_seq;
    }
    if (s) s->asked = g_frame;

    if (s && s->state == READY) {
        px = s->px;
        if (tex_w) *tex_w = s->tex_w;
        if (tex_h) *tex_h = s->tex_h;
        if (got_w) *got_w = s->got_w;
        if (got_h) *got_h = s->got_h;
    } else if (waiting) {
        /* No slot free this frame is still coming: it is asked again next. */
        *waiting = s ? s->state != NONE : g_run != 0;
    }
    platform_lock_give(g_lock);
    return px;
}

void posters_clear(void) {
    int i;

    platform_lock_take(g_lock);
    for (i = 0; i < SLOT_N; i++)
        if (g_slot[i].state != RUNNING) g_slot[i].state = FREE;
    platform_lock_give(g_lock);
}

void posters_cache_clear(void) {
    posters_clear();
    g_clear_pack = 1;
}

void posters_cache_stats(unsigned *bytes, int *records) {
    if (bytes) *bytes = g_pack ? g_pack_end : 0u;
    if (records) *records = g_pack ? g_pack_n : 0;
}

void posters_times(unsigned *from_pack, unsigned *pack_us, unsigned *source_us) {
    if (from_pack) *from_pack = g_from_pack;
    if (pack_us) *pack_us = g_pack_us;
    if (source_us) *source_us = g_source_us;
}

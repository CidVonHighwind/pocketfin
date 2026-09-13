/* See link.h. The lock guards only the shared state and is never held across a
 * file operation. */

#include "io/link.h"

#include "port/platform.h"

#include <stdio.h>
#include <string.h>

#define LEAF_MAX    32
#define PATH_MAX_   128
#define QUEUE_MAX   2
#define QUEUE_BYTES 8192
#define PUT_MAX     4
#define READ_MAX    65536
#define READ_US     1000000u

/* state.txt holds a screen's whole account of itself; at 256 bytes it was cut
   after the fourth row. */
#define PUT_TEXT_MAX 2048

typedef struct {
    char     leaf[LEAF_MAX];
    char     buf[QUEUE_BYTES];
    unsigned used;
} queue;

typedef struct {
    char leaf[LEAF_MAX];
    char text[PUT_TEXT_MAX];
    int  pending;
} put_slot;

static char           g_dir[80];
static int            g_up;
static platform_lock *g_lock;
static volatile int   g_running;
static volatile int   g_ended;

static queue    g_queue[QUEUE_MAX];
static put_slot g_put[PUT_MAX];
static char     g_scratch[QUEUE_BYTES];

/* These files outlive a load and the sequence counter does not, so the first
   read is a baseline, never a change. */
static struct {
    char leaf[LEAF_MAX];
    char line[HOSTFS_LINE_MAX];
    int  fresh;
    int  seen;
} g_watch;

/* Read into the link's own buffer and copied out under the lock, so a caller
   that gave up never has its buffer written after it returned. `gen` tells a
   late answer from the current request's. */
static struct {
    char     leaf[LEAF_MAX];
    unsigned cap, got, gen;
    int      asked, done, abandoned;
} g_read;
static char g_read_buf[READ_MAX];

static volatile int g_paused;
static volatile int g_write_ok = -1; /* -1: not tried since the link came back */

static void path_of(char *out, unsigned n, const char *leaf) {
    snprintf(out, n, "%.*s%.*s", (int)sizeof(g_dir) - 1, g_dir, LEAF_MAX - 1, leaf);
}

static void do_appends(void) {
    int i;

    for (i = 0; i < QUEUE_MAX; i++) {
        char     p[PATH_MAX_], leaf[LEAF_MAX];
        unsigned n;
        FILE    *f;

        platform_lock_take(g_lock);
        n = g_queue[i].used;
        if (n) {
            memcpy(g_scratch, g_queue[i].buf, n);
            memcpy(leaf, g_queue[i].leaf, sizeof(leaf));
        }
        platform_lock_give(g_lock);
        if (!n) continue;

        path_of(p, sizeof(p), leaf);
        /* Kept, not dropped: the next pass tries again. */
        if ((f = fopen(p, "ab")) == NULL) {
            g_write_ok = 0;
            continue;
        }
        fwrite(g_scratch, 1, n, f);
        fclose(f);
        g_write_ok = 1;

        platform_lock_take(g_lock);
        if (g_queue[i].used > n) memmove(g_queue[i].buf, g_queue[i].buf + n, g_queue[i].used - n);
        g_queue[i].used -= n;
        platform_lock_give(g_lock);
    }
}

static void do_puts(void) {
    int i;

    for (i = 0; i < PUT_MAX; i++) {
        char  p[PATH_MAX_], text[PUT_TEXT_MAX], leaf[LEAF_MAX];
        FILE *f;

        platform_lock_take(g_lock);
        if (!g_put[i].pending) {
            platform_lock_give(g_lock);
            continue;
        }
        memcpy(leaf, g_put[i].leaf, sizeof(leaf));
        memcpy(text, g_put[i].text, sizeof(text));
        g_put[i].pending = 0;
        platform_lock_give(g_lock);

        path_of(p, sizeof(p), leaf);
        f = fopen(p, "wb");
        if (!f) {
            g_write_ok = 0;
            continue;
        }
        fwrite(text, 1, strlen(text), f);
        fclose(f);
        g_write_ok = 1;
    }
}

static void do_watch(void) {
    char  p[PATH_MAX_], line[HOSTFS_LINE_MAX];
    FILE *f;

    if (!g_watch.leaf[0]) return;
    path_of(p, sizeof(p), g_watch.leaf);
    f = fopen(p, "rb");
    if (!f) {
        /* Absent is a baseline too, or the first command to arrive is mistaken
         * for what was already there and swallowed. */
        g_watch.seen = 1;
        return;
    }
    line[0] = 0;
    if (!fgets(line, sizeof(line), f)) line[0] = 0;
    fclose(f);
    if (!line[0]) return;

    platform_lock_take(g_lock);
    if (strcmp(line, g_watch.line) != 0) {
        memcpy(g_watch.line, line, sizeof(line));
        g_watch.fresh = g_watch.seen;
    }
    g_watch.seen = 1;
    platform_lock_give(g_lock);
}

static void do_read(void) {
    char     p[PATH_MAX_];
    FILE    *f;
    unsigned n = 0, gen, cap;

    platform_lock_take(g_lock);
    if (!g_read.asked || g_read.done || g_read.abandoned) {
        platform_lock_give(g_lock);
        return;
    }
    gen = g_read.gen;
    cap = g_read.cap;
    path_of(p, sizeof(p), g_read.leaf);
    platform_lock_give(g_lock);

    f = fopen(p, "rb");
    if (f) {
        n = (unsigned)fread(g_read_buf, 1, cap - 1, f);
        fclose(f);
    }

    platform_lock_take(g_lock);
    if (g_read.gen == gen) {
        g_read.got  = n;
        g_read.done = 1;
    }
    platform_lock_give(g_lock);
}

static int hostfs_thread(void *arg) {
    (void)arg;
    while (g_running) {
        if (!g_paused) {
            do_appends();
            do_puts();
            do_watch();
            do_read();
        }
        platform_sleep_us(HOSTFS_PASS_US);
    }
    do_appends();
    do_puts();
    g_ended = 1;
    return 0;
}

int hostfs_start(void) {
    const char *dir = platform_hostfs_dir();

    if (g_up) return 0;
    if (!dir || !dir[0]) return -1;
    snprintf(g_dir, sizeof(g_dir), "%s", dir);

    g_lock = platform_lock_new("link");
    if (!g_lock) return -1;

    g_running = 1;
    if (platform_thread_start("link", hostfs_thread, NULL) != 0) {
        g_running = 0;
        return -1;
    }
    g_up = 1;
    return 0;
}

int hostfs_up(void) { return g_up; }

int hostfs_stop(unsigned bound_ms) {
    unsigned t0 = platform_clock_us();

    if (!g_up) return 0;
    g_running = 0;

    while (!g_ended) {
        if ((platform_clock_us() - t0) / 1000u >= bound_ms) return -1;
        platform_sleep_us(2000);
    }
    g_up = 0;
    return 0;
}

unsigned hostfs_append(const char *leaf, const void *bytes, unsigned n) {
    unsigned room;
    int      i, slot = -1;

    if (!g_up || !leaf || !bytes || !n) return 0;

    platform_lock_take(g_lock);
    /* A drained queue is up for grabs, or the first screenshot's name owns a
     * slot for the rest of the run and the second one is refused. */
    for (i = 0; i < QUEUE_MAX; i++) {
        if (!strcmp(g_queue[i].leaf, leaf)) {
            slot = i;
            break;
        }
        if (slot < 0 && !g_queue[i].used) slot = i;
    }
    if (slot < 0) {
        platform_lock_give(g_lock);
        return 0;
    }
    if (strcmp(g_queue[slot].leaf, leaf) != 0) snprintf(g_queue[slot].leaf, sizeof(g_queue[0].leaf), "%s", leaf);

    room = (unsigned)sizeof(g_queue[0].buf) - g_queue[slot].used;
    if (n > room) n = room;
    if (n) {
        memcpy(g_queue[slot].buf + g_queue[slot].used, bytes, n);
        g_queue[slot].used += n;
    }
    platform_lock_give(g_lock);
    return n;
}

int hostfs_put(const char *leaf, const char *text) {
    int i, free_slot = -1;

    if (!g_up || !leaf) return -1;

    platform_lock_take(g_lock);
    /* A slot with nothing pending is up for grabs, so a run touching more names
     * than there are slots refuses whoever asks fifth, not whoever filled
     * them. */
    for (i = 0; i < PUT_MAX; i++) {
        if (!strcmp(g_put[i].leaf, leaf)) {
            free_slot = i;
            break;
        }
        if (free_slot < 0 && !g_put[i].pending) free_slot = i;
    }
    if (free_slot >= 0) {
        snprintf(g_put[free_slot].leaf, sizeof(g_put[0].leaf), "%s", leaf);
        snprintf(g_put[free_slot].text, sizeof(g_put[0].text), "%s", text ? text : "");
        g_put[free_slot].pending = 1;
    }
    platform_lock_give(g_lock);
    return free_slot >= 0 ? 0 : -1;
}

unsigned hostfs_slurp(const char *leaf, char *out, unsigned n) {
    unsigned t0 = platform_clock_us(), mine = 0, got = 0;
    int      done = 0;

    if (!g_up || !leaf || !out || n < 2) return 0;
    out[0] = 0;
    if (n > READ_MAX) n = READ_MAX;

    /* One read at a time: a second waits for the first rather than being
       refused. */
    while (!done && platform_clock_us() - t0 < READ_US) {
        platform_lock_take(g_lock);
        if (!mine && (!g_read.asked || g_read.abandoned)) {
            snprintf(g_read.leaf, sizeof(g_read.leaf), "%s", leaf);
            g_read.cap       = n;
            g_read.done      = 0;
            g_read.abandoned = 0;
            g_read.asked     = 1;
            mine             = ++g_read.gen;
        } else if (mine && g_read.done) {
            got = g_read.got;
            memcpy(out, g_read_buf, got);
            out[got]     = 0;
            g_read.asked = 0;
            done         = 1;
        }
        platform_lock_give(g_lock);
        if (!done) platform_sleep_us(HOSTFS_PASS_US);
    }
    if (!done && mine) {
        platform_lock_take(g_lock);
        if (g_read.gen == mine) g_read.abandoned = 1;
        platform_lock_give(g_lock);
    }
    return got;
}

int hostfs_watch(const char *leaf) {
    if (!g_up || !leaf || g_watch.leaf[0]) return -1;
    platform_lock_take(g_lock);
    snprintf(g_watch.leaf, sizeof(g_watch.leaf), "%s", leaf);
    platform_lock_give(g_lock);
    return 0;
}

int hostfs_line(char *out, unsigned n) {
    int got = 0;

    if (!g_up || !out || !n) return 0;
    platform_lock_take(g_lock);
    if (g_watch.fresh) {
        snprintf(out, n, "%s", g_watch.line);
        g_watch.fresh = 0;
        got           = 1;
    }
    platform_lock_give(g_lock);
    return got;
}

int hostfs_idle(const char *leaf) {
    int i, busy = 0;

    if (!g_up || !leaf) return 1;
    platform_lock_take(g_lock);
    for (i = 0; i < QUEUE_MAX; i++)
        if (g_queue[i].used && !strcmp(g_queue[i].leaf, leaf)) busy = 1;
    for (i = 0; i < PUT_MAX; i++)
        if (g_put[i].pending && !strcmp(g_put[i].leaf, leaf)) busy = 1;
    platform_lock_give(g_lock);
    return !busy;
}

void hostfs_release(void) {
    g_paused   = 1;
    g_write_ok = -1;
}

int hostfs_acquire(void) {
    if (!g_up) return 0;
    g_paused = 0;
    /* Something to write, so verify observes rather than assumes. */
    hostfs_put("alive.txt", "1\n");
    return 0;
}

/* With no cable there is nothing to prove, and waiting on a link that does not
 * exist would leave every card boot stuck restoring for ever. */
int hostfs_verify(void) {
    if (!g_up) return 0;
    return g_write_ok == 1 ? 0 : -1;
}

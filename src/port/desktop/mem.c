/* See port/mem.h.
 *
 * The console's table: src/port/psp/mem.c is where the numbers were measured
 * and the authority if the two disagree. The alignment is honoured so a check
 * about it means the same thing on both. */

#include "port/mem.h"

#include "base/log.h"

#include <stdlib.h>

#ifdef _WIN32
#include <malloc.h>
#define ALIGNED_ALLOC(align, bytes) _aligned_malloc((bytes), (align))
#define ALIGNED_FREE(p)             _aligned_free(p)
#else
#define ALIGNED_ALLOC(align, bytes) aligned_alloc((align), (bytes))
#define ALIGNED_FREE(p)             free(p)
#endif

static const struct {
    const char *name;
    unsigned    bytes;
    unsigned    align;
} kBudget[MEM_N] = {{"decoder", 4u * 1024 * 1024, 4u * 1024 * 1024},
                    {"video ring", 3u * 1024 * 1024, 64},
                    {"audio ring", 384u * 1024, 64},
                    {"segment stage", 1024u * 1024, 64}};

static void *g_block[MEM_N];

unsigned mem_size(mem_id id) { return (id >= 0 && id < MEM_N) ? kBudget[id].bytes : 0u; }

void *mem_reserve(mem_id id) {
    void *p;

    if (id < 0 || id >= MEM_N) return 0;
    if (g_block[id]) return g_block[id];

    p = ALIGNED_ALLOC(kBudget[id].align, kBudget[id].bytes);
    if (!p) {
        log_printf("mem: %s (%u kB) would not be placed", kBudget[id].name, kBudget[id].bytes / 1024u);
        return 0;
    }
    g_block[id] = p;
    return p;
}

int mem_reserve_all(void) {
    int id;

    for (id = 0; id < MEM_N; id++)
        if (!mem_reserve((mem_id)id)) return -1;
    return 0;
}

void mem_release_all(void) {
    int id;

    for (id = 0; id < MEM_N; id++) {
        ALIGNED_FREE(g_block[id]);
        g_block[id] = 0;
    }
}

int mem_budget_ok(void) {
    int ok;

    if (g_block[MEM_DECODER]) return 1;
    ok = mem_reserve_all() == 0;
    mem_release_all();
    return ok;
}

void mem_report(void) {
    int id;

    for (id = 0; id < MEM_N; id++)
        log_printf("mem: %-14s %6u kB %s", kBudget[id].name, kBudget[id].bytes / 1024u, g_block[id] ? "held" : "-");
}

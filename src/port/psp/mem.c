/* See port/mem.h. */

#include "port/mem.h"

#include "base/log.h"

#include <pspkernel.h>
#include <pspsysmem.h>

#include <malloc.h>
#include <stdio.h>

/* Each size is what the console needed on a real film. Raising one is not
   free: at a 2 MB segment stage the decoder's aligned claim no longer fits. */
static const struct {
    const char *name;
    unsigned    bytes;
    unsigned    align;
} kBudget[MEM_N] = {{"decoder", 4u * 1024 * 1024, 4u * 1024 * 1024},
                    /* Ten seconds of real playback, logged: peak 1,120 kB and no dry-outs. */
                    {"video ring", 3u * 1024 * 1024, 64},
                    /* Peak 164 kB across the same run. */
                    {"audio ring", 384u * 1024, 64},
                    /* Segments measure up to 824 kB; a truncated one kills playback. */
                    {"segment stage", 1024u * 1024, 64}};

static void *g_block[MEM_N];

unsigned mem_size(mem_id id) { return (id >= 0 && id < MEM_N) ? kBudget[id].bytes : 0u; }

void *mem_reserve(mem_id id) {
    void *p;

    if (id < 0 || id >= MEM_N) return 0;
    if (g_block[id]) return g_block[id];

    p = memalign(kBudget[id].align, kBudget[id].bytes);
    if (!p) {
        log_printf("mem: %s (%u kB, %u kB aligned) would not be placed -- %u kB free, largest run %u kB", kBudget[id].name,
                   kBudget[id].bytes / 1024u, kBudget[id].align / 1024u, (unsigned)sceKernelTotalFreeMemSize() / 1024u,
                   (unsigned)sceKernelMaxFreeMemSize() / 1024u);
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
        free(g_block[id]);
        g_block[id] = 0;
    }
}

/* "Can be placed", not "is held": claimed and handed straight back. */
int mem_budget_ok(void) {
    int ok;

    if (g_block[MEM_DECODER]) return 1;
    ok = mem_reserve_all() == 0;
    mem_release_all();
    return ok;
}

void mem_report(void) {
    int id;

    log_printf("mem: partition %u kB free, largest run %u kB", (unsigned)sceKernelTotalFreeMemSize() / 1024u,
               (unsigned)sceKernelMaxFreeMemSize() / 1024u);
    for (id = 0; id < MEM_N; id++)
        log_printf("mem:   %-14s %5u kB  %s%s", kBudget[id].name, kBudget[id].bytes / 1024u, g_block[id] ? "held" : "NOT HELD",
                   (g_block[id] && ((unsigned long)g_block[id] & (kBudget[id].align - 1u))) ? " -- MISALIGNED" : "");
}

/* See port/mem.h. The decoder wants 4 MB aligned to 4 MB, and after the
 * browser has been walked for a while the partition may have no such run: the
 * claim fails with a status nobody traces back, and no film starts. The
 * largest free block has measured 1.16 MB by then. The desktop's allocator has
 * room to spare, so a pass there proves nothing about the console. */

#include "port/mem.h"

#include "tools/selftest.h"

#include <stdint.h>
#include <stdio.h>

/* The budget is 8,576 kB of a 24 MB partition. Held past this file, every
   later check ran starved: the card's artwork could not be placed, a scrim
   over it read as black, and "a caption's scrim never reaches opaque" failed
   on the console only. */
static int done(char *note, unsigned n, int rc) {
    (void)note;
    (void)n;
    mem_release_all();
    return rc;
}

static int budget_or_skip(char *note, unsigned n) {
    if (!mem_budget_ok()) {
        snprintf(note, n, "no budget on this machine -- it has no decoder to feed");
        return -1;
    }
    return 0;
}

/* The whole table, not each block alone: placement is what fails, not the
   total. */
static int t_the_whole_budget_can_be_placed(char *note, unsigned n) {
    int id, held = 0;

    if (budget_or_skip(note, n) != 0) return -1;

    if (mem_reserve_all() != 0) {
        snprintf(note, n, "the table would not be claimed whole");
        return done(note, n, 1);
    }
    for (id = 0; id < MEM_N; id++) {
        if (!mem_reserve((mem_id)id)) {
            snprintf(note, n, "block %d of %d is not held after reserving them all", id, (int)MEM_N);
            return done(note, n, 1);
        }
        held += (int)(mem_size((mem_id)id) / 1024u);
    }
    snprintf(note, n, "%d blocks, %d kB, placed in claim order", (int)MEM_N, held);
    return done(note, n, 0);
}

/* A decoder block on any other boundary is refused by the hardware with a
   status that names nothing. */
static int t_the_decoder_block_is_aligned_to_its_own_size(char *note, unsigned n) {
    uintptr_t addr;
    unsigned  want;

    if (budget_or_skip(note, n) != 0) return -1;
    if (mem_reserve_all() != 0) {
        snprintf(note, n, "the table would not be claimed");
        return done(note, n, 1);
    }

    want = mem_size(MEM_DECODER);
    addr = (uintptr_t)mem_reserve(MEM_DECODER);
    if (!addr) {
        snprintf(note, n, "the decoder block is not held at all");
        return done(note, n, 1);
    }
    if (want < 4u * 1024u * 1024u) {
        snprintf(note, n, "the decoder block is %u kB, and the engine wants 4 MB", want / 1024u);
        return done(note, n, 1);
    }
    if (addr & (uintptr_t)(want - 1u)) {
        snprintf(note, n, "the decoder block sits at %08llx, not on a %u kB boundary", (unsigned long long)addr, want / 1024u);
        return done(note, n, 1);
    }
    snprintf(note, n, "%u kB at %08llx, aligned to its own size", want / 1024u, (unsigned long long)addr);
    return done(note, n, 0);
}

/* A second block would be lost for the run. */
static int t_claiming_twice_is_the_same_block(char *note, unsigned n) {
    void *first, *second;

    if (budget_or_skip(note, n) != 0) return -1;
    if (mem_reserve_all() != 0) {
        snprintf(note, n, "the table would not be claimed");
        return done(note, n, 1);
    }

    first  = mem_reserve(MEM_VIDEO_RING);
    second = mem_reserve(MEM_VIDEO_RING);
    if (!first || first != second) {
        snprintf(note, n, "two claims gave %p and %p", first, second);
        return done(note, n, 1);
    }
    snprintf(note, n, "the second claim is the first block");
    return done(note, n, 0);
}

void test_mem_register(void) {
    selftest_add("mem", "the whole budget can be placed", t_the_whole_budget_can_be_placed);
    selftest_add("mem", "the decoder block is aligned to its own size", t_the_decoder_block_is_aligned_to_its_own_size);
    selftest_add("mem", "claiming twice is the same block", t_claiming_twice_is_the_same_block);
}

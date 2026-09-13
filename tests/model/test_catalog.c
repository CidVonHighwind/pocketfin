/* See model/catalog.h, and test_catalog.h for the real server the page checks
 * draw from. */

#include "model/catalog.h"
#include "test_catalog.h"

#include "port/platform.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

/* The console's join and sign-in measure about nine seconds. */
#define BOUND_US 30000000u

int real_library(char *note, unsigned n) {
    unsigned t0    = platform_clock_us();
    int      moved = 0;
    jf_conn  conn;

    if (library_start() != 0) {
        snprintf(note, n, "the library worker would not start");
        return -1;
    }
    /* The query and segment checks point the client at servers of their own,
       and sign in there as a user the real server has never heard of. */
    if (jf_load_conn(&conn) == 0) {
        char host[JF_HOST_LEN];
        int  port = 0;

        jf_address(host, sizeof(host), &port);
        moved = strcmp(host, conn.host) != 0 || port != conn.port;
    }
    /* The boot and offline checks leave a staged stage and a lost link. */
    session_fake_lost(0);
    if (moved || session_stage() != LIB_STAGE_READY || !jf_signed_in()) session_retry();

    for (;;) {
        lib_home home;

        library_watch_link();
        library_home(&home);
        if (session_stage() == LIB_STAGE_READY && home.state == LIB_READY) break;
        if (session_stage() == LIB_STAGE_STOPPED || platform_clock_us() - t0 > BOUND_US) {
            snprintf(note, n, "no server to ask: %s", session_error()[0] ? session_error() : "it did not answer");
            return -1;
        }
        platform_sleep_us(20000);
    }
    return 0;
}

static const item *rows_of(const char *parent, const char *title, int *count, unsigned *gen) {
    unsigned  t0   = platform_clock_us();
    int       desc = item_sort_default_descending(ITEM_SORT_NAME);
    lib_items li;

    for (;;) {
        library_items(parent, title, ITEM_SORT_NAME, desc, ITEM_FILTER_ALL, &li);
        if (li.rows) break;
        library_watch_link();
        if (li.state == LIB_FAILED || platform_clock_us() - t0 > BOUND_US) return 0;
        platform_sleep_us(20000);
    }
    *count = li.n;
    if (gen) *gen = li.gen;
    return li.rows;
}

/* Chosen once a kind: walking every library costs a request each. */
#define CHOSEN_MAX 4

static struct {
    int  valid, kind, at;
    char parent[JF_ID_LEN], title[ITEM_NAME_LEN];
} g_chosen[CHOSEN_MAX];

static int listing_gen(int kind, char *parent, char *title, int *count, int *at, unsigned *gen, char *note, unsigned n) {
    int c;

    if (real_library(note, n) != 0) return -1;

    for (c = 0; c < CHOSEN_MAX; c++)
        if (g_chosen[c].valid && g_chosen[c].kind == kind) break;
    if (c == CHOSEN_MAX) {
        lib_home    home;
        const item *views;
        int         nv, v, best_n = 0;

        for (c = 0; c < CHOSEN_MAX - 1 && g_chosen[c].valid; c++) {}
        g_chosen[c].valid = 0;
        library_home(&home);
        views = home.rail[LIB_RAIL_VIEWS];
        nv    = home.n[LIB_RAIL_VIEWS];
        for (v = 0; v < nv; v++) {
            char        id[JF_ID_LEN], name[ITEM_NAME_LEN];
            const item *rows;
            int         got = 0, k;

            /* Copied out: the rails can be fetched again while this waits. */
            snprintf(id, sizeof(id), "%s", views[v].id);
            snprintf(name, sizeof(name), "%s", views[v].name);
            rows = rows_of(id, name, &got, 0);
            for (k = 0; rows && k < got; k++)
                if (kind < 0 || (int)rows[k].kind == kind) break;
            if (rows && k < got && got > best_n) {
                best_n            = got;
                g_chosen[c].valid = 1;
                g_chosen[c].kind  = kind;
                g_chosen[c].at    = k;
                snprintf(g_chosen[c].parent, sizeof(g_chosen[c].parent), "%s", id);
                snprintf(g_chosen[c].title, sizeof(g_chosen[c].title), "%s", name);
            }
        }
        if (!g_chosen[c].valid) {
            snprintf(note, n, "none of %d libraries holds what this check needs", nv);
            return -1;
        }
    }

    snprintf(parent, JF_ID_LEN, "%s", g_chosen[c].parent);
    snprintf(title, ITEM_NAME_LEN, "%s", g_chosen[c].title);
    if (at) *at = g_chosen[c].at;
    if (!rows_of(parent, title, count, gen)) {
        snprintf(note, n, "\"%s\" would not load", title);
        return -1;
    }
    return 0;
}

int real_listing(int kind, char *parent, char *title, int *count, int *at, char *note, unsigned n) {
    return listing_gen(kind, parent, title, count, at, 0, note, n);
}

/* Refetching after a watched mark gives the same count and first row, so the
   generation is the only way a page can tell its rows were replaced. */
static int t_the_same_rows_are_still_a_new_answer(char *note, unsigned n) {
    char     parent[JF_ID_LEN], title[ITEM_NAME_LEN];
    int      first_n = 0, second_n = 0;
    unsigned first = 0, second = 0;

    if (listing_gen(-1, parent, title, &first_n, 0, &first, note, n) != 0) return -1;
    library_forget_items();
    if (listing_gen(-1, parent, title, &second_n, 0, &second, note, n) != 0) return -1;

    if (second == first) {
        snprintf(note, n, "fetching \"%s\" again left the generation at %u -- a refresh is invisible", title, first);
        return 1;
    }
    if (second_n != first_n) {
        snprintf(note, n, "%d rows, then %d: the library changed under the check", first_n, second_n);
        return -1;
    }
    snprintf(note, n, "\"%s\" fetched twice, %d rows both times, generation %u -> %u", title, first_n, first, second);
    return 0;
}

void test_catalog_register(void) {
    selftest_add("catalog", "the same rows are still a new answer", t_the_same_rows_are_still_a_new_answer);
}

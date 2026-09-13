/* The server orders and filters: a listing is at most LIB_ITEM_MAX rows, and
   ordering them here ranked the alphabetically-first 128 as "top rated". What
   is left is a mapping, which goes wrong silently. */

#include "jelly/query.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

/* A duplicate key is a sort that quietly does what its neighbour does. */
static int t_every_sort_has_its_own_label_and_key(char *note, unsigned n) {
    int i, j;

    for (i = 0; i < ITEM_SORT_COUNT; i++) {
        const char *label = item_sort_label((item_sort)i);
        const char *key   = item_sort_key((item_sort)i);

        if (!label || !label[0]) {
            snprintf(note, n, "sort %d has no label", i);
            return 1;
        }
        if (!key || !key[0]) {
            snprintf(note, n, "sort %d (%s) has no server key", i, label);
            return 1;
        }
        for (j = 0; j < i; j++) {
            if (strcmp(label, item_sort_label((item_sort)j)) == 0) {
                snprintf(note, n, "sorts %d and %d are both \"%s\"", j, i, label);
                return 1;
            }
            if (strcmp(key, item_sort_key((item_sort)j)) == 0) {
                snprintf(note, n, "sorts %d and %d both ask for \"%s\"", j, i, key);
                return 1;
            }
        }
    }
    snprintf(note, n, "%d sorts, each with its own label and key", ITEM_SORT_COUNT);
    return 0;
}

/* Without a second key, a block the server can't separate -- common, with no
   rating or premiere date -- reorders itself between requests, under the
   cursor. */
static int t_every_sort_has_a_tie_break(char *note, unsigned n) {
    int i;

    for (i = 0; i < ITEM_SORT_COUNT; i++) {
        const char *key = item_sort_key((item_sort)i);

        if (i == ITEM_SORT_RANDOM || i == ITEM_SORT_NAME) continue;
        if (!strstr(key, ",SortName")) {
            snprintf(note, n, "%s asks for \"%s\" with nothing to break a tie", item_sort_label((item_sort)i), key);
            return 1;
        }
    }
    snprintf(note, n, "every ordered sort falls back to SortName");
    return 0;
}

/* An empty Filters= is a 400. */
static int t_all_sends_no_filter(char *note, unsigned n) {
    int i;

    if (item_filter_key(ITEM_FILTER_ALL) != 0) {
        snprintf(note, n, "All asks for \"%s\"; it must ask for nothing", item_filter_key(ITEM_FILTER_ALL));
        return 1;
    }
    for (i = 0; i < ITEM_FILTER_COUNT; i++) {
        if (i == ITEM_FILTER_ALL) continue;
        if (!item_filter_key((item_filter)i)) {
            snprintf(note, n, "filter %d (%s) has no server key", i, item_filter_label((item_filter)i));
            return 1;
        }
    }
    snprintf(note, n, "All sends none; the other %d each send one", ITEM_FILTER_COUNT - 1);
    return 0;
}

/* Getting this wrong puts the least-recent item first the moment a viewer
   picks "Date added". */
static int t_defaults_read_the_way_a_viewer_expects(char *note, unsigned n) {
    struct {
        item_sort s;
        int       desc;
    } want[] = {{ITEM_SORT_NAME, 0},    {ITEM_SORT_ADDED, 1},  {ITEM_SORT_RELEASE, 1}, {ITEM_SORT_RATING, 1},
                {ITEM_SORT_RUNTIME, 0}, {ITEM_SORT_PLAYED, 1}, {ITEM_SORT_RANDOM, 0}};
    int i;

    for (i = 0; i < (int)(sizeof(want) / sizeof(want[0])); i++)
        if (item_sort_default_descending(want[i].s) != want[i].desc) {
            snprintf(note, n, "%s defaults to %s", item_sort_label(want[i].s), want[i].desc ? "ascending" : "descending");
            return 1;
        }
    snprintf(note, n, "all %d defaults point the way a viewer expects", (int)(sizeof(want) / sizeof(want[0])));
    return 0;
}

void test_item_sort_register(void) {
    selftest_add("item_sort", "every sort has its own label and key", t_every_sort_has_its_own_label_and_key);
    selftest_add("item_sort", "every ordered sort breaks ties by name", t_every_sort_has_a_tie_break);
    selftest_add("item_sort", "All sends no filter at all", t_all_sends_no_filter);
    selftest_add("item_sort", "the defaults read the way a viewer expects", t_defaults_read_the_way_a_viewer_expects);
}

#include "page/item_text.h"

#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

/* A tick is 100 ns, so a two-hour film is 72,000,000,000 of them -- eleven
   digits, and an int holds ten. That once wrapped, drawing a watched tick on
   an unwatched film and nothing on a watched one. Small numbers can't fail
   this way. */
static int t_progress_survives_a_real_runtime(char *note, unsigned n) {
    item it;
    int  pct;

    memset(&it, 0, sizeof(it));
    it.season_no = it.episode_no = -1;

    it.run_ticks    = 72000000000ULL;
    it.resume_ticks = 18000000000ULL;
    pct             = item_progress_pct(&it);
    if (pct != 25) {
        snprintf(note, n, "a quarter through a two-hour film read %d%%", pct);
        return 1;
    }

    it.resume_ticks = 60000000ULL; /* six seconds */
    pct             = item_progress_pct(&it);
    if (pct != 0) {
        snprintf(note, n, "six seconds into two hours read %d%%", pct);
        return 1;
    }

    it.resume_ticks = it.run_ticks + 1;
    if (item_progress_pct(&it) != 100) {
        snprintf(note, n, "a resume past the end read %d%%", item_progress_pct(&it));
        return 1;
    }

    /* Dividing by a folder's zero runtime would end the frame, not give a
       wrong number. */
    it.run_ticks    = 0;
    it.resume_ticks = 5;
    if (item_progress_pct(&it) != 0) {
        snprintf(note, n, "an item with no runtime read %d%%", item_progress_pct(&it));
        return 1;
    }
    if (item_progress_pct(0) != 0) {
        snprintf(note, n, "a null item read %d%%", item_progress_pct(0));
        return 1;
    }

    snprintf(note, n, "25%% of 72,000,000,000 ticks, clamped at both ends");
    return 0;
}

static int t_a_row_title_is_the_items_own_name(char *note, unsigned n) {
    item it;
    char out[ITEM_ROW_TEXT];

    memset(&it, 0, sizeof(it));
    snprintf(it.name, sizeof(it.name), "Spirited Away");
    it.season_no = it.episode_no = -1;

    item_row_title(&it, 0, out, sizeof(out));
    if (strcmp(out, "Spirited Away") != 0) {
        snprintf(note, n, "row title read \"%s\"", out);
        return 1;
    }
    /* NULL is a real case: a row past the end of what is on screen. */
    item_row_title(0, 0, out, sizeof(out));
    if (out[0] != 0) {
        snprintf(note, n, "a null item did not clear the output");
        return 1;
    }
    return 0;
}

/* Which of a row's two lines carries the series and the number depends on
   where the row is, so both shapes are pinned. */
static int t_an_episode_reads_the_way_reference_writes_it(char *note, unsigned n) {
    struct {
        int         flat;
        const char *title, *sub;
    } want[2] = {/* A flat rail: nothing else on screen names the series. */
                 {1, "Andor  S1E01  Kassa", "Episode"},
                 /* Inside its season: the series IS the page title. */
                 {0, "Kassa", "1. Kassa"}};
    char title[ITEM_ROW_TEXT], sub[ITEM_ROW_TEXT];
    item it;
    int  i;

    memset(&it, 0, sizeof(it));
    snprintf(it.name, sizeof(it.name), "Kassa");
    snprintf(it.series, sizeof(it.series), "Andor");
    it.kind       = ITEM_KIND_EPISODE;
    it.season_no  = 1;
    it.episode_no = 1;

    for (i = 0; i < 2; i++) {
        item_row_title(&it, want[i].flat, title, sizeof(title));
        item_row_subtitle(&it, want[i].flat, sub, sizeof(sub));
        if (strcmp(title, want[i].title) != 0) {
            snprintf(note, n, "%s title is \"%s\", reference writes \"%s\"", want[i].flat ? "a rail's" : "a listing's", title,
                     want[i].title);
            return 1;
        }
        if (strcmp(sub, want[i].sub) != 0) {
            snprintf(note, n, "%s subtitle is \"%s\", reference writes \"%s\"", want[i].flat ? "a rail's" : "a listing's", sub,
                     want[i].sub);
            return 1;
        }
    }

    /* Numbered on one axis only prints neither: half of an "S1E" reads as a
       bug, and Jellyfin does hand those back. */
    it.episode_no = -1;
    item_row_title(&it, 1, title, sizeof(title));
    if (strcmp(title, "Andor  Kassa") != 0) {
        snprintf(note, n, "a half-numbered episode reads \"%s\"", title);
        return 1;
    }
    snprintf(note, n, "both shapes match reference");
    return 0;
}

/* Only a folder that belongs to a series names the series instead. A season
   names itself: "Andor" under the heading "Andor" says nothing, and this build
   once drew it. */
static int t_a_row_says_what_kind_of_thing_it_is(char *note, unsigned n) {
    struct {
        int         kind;
        const char *series, *want;
    } CASE[] = {{ITEM_KIND_MOVIE, "", "Movie"},   {ITEM_KIND_SERIES, "", "Series"},     {ITEM_KIND_SEASON, "Andor", "Season"},
                {ITEM_KIND_SEASON, "", "Season"}, {ITEM_KIND_FOLDER, "Andor", "Andor"}, {ITEM_KIND_FOLDER, "", "Library"}};
    char sub[ITEM_ROW_TEXT];
    item it;
    int  i;

    for (i = 0; i < (int)(sizeof(CASE) / sizeof(CASE[0])); i++) {
        memset(&it, 0, sizeof(it));
        it.kind      = (item_kind)CASE[i].kind;
        it.season_no = it.episode_no = -1;
        snprintf(it.name, sizeof(it.name), "Something");
        snprintf(it.series, sizeof(it.series), "%s", CASE[i].series);

        item_row_subtitle(&it, 0, sub, sizeof(sub));
        if (strcmp(sub, CASE[i].want) != 0) {
            snprintf(note, n, "kind %d reads \"%s\", wanted \"%s\"", CASE[i].kind, sub, CASE[i].want);
            return 1;
        }
    }
    snprintf(note, n, "five kinds, each naming itself");
    return 0;
}

void test_item_row_register(void) {
    selftest_add("item_row", "a row title is the item's own name", t_a_row_title_is_the_items_own_name);
    selftest_add("item_row", "an episode reads the way reference writes it", t_an_episode_reads_the_way_reference_writes_it);
    selftest_add("item_row", "a row says what kind of thing it is", t_a_row_says_what_kind_of_thing_it_is);
    selftest_add("item_row", "progress survives a real runtime", t_progress_survives_a_real_runtime);
}

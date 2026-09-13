/* See item_text.h. */

#include "page/item_text.h"

#include <stdio.h>
#include <string.h>

void item_art_box(const item *it, int *w, int *h) {
    int wide = it && (it->kind == ITEM_KIND_EPISODE || it->kind == ITEM_KIND_FOLDER);

    if (w) *w = wide ? ITEM_THUMB_WIDE_W : ITEM_THUMB_W;
    if (h) *h = wide ? ITEM_THUMB_WIDE_H : ITEM_THUMB_H;
}

void item_label(const item *it, int with_series, char *out, size_t outlen) {
    if (!out || !outlen) return;
    out[0] = 0;
    if (!it) return;

    if (it->kind != ITEM_KIND_EPISODE) {
        snprintf(out, outlen, "%s", it->name);
        return;
    }

    /* Season and episode together or not at all: the server does hand back
       episodes with one and not the other. */
    if (with_series && it->series[0]) {
        if (it->season_no >= 0 && it->episode_no >= 0)
            snprintf(out, outlen, "%s  S%dE%02d  %s", it->series, it->season_no, it->episode_no, it->name);
        else
            snprintf(out, outlen, "%s  %s", it->series, it->name);
        return;
    }
    if (it->episode_no >= 0)
        snprintf(out, outlen, "%d. %s", it->episode_no, it->name);
    else
        snprintf(out, outlen, "%s", it->name);
}

void item_row_title(const item *it, int flat, char *out, size_t outlen) {
    if (!out || !outlen) return;
    out[0] = 0;
    if (!it) return;

    if (flat)
        item_label(it, 1, out, outlen);
    else
        snprintf(out, outlen, "%s", it->name);
}

int item_progress_pct(const item *it) {
    if (!it || it->run_ticks == 0) return 0;
    if (it->resume_ticks >= it->run_ticks) return 100;
    return (int)((it->resume_ticks * 100u) / it->run_ticks);
}

static const char *kind_name(item_kind k) {
    switch (k) {
    case ITEM_KIND_MOVIE: return "Movie";
    case ITEM_KIND_SERIES: return "Series";
    case ITEM_KIND_SEASON: return "Season";
    case ITEM_KIND_EPISODE: return "Episode";
    case ITEM_KIND_COLLECTION: return "Collection";
    default: return "Library";
    }
}

void item_row_subtitle(const item *it, int flat, char *out, size_t outlen) {
    char label[ITEM_ROW_TEXT];

    if (!out || !outlen) return;
    out[0] = 0;
    if (!it) return;

    if (flat) {
        snprintf(out, outlen, "%s", kind_name(it->kind));
        return;
    }

    /* An episode with no number formats as its own name, which the title
       already says. */
    if (it->kind == ITEM_KIND_EPISODE) {
        item_label(it, 0, label, sizeof(label));
        if (label[0] && strcmp(label, it->name) != 0) {
            snprintf(out, outlen, "%s", label);
            return;
        }
    }

    /* A folder belonging to a show would otherwise read "Library". */
    if (it->kind == ITEM_KIND_FOLDER && it->series[0]) {
        snprintf(out, outlen, "%s", it->series);
        return;
    }

    snprintf(out, outlen, "%s", kind_name(it->kind));
}

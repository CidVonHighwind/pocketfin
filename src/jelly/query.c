#include "jelly/query.h"

/* The tie-breakers are not decoration: many items have no rating, runtime or
 * premiere date, and a block of nulls with no second key comes back in a
 * different order each request, reordering the listing under the cursor. */
static const struct {
    const char *label, *key;
    int         descending; /* when first chosen */
} kSort[ITEM_SORT_COUNT] = {
    [ITEM_SORT_NAME]    = {"Name", "SortName", 0},
    [ITEM_SORT_ADDED]   = {"Date added", "DateCreated,SortName", 1},
    [ITEM_SORT_RELEASE] = {"Release date", "PremiereDate,ProductionYear,SortName", 1},
    [ITEM_SORT_RATING]  = {"Rating", "CommunityRating,SortName", 1},
    [ITEM_SORT_RUNTIME] = {"Runtime", "Runtime,SortName", 0},
    [ITEM_SORT_PLAYED]  = {"Last played", "DatePlayed,SortName", 1},
    [ITEM_SORT_RANDOM]  = {"Random", "Random", 0},
};

static const struct {
    const char *label, *key;
} kFilter[ITEM_FILTER_COUNT] = {
    [ITEM_FILTER_ALL]       = {"All", 0},
    [ITEM_FILTER_UNPLAYED]  = {"Unplayed", "IsUnplayed"},
    [ITEM_FILTER_PLAYED]    = {"Played", "IsPlayed"},
    [ITEM_FILTER_FAVORITE]  = {"Favourites", "IsFavorite"},
    [ITEM_FILTER_RESUMABLE] = {"In progress", "IsResumable"},
};

static item_sort   sort_of(item_sort s) { return (unsigned)s < (unsigned)ITEM_SORT_COUNT ? s : ITEM_SORT_NAME; }
static item_filter filter_of(item_filter f) { return (unsigned)f < (unsigned)ITEM_FILTER_COUNT ? f : ITEM_FILTER_ALL; }

const char *item_sort_label(item_sort s) { return kSort[sort_of(s)].label; }
const char *item_sort_key(item_sort s) { return kSort[sort_of(s)].key; }
int         item_sort_default_descending(item_sort s) { return kSort[sort_of(s)].descending; }
const char *item_filter_label(item_filter f) { return kFilter[filter_of(f)].label; }
const char *item_filter_key(item_filter f) { return kFilter[filter_of(f)].key; }

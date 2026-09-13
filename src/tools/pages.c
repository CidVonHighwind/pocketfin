/* See pages.h. */

#include "tools/pages.h"

#include "page/boot.h"
#include "page/home.h"
#include "model/catalog.h"
#include "page/listing.h"
#include "page/offline.h"
#include "page/player.h"
#include "view/stack.h"
#include "page/settings.h"

#include <string.h>

static void go_home(void) { screen_reset(&home_page_screen); }
static void go_boot(void) { screen_reset(&boot_page_screen); }
static void go_settings(void) { screen_reset(&settings_page_screen); }
static void go_player(void) { screen_reset(&player_page_screen); }

/* The first library, once the front page knows what they are. */
static void go_listing(void) {
    lib_home home;

    screen_reset(&home_page_screen);
    library_home(&home);
    if (home.n[LIB_RAIL_VIEWS] > 0)
        listing_page_show(home.rail[LIB_RAIL_VIEWS][0].id, home.rail[LIB_RAIL_VIEWS][0].name);
    else
        listing_page_show(0, "Everything");
}

static void go_offline(void) { screen_reset(&offline_page_screen); }

static const struct {
    const char *name;
    void (*go)(void);
} PAGES[] = {
    {"home", go_home}, {"settings", go_settings}, {"boot", go_boot}, {"list", go_listing}, {"offline", go_offline}, {"player", go_player},
};

#define PAGE_N ((int)(sizeof(PAGES) / sizeof(PAGES[0])))

int pages_go(const char *name) {
    int i;

    if (!name) return 0;
    for (i = 0; i < PAGE_N; i++)
        if (!strcmp(name, PAGES[i].name)) {
            PAGES[i].go();
            return 1;
        }
    return 0;
}

const char *pages_names(void) { return "home, settings, boot, list, offline, player"; }

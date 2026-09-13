/* See tools/pages.h. `psp.sh cmd page <name>` reaches every screen through
 * this. pages_go() walks a table and pages_names() is a hand-written string,
 * and nothing else compares them. */

#include "tools/pages.h"

#include "port/gfx.h"
#include "port/text.h"
#include "page/home.h"
#include "view/stack.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

/* An unknown name landing somewhere quiet gives a script asking for "detailhome"'s pixels, and it never knows. */
static int t_every_advertised_page_can_be_reached(char *note, unsigned n) {
    char names[128], *p;
    int  found = 0;

    if (gfx_start() != 0 || text_start() != 0) {
        snprintf(note, n, "no panel or no glyphs");
        return -1;
    }

    snprintf(names, sizeof(names), "%s", pages_names());
    for (p = strtok(names, ", "); p; p = strtok(0, ", ")) {
        if (!pages_go(p)) {
            snprintf(note, n, "\"%s\" is advertised and refused", p);
            return 1;
        }
        found++;
    }
    if (found == 0) {
        snprintf(note, n, "pages_names() advertised nothing");
        return 1;
    }
    if (!pages_go("home")) {
        snprintf(note, n, "\"home\" is not reachable");
        return 1;
    }
    if (pages_go("no-such-page") || pages_go("")) {
        snprintf(note, n, "a name nothing is called was accepted");
        return 1;
    }

    screen_reset(&home_page_screen);
    snprintf(note, n, "%d names advertised, all reachable, nothing else is", found);
    return 0;
}

void test_pages_register(void) { selftest_add("pages", "every advertised page can be reached", t_every_advertised_page_can_be_reached); }

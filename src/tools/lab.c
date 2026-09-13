/* See lab.h. */

#include "tools/lab.h"

#include "view/element.h"
#include "view/page_chrome.h"

void lab_page_draw(void) {
    static const ui_hint_item LEFT[]  = {{UI_BTN_TRIANGLE, "Reload"}, {UI_BTN_START, "Settings"}};
    static const ui_hint_item RIGHT[] = {{UI_BTN_CROSS, "Select"}, {UI_BTN_CIRCLE, "Back"}};
    ui_layout                 page;

    /* No coordinate, face, colour, height or margin. */
    ui_page_begin(&page, UI_PAGE_TEXT, "Lab", "Name", UI_ARROW_UP);

    ui_heading(&page, "Lab");
    ui_body(&page, "A page names what is on it.");
    ui_caption(&page,
               "The face, the ink, the band and the space above each line come from style.h; the bars and the margins come from the page.");

    ui_heading(&page, "A second heading");
    ui_body(&page, "It leads with more space than a caption does, because a heading is a heading.");
    ui_caption(&page, "A line wider than the page is cut with an ellipsis rather than running off the panel.");

    ui_page_end(&page, LEFT, 2, RIGHT, 2);
}

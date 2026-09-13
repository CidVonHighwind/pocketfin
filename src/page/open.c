/* See open.h. */

#include "page/open.h"

#include "page/detail.h"
#include "page/listing.h"
#include "page/network.h"

#include "io/net.h"
#include "model/catalog.h"
#include "port/platform.h"
#include "view/stack.h"

/* DOWN only: the radio is on and joined nothing, the one state the dialog can
   fix. With the link UP a refused sign-in has nothing to do with the network,
   and OFF is the switch -- offered there, the dialog came up on a black panel. */
int page_offer_picker(net_state_t link, int has_picker) { return has_picker && link == NET_DOWN; }

int page_retry_or_pick(void) {
    if (page_offer_picker(net_state(), platform_has_picker())) {
        screen_push(&network_page_screen);
        return 1;
    }
    session_retry();
    return 0;
}

void page_open_item(const item *it) {
    if (!it) return;

    if (it->kind == ITEM_KIND_MOVIE || it->kind == ITEM_KIND_EPISODE)
        detail_page_show(it);
    else
        listing_page_show(it->id, it->name);
}

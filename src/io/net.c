/* See net.h. The machine halves are port/{psp,desktop}/net.c. */

#include "io/net.h"
#include "io/net_private.h"

#include "base/log.h"
#include "base/standby.h"

#include <stdio.h>
#include <string.h>

static unsigned g_link_epoch; /* the standby generation it was made in; 0 when none */
static int      g_profile;
static char     g_ip[24];

const char *net_state_text(net_state_t s) {
    switch (s) {
    case NET_OFF: return "off";
    case NET_DOWN: return "down";
    case NET_UP: return "up";
    default: return "?";
    }
}

int         net_joined(void) { return g_link_epoch != 0; }
const char *net_address(void) { return g_ip; }

/* Not cleared by leaving: a wake rejoins with it. */
int net_profile(void) { return g_profile; }

int net_revalidate(void) {
    if (!g_link_epoch) {
        log_line("net: nothing to revalidate");
        return -1;
    }
    g_link_epoch = standby_epoch();
    log_printf("net: the association is current again, epoch %u", g_link_epoch);
    return 0;
}

void net_note_joined(const char *ip, int profile) {
    g_link_epoch = standby_epoch();
    if (profile > 0) g_profile = profile;
    snprintf(g_ip, sizeof(g_ip), "%s", ip ? ip : "");
    log_printf("net: up at %s on connection %d in epoch %u", g_ip, g_profile, g_link_epoch);
}

void net_note_left(void) {
    g_link_epoch = 0;
    g_ip[0]      = 0;
}

int net_link_is_current(void) { return g_link_epoch && g_link_epoch == standby_epoch(); }

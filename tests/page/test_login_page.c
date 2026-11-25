/* The login page. Typing goes through the console's keyboard, which a check
   cannot drive, so what the typing feeds -- the address -- is checked as a
   function, and the page through the session's fake seam. */

#include "page/boot.h"
#include "page/login.h"

#include "model/catalog.h"
#include "view/stack.h"
#include "tools/selftest.h"
#include "test_page.h"

#include <stdio.h>
#include <string.h>

static int address(const char *typed, const char *host, int port, int https, int start_port, int start_https, char *note, unsigned n) {
    jf_conn c;

    memset(&c, 0, sizeof(c));
    c.port  = start_port;
    c.https = start_https;
    login_take_address(typed, &c);
    if (strcmp(c.host, host) != 0 || c.port != port || c.https != https) {
        snprintf(note, n, "\"%s\" gave \"%s\" port %d https %d, not \"%s\" %d %d", typed, c.host, c.port, c.https, host, port, https);
        return 1;
    }
    return 0;
}

/* People paste what their browser shows. */
static int t_a_pasted_address_is_taken_apart(char *note, unsigned n) {
    return address("192.168.99.5", "192.168.99.5", 8096, 0, 8096, 0, note, n) ||
           address("https://jellyfin.example.com", "jellyfin.example.com", 443, 1, 8096, 0, note, n) ||
           address("https://jellyfin.example.com/web/index.html#!/home", "jellyfin.example.com", 443, 1, 8096, 0, note, n) ||
           address("http://192.168.99.5:8096", "192.168.99.5", 8096, 0, 443, 1, note, n) ||
           address("media.example.org:8920", "media.example.org", 8920, 0, 8096, 0, note, n) ||
           address("  10.0.0.4  ", "10.0.0.4", 8096, 0, 8096, 0, note, n);
}

/* A chosen port survives a scheme in front of the name. */
static int t_a_chosen_port_is_kept(char *note, unsigned n) {
    return address("https://name.example", "name.example", 8443, 1, 8443, 0, note, n);
}

/* Linked with nobody to sign in as: the boot page hands over to the login page
   rather than stopping with an error. */
static int t_signed_out_goes_to_the_login_page(char *note, unsigned n) {
    if (page_panel(note, n) != 0) return -1;

    session_fake_stage(LIB_STAGE_LINKING, "");
    screen_reset(&boot_page_screen);
    page_step(0);
    session_fake_stage(LIB_STAGE_SIGNED_OUT, "");
    page_step(0);
    page_step(0);
    if (strcmp(screen_top_name(), "login") != 0) {
        snprintf(note, n, "signed out, and the top screen is \"%s\", not login", screen_top_name());
        return 1;
    }
    if (screen_depth() != 1) {
        snprintf(note, n, "the login page came up %d deep -- the boot page is still under it", screen_depth());
        return 1;
    }
    if (!login_page_value(LOGIN_ROW_PORT)[0]) {
        snprintf(note, n, "the port row shows nothing");
        return 1;
    }
    return 0;
}

void test_login_page_register(void) {
    selftest_add("login", "a pasted address is taken apart", t_a_pasted_address_is_taken_apart);
    selftest_add("login", "a chosen port is kept", t_a_chosen_port_is_kept);
    selftest_add("login", "signed out goes to the login page", t_signed_out_goes_to_the_login_page);
}

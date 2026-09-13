/* See jelly/api.h. The connection file is written by hand before anything
 * works, so what it forgives is the feature. */

#include "jelly/api.h"

#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

static int parse(const char *text, jf_conn *c) { return jf_conn_parse(text, c, "a check"); }

static int t_a_setting_a_line_in_any_order(char *note, unsigned n) {
    jf_conn c;

    if (parse("port 9000\n"
              "password hunter2\n"
              "host 10.0.0.4\n"
              "profile 3\n"
              "user alice\n",
              &c) != 0) {
        snprintf(note, n, "a complete file was refused");
        return 1;
    }
    if (strcmp(c.host, "10.0.0.4") || c.port != 9000 || strcmp(c.user, "alice") || strcmp(c.pass, "hunter2") || c.profile != 3) {
        snprintf(note, n, "read back \"%s\" %d \"%s\" profile %d", c.host, c.port, c.user, c.profile);
        return 1;
    }
    return 0;
}

static int t_it_forgives_what_a_hand_typed_file_carries(char *note, unsigned n) {
    jf_conn c;

    if (parse("# where the server is\r\n"
              "\r\n"
              "  HOST = 10.0.0.4   # trailing note\r\n"
              "Port: 9000\r\n"
              "\tUsername\talice\r\n",
              &c) != 0) {
        snprintf(note, n, "refused a file with comments, tabs and CRLF");
        return 1;
    }
    if (strcmp(c.host, "10.0.0.4") || c.port != 9000 || strcmp(c.user, "alice")) {
        snprintf(note, n, "read back \"%s\" %d \"%s\"", c.host, c.port, c.user);
        return 1;
    }
    return 0;
}

/* A fixed-column format once cut a password at a space without saying so. */
static int t_a_password_may_hold_spaces(char *note, unsigned n) {
    jf_conn c;

    if (parse("host 10.0.0.4\npassword two words\n", &c) != 0) {
        snprintf(note, n, "refused");
        return 1;
    }
    if (strcmp(c.pass, "two words") != 0) {
        snprintf(note, n, "the password came back as \"%s\"", c.pass);
        return 1;
    }
    return 0;
}

static int t_only_the_host_is_required(char *note, unsigned n) {
    jf_conn c;

    if (parse("host 10.0.0.4\n", &c) != 0) {
        snprintf(note, n, "a host on its own was refused");
        return 1;
    }
    /* Profile 0 says nobody chose one, so the viewer is asked rather than
       joined blind to the first saved network. */
    if (c.port != 8096 || c.profile != 0) {
        snprintf(note, n, "port %d, profile %d", c.port, c.profile);
        return 1;
    }
    if (c.user[0] || c.pass[0]) {
        snprintf(note, n, "invented a user or a password");
        return 1;
    }
    return 0;
}

/* Refused rather than half-read, or a fixable mistake is a client that sits
   there. */
static int t_a_file_that_cannot_work_is_refused(char *note, unsigned n) {
    jf_conn c;

    if (parse("port 8096\nuser alice\n", &c) == 0) {
        snprintf(note, n, "a file with no host was accepted");
        return 1;
    }
    if (parse("host 10.0.0.4\nport 70000\n", &c) == 0) {
        snprintf(note, n, "port 70000 was accepted");
        return 1;
    }
    if (parse("host 10.0.0.4\nport 0\n", &c) == 0) {
        snprintf(note, n, "port 0 was accepted");
        return 1;
    }
    return 0;
}

/* Unknown names are logged, or a misspelling is a sign-in that fails with
   nothing to look at. */
static int t_a_misspelt_name_does_not_become_a_password(char *note, unsigned n) {
    jf_conn c;

    if (parse("host 10.0.0.4\npasswrod hunter2\n", &c) != 0) {
        snprintf(note, n, "refused outright");
        return 1;
    }
    if (c.pass[0]) {
        snprintf(note, n, "\"passwrod\" was read as the password");
        return 1;
    }
    return 0;
}

void test_conn_register(void) {
    selftest_add("conn", "a setting a line, in any order", t_a_setting_a_line_in_any_order);
    selftest_add("conn", "it forgives what a hand-typed file carries", t_it_forgives_what_a_hand_typed_file_carries);
    selftest_add("conn", "a password may hold spaces", t_a_password_may_hold_spaces);
    selftest_add("conn", "only the host is required", t_only_the_host_is_required);
    selftest_add("conn", "a file that cannot work is refused", t_a_file_that_cannot_work_is_refused);
    selftest_add("conn", "a misspelt name does not become a password", t_a_misspelt_name_does_not_become_a_password);
}

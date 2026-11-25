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

/* An address reached through a reverse proxy is HTTPS, said in the file. */
static int t_https_is_read_from_the_file(char *note, unsigned n) {
    jf_conn c;

    if (parse("host jellyfin.example.com\nport 443\nhttps yes\n", &c) != 0 || !c.https || c.port != 443 ||
        strcmp(c.host, "jellyfin.example.com") != 0) {
        snprintf(note, n, "read back \"%s\" %d https=%d", c.host, c.port, c.https);
        return 1;
    }
    if (parse("host 10.0.0.4\n", &c) != 0 || c.https) {
        snprintf(note, n, "a file that says nothing about HTTPS came back with it on");
        return 1;
    }
    return 0;
}

/* account.txt is written by the application and read at every start. */
static int t_the_account_file_reads_back(char *note, unsigned n) {
    jf_conn c;
    char    token[JF_TOKEN_LEN], uid[JF_ID_LEN];

    if (jf_account_parse("# Written by Pocketfin\nhost: media.example.org\nport: 443\nhttps: 1\nuser: alice\n"
                         "user_id: 0123abcd\ntoken: 9f8e7d6c5b4a\n",
                         &c, token, sizeof(token), uid, sizeof(uid)) != 0) {
        snprintf(note, n, "a complete account was refused");
        return 1;
    }
    if (strcmp(c.host, "media.example.org") || c.port != 443 || !c.https || strcmp(c.user, "alice") || strcmp(uid, "0123abcd") ||
        strcmp(token, "9f8e7d6c5b4a")) {
        snprintf(note, n, "read back \"%s\" %d https=%d \"%s\" \"%s\" \"%s\"", c.host, c.port, c.https, c.user, uid, token);
        return 1;
    }
    /* Signed out: the server stays, the session goes. */
    if (jf_account_parse("host: 10.0.0.4\nport: 8096\nhttps: 0\nuser: alice\nuser_id: \ntoken: \n", &c, token, sizeof(token), uid,
                         sizeof(uid)) != 0 ||
        token[0] || strcmp(c.host, "10.0.0.4")) {
        snprintf(note, n, "a signed-out account read as \"%s\" with token \"%s\"", c.host, token);
        return 1;
    }
    if (jf_account_parse("user: alice\n", &c, token, sizeof(token), uid, sizeof(uid)) == 0) {
        snprintf(note, n, "an account with no server was accepted");
        return 1;
    }
    return 0;
}

/* A quote in a password once ended the sign-in body early. */
static int t_json_strings_are_escaped(char *note, unsigned n) {
    char out[64];

    jf_json_escape("a\"b\\c\nd", out, sizeof(out));
    if (strcmp(out, "a\\\"b\\\\c\\u000ad") != 0) {
        snprintf(note, n, "escaped as %s", out);
        return 1;
    }
    jf_json_escape("\"\"\"\"\"\"", out, 6);
    if (strlen(out) > 5 || (strlen(out) % 2) != 0) {
        snprintf(note, n, "a cut escape left \"%s\" -- half a pair", out);
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
    selftest_add("conn", "https is read from the file", t_https_is_read_from_the_file);
    selftest_add("conn", "the account file reads back", t_the_account_file_reads_back);
    selftest_add("conn", "json strings are escaped", t_json_strings_are_escaped);
}

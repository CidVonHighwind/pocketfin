/* See model/session.h. session_lost() raises the offline screen: a boot that
 * never found the server says so on the boot screen, and only a library that
 * stopped answering goes offline. No radio, server or worker, so these run on
 * both machines. */

#include "model/session.h"

#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

static void a_working_session(void) {
    session_fake_stage(LIB_STAGE_READY, "");
    session_fake_lost(0);
    session_note_reached();
}

/* A first boot with the server off fails with the same error a lost link
   gives, and somebody who has never connected needs the boot screen's reason,
   not the offline screen. */
static int t_a_server_never_reached_is_not_a_lost_one(char *note, unsigned n) {
    int i;

    session_fake_stage(LIB_STAGE_SIGNIN, "");
    session_fake_lost(0);

    for (i = 0; i < 8; i++) session_note_failure(JF_ERR_OFFLINE);

    if (session_lost()) {
        snprintf(note, n, "8 failures before ever connecting read as a lost link");
        return 1;
    }
    snprintf(note, n, "8 offline failures, never connected, not lost");
    return 0;
}

/* A 404 or a refusal is the server answering; read as a lost link it sends
   the viewer offline for a film that simply is not there any more. */
static int t_a_server_that_answers_is_not_a_lost_link(char *note, unsigned n) {
    int i;

    a_working_session();

    for (i = 0; i < 8; i++) {
        session_note_failure(JF_ERR_MISSING);
        session_note_failure(JF_ERR_SERVER);
        session_note_failure(JF_ERR_AUTH);
        session_note_failure(JF_ERR_GARBLED);
    }

    if (session_lost()) {
        snprintf(note, n, "32 answered-but-refused requests read as a lost link");
        return 1;
    }
    snprintf(note, n, "missing, server, auth and garbled: answered, not lost");
    return 0;
}

static int t_a_reached_server_that_stops_answering_is_lost(char *note, unsigned n) {
    int tries = 0;

    a_working_session();
    if (session_lost()) {
        snprintf(note, n, "a freshly reached session already reads as lost");
        return 1;
    }

    while (!session_lost() && tries < 16) {
        session_note_failure(JF_ERR_OFFLINE);
        tries++;
    }
    if (!session_lost()) {
        snprintf(note, n, "16 offline failures after connecting and still not lost");
        return 1;
    }

    /* A single missed request is a packet, not a link: the offline screen
       must not flicker up between two good frames. */
    if (tries < 2) {
        snprintf(note, n, "one missed request was enough to declare it lost");
        return 1;
    }
    snprintf(note, n, "lost after %d offline failures, and not after one", tries);
    return 0;
}

/* Or a run of failures that stops short adds to the next one hours later. */
static int t_reaching_it_again_forgives_the_misses(char *note, unsigned n) {
    a_working_session();

    session_note_failure(JF_ERR_OFFLINE);
    session_note_reached();
    session_note_failure(JF_ERR_OFFLINE);

    if (session_lost()) {
        snprintf(note, n, "a miss either side of a success added up to lost");
        return 1;
    }
    snprintf(note, n, "a success between two misses clears the count");
    return 0;
}

/* This text is the whole of what the boot screen tells a viewer while it
   waits. */
static int t_every_stage_has_its_own_sentence(char *note, unsigned n) {
    static const lib_stage all[] = {LIB_STAGE_STARTING, LIB_STAGE_LINKING, LIB_STAGE_SIGNIN, LIB_STAGE_READY, LIB_STAGE_STOPPED};
    unsigned               i, j;

    for (i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        const char *s = session_stage_text(all[i]);

        if (!s || !s[0]) {
            snprintf(note, n, "stage %d has no sentence", (int)all[i]);
            return 1;
        }
        for (j = 0; j < i; j++)
            if (strcmp(s, session_stage_text(all[j])) == 0) {
                snprintf(note, n, "stages %d and %d both say \"%s\"", (int)all[j], (int)all[i], s);
                return 1;
            }
    }
    snprintf(note, n, "%u stages, %u sentences", i, i);
    return 0;
}

/* The start-up screen is all a viewer has to go on, and a mistyped password
   once read "the session is no longer valid". */
static int t_a_failed_sign_in_says_what_to_check(char *note, unsigned n) {
    static const struct {
        jf_err      e;
        const char *names;
    } CASE[] = {{JF_ERR_AUTH, "password"}, {JF_ERR_OFFLINE, "10.0.0.4:8096"}, {JF_ERR_MISSING, "port"}, {JF_ERR_SERVER, "10.0.0.4:8096"}};
    char     text[4][160];
    unsigned i, j;

    for (i = 0; i < sizeof(CASE) / sizeof(CASE[0]); i++) {
        session_sign_in_text(CASE[i].e, "10.0.0.4:8096", text[i], sizeof(text[i]));
        if (!strstr(text[i], CASE[i].names)) {
            snprintf(note, n, "%s says \"%.60s\", which does not name the %s", jf_err_text(CASE[i].e), text[i], CASE[i].names);
            return 1;
        }
        for (j = 0; j < i; j++)
            if (strcmp(text[i], text[j]) == 0) {
                snprintf(note, n, "%s and %s read the same", jf_err_text(CASE[j].e), jf_err_text(CASE[i].e));
                return 1;
            }
    }
    snprintf(note, n, "a refused password, silence, the wrong port and a server fault each name what to check");
    return 0;
}

void test_session_register(void) {
    selftest_add("session", "a server never reached is not a lost one", t_a_server_never_reached_is_not_a_lost_one);
    selftest_add("session", "a server that answers is not a lost link", t_a_server_that_answers_is_not_a_lost_link);
    selftest_add("session", "a reached server that stops answering is lost", t_a_reached_server_that_stops_answering_is_lost);
    selftest_add("session", "reaching it again forgives the misses", t_reaching_it_again_forgives_the_misses);
    selftest_add("session", "every stage has its own sentence", t_every_stage_has_its_own_sentence);
    selftest_add("session", "a failed sign-in says what to check", t_a_failed_sign_in_says_what_to_check);
}

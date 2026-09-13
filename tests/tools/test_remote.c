/* The command channel's two guarantees: a command runs once, and an ack
 * means the work is done. See tools/remote.h. */

#include "tools/remote.h"

#include "io/link.h"

#include "port/platform.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

/* The checks' own channel: pointed at cmd.txt, the application eats commands
 * the checks are waiting on, which reads as a broken channel. */
#define CHECK_CMD   "cmd-check.txt"
#define CHECK_ACK   "ack-check.txt"
#define CHECK_STATE "state-check.txt"

static int put_cmd(const char *line) { return hostfs_put(CHECK_CMD, line); }

static unsigned read_ack(void) {
    char     buf[24];
    unsigned seq = 0;

    /* The file on disk, not an ack the link has yet to write. */
    if (hostfs_slurp(CHECK_ACK, buf, sizeof(buf))) sscanf(buf, "%u", &seq);
    return seq;
}

/* The link hands a line over only when it has changed, so a fresh process
   delivers the previous run's last command first. The group passed in a full
   run and failed alone. */
static void drain(void) {
    unsigned t0 = platform_clock_us();

    while (platform_clock_us() - t0 < 400000u) {
        if (remote_take(NULL, 0) != REMOTE_NONE)
            remote_done();
        else
            platform_sleep_us(10000);
    }
}

static remote_cmd take_within(unsigned bound_us) {
    unsigned t0 = platform_clock_us();

    for (;;) {
        remote_cmd c = remote_take(NULL, 0);

        if (c != REMOTE_NONE) return c;
        if (platform_clock_us() - t0 > bound_us) return REMOTE_NONE;
        platform_sleep_us(10000);
    }
}

/* "<seq> <word>" posted on a drained channel, with the ack as it stood once
   drained in `acked` if asked: -1 when nothing drives this run, 1 when the
   post failed. */
static int post(unsigned seq, const char *word, unsigned *acked, char *note, unsigned n) {
    char line[64];

    if (!remote_on()) {
        snprintf(note, n, "the channel is off; nothing driving this run");
        return -1;
    }
    drain();
    if (acked) *acked = read_ack();

    snprintf(line, sizeof(line), "%u %s\n", seq, word);
    if (put_cmd(line) != 0) {
        snprintf(note, n, CHECK_CMD " could not be posted to the link");
        return 1;
    }
    return 0;
}

static int t_a_command_runs_once(char *note, unsigned n) {
    unsigned   seq = 900;
    remote_cmd c;
    int        rc;

    if ((rc = post(seq, "ping", 0, note, n)) != 0) return rc;

    c = take_within(2000000u);
    if (c != REMOTE_PING) {
        snprintf(note, n, "the first ping never arrived (%d)", (int)c);
        return 1;
    }
    remote_done();

    /* Unchanged, so the link does not hand it over again; "a used number is
     * refused" reaches the sequence guard behind it. */
    c = take_within(600000u);
    if (c != REMOTE_NONE) {
        snprintf(note, n, "the same number ran twice (%d)", (int)c);
        return 1;
    }
    snprintf(note, n, "%u ran once with " CHECK_CMD " unchanged", seq);
    return 0;
}

static int t_an_ack_means_complete(char *note, unsigned n) {
    unsigned   seq = 901, before = 0, t0;
    remote_cmd c;
    int        rc;

    if ((rc = post(seq, "ping", &before, note, n)) != 0) return rc;

    c = take_within(2000000u);
    if (c != REMOTE_PING) {
        snprintf(note, n, "no ping %u within 2 s", seq);
        return 1;
    }

    /* Given a pass to be wrong in, since the link writes the ack. */
    platform_sleep_us(HOSTFS_PASS_US * 4u);
    if (read_ack() != before) {
        snprintf(note, n, "acked at %u before the work was done", read_ack());
        return 1;
    }

    remote_done();
    /* The ack crosses on the link thread, so it lands a pass later. */
    t0 = platform_clock_us();
    while (read_ack() != seq && platform_clock_us() - t0 < 2000000u) platform_sleep_us(HOSTFS_PASS_US);
    if (read_ack() != seq) {
        snprintf(note, n, "after finishing, ack.txt says %u not %u", read_ack(), seq);
        return 1;
    }
    snprintf(note, n, "ack stayed at %u until done, then %u", before, seq);
    return 0;
}

/* The line changes, so only the sequence guard can stop it. */
static int t_a_used_number_is_refused(char *note, unsigned n) {
    unsigned   seq = 902;
    char       line[64];
    remote_cmd c;
    int        rc;

    if ((rc = post(seq, "ping", 0, note, n)) != 0) return rc;
    c = take_within(2000000u);
    if (c != REMOTE_PING) {
        snprintf(note, n, "the ping never arrived (%d)", (int)c);
        return 1;
    }
    remote_done();

    /* The same number, a different word: a line the link WILL hand over. */
    snprintf(line, sizeof(line), "%u status\n", seq);
    if (put_cmd(line) != 0) {
        snprintf(note, n, CHECK_CMD " could not be rewritten");
        return 1;
    }
    c = take_within(600000u);
    if (c != REMOTE_NONE) {
        snprintf(note, n, "number %u ran a second time as %d", seq, (int)c);
        return 1;
    }
    snprintf(note, n, "%u refused a second time though the line changed", seq);
    return 0;
}

/* A duplicate means a script pressing one button gets another. `select` had
   no entry at all on the desktop's copy once. */
static int t_every_button_names_its_own_bit(char *note, unsigned n) {
    static const char *NAMES[] = {"up", "down", "left", "right", "cross", "circle", "triangle", "square", "start", "select", "l", "r"};
    int                count   = (int)(sizeof(NAMES) / sizeof(NAMES[0]));
    unsigned           seen    = 0;
    int                i;

    for (i = 0; i < count; i++) {
        unsigned bit = remote_pad_named(NAMES[i]);

        if (!bit) {
            snprintf(note, n, "\"%s\" names no button", NAMES[i]);
            return 1;
        }
        if (bit & seen) {
            snprintf(note, n, "\"%s\" gives %08x, which another name already used", NAMES[i], bit);
            return 1;
        }
        seen |= bit;
    }

    if (remote_pad_named("elbow") != 0 || remote_pad_named("") != 0 || remote_pad_named(0) != 0) {
        snprintf(note, n, "a name that is not a button gave one");
        return 1;
    }
    snprintf(note, n, "%d buttons, %d distinct bits", count, count);
    return 0;
}

/* Refused in the log alone, the sender blocks on an ack that never comes and a
 * typo looks like a hung application. */
static int t_a_word_it_does_not_know_is_still_answered(char *note, unsigned n) {
    unsigned   seq = 903, t0;
    remote_cmd c;
    int        rc;

    if ((rc = post(seq, "wibble", 0, note, n)) != 0) return rc;

    /* Nothing is handed to the application, so this waits on the ack the
       refusal writes. */
    t0 = platform_clock_us();
    while (read_ack() != seq) {
        if (platform_clock_us() - t0 > 2000000u) {
            snprintf(note, n, "two seconds on, \"wibble\" has not been answered (ack is %u, wanted %u) -- the sender would wait for ever",
                     read_ack(), seq);
            return 1;
        }
        (void)remote_take(NULL, 0);
        platform_sleep_us(10000);
    }

    c = take_within(300000u);
    if (c != REMOTE_NONE) {
        snprintf(note, n, "\"wibble\" was acked and then handed over as %d", (int)c);
        return 1;
    }
    snprintf(note, n, "refused and acked at %u", seq);
    return 0;
}

void test_remote_register(void) {
    selftest_add("remote", "a command runs once", t_a_command_runs_once);
    selftest_add("remote", "every button names its own bit", t_every_button_names_its_own_bit);
    selftest_add("remote", "an ack means complete", t_an_ack_means_complete);
    /* In number order: one channel and one g_last_seq, so a high number burnt
     * early makes every lower one look already done. */
    selftest_add("remote", "a used number is refused", t_a_used_number_is_refused);
    selftest_add("remote", "a word it does not know is still answered", t_a_word_it_does_not_know_is_still_answered);
}

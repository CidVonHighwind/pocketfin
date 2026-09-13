/* See base/log.h. These read the card's file, so what passes is that the bytes
 * landed, not that a counter moved. */

#include "base/log.h"

#include "port/platform.h"
#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

static unsigned card_tail(char *buf, unsigned n) {
    FILE *f = fopen(log_card_path(), "rb");
    long  size;

    buf[0] = 0;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    if (size > (long)n - 1)
        fseek(f, size - (long)n + 1, SEEK_SET);
    else
        fseek(f, 0, SEEK_SET);
    size = (long)fread(buf, 1, n - 1, f);
    fclose(f);
    buf[size > 0 ? size : 0] = 0;
    return size > 0 ? (unsigned)size : 0u;
}

static int card_holds(const char *needle) {
    char buf[512];

    card_tail(buf, sizeof(buf));
    return strstr(buf, needle) != NULL;
}

static long stamp_of(const char *needle) {
    char        buf[1024];
    const char *at;
    unsigned    i, j, sec = 0, ms = 0;

    card_tail(buf, sizeof(buf));
    at = strstr(buf, needle);
    if (!at) return -1;

    i = (unsigned)(at - buf);
    while (i > 0 && buf[i - 1] != '\n') i--;

    if (buf[i++] != '[') return -1;
    while (buf[i] == ' ') i++;
    if (buf[i] < '0' || buf[i] > '9') return -1;
    while (buf[i] >= '0' && buf[i] <= '9') sec = sec * 10u + (unsigned)(buf[i++] - '0');
    if (buf[i++] != '.') return -1;
    for (j = 0; j < 3; j++) {
        if (buf[i] < '0' || buf[i] > '9') return -1;
        ms = ms * 10u + (unsigned)(buf[i++] - '0');
    }
    if (buf[i++] != ']') return -1;
    if (buf[i++] != ' ') return -1;
    if (buf + i != at) return -1;
    return (long)sec * 1000 + (long)ms;
}

static long file_bytes(const char *path) {
    long  size = 0;
    FILE *f    = fopen(path, "rb");

    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fclose(f);
    return size;
}

static int no_card(char *note, unsigned n) {
    if (log_card_path()[0]) return 0;
    snprintf(note, n, "no card log on this run");
    return 1;
}

static int t_a_line_reaches_the_card(char *note, unsigned n) {
    log_stats before, after;

    if (no_card(note, n)) return -1;
    log_to_card(1);
    log_get_stats(&before);
    log_line("check: a line reaches the card");
    log_flush();
    log_get_stats(&after);
    log_to_card(0);

    if (after.lines != before.lines + 1) {
        snprintf(note, n, "lines went %u -> %u, wanted one more", before.lines, after.lines);
        return 1;
    }
    if (!card_holds("a line reaches the card")) {
        snprintf(note, n, "the counter moved but %s does not hold the line", log_card_path());
        return 1;
    }
    snprintf(note, n, "the bytes are in %s", log_card_path());
    return 0;
}

static int t_a_long_line_is_cut(char *note, unsigned n) {
    char      big[LOG_LINE_MAX + 64];
    log_stats before, after;
    unsigned  i;

    for (i = 0; i < sizeof(big) - 1; i++) big[i] = 'x';
    big[sizeof(big) - 1] = 0;
    memcpy(big, "check: cut ", 11);

    log_get_stats(&before);
    log_line(big);
    log_get_stats(&after);

    if (after.truncated != before.truncated + 1) {
        snprintf(note, n, "truncated went %u -> %u, wanted one more", before.truncated, after.truncated);
        return 1;
    }
    snprintf(note, n, "%u bytes offered, cut at %d", (unsigned)sizeof(big) - 1, LOG_LINE_MAX);
    return 0;
}

static int t_a_line_carries_a_stamp(char *note, unsigned n) {
    long first, second;

    if (no_card(note, n)) return -1;
    log_to_card(1);
    log_line("check: stamp, the first");
    platform_sleep_us(20000);
    log_line("check: stamp, the second");
    log_flush();
    log_to_card(0);

    first  = stamp_of("check: stamp, the first");
    second = stamp_of("check: stamp, the second");
    if (first < 0 || second < 0) {
        snprintf(note, n, "a line reached %s with no [s.mmm] in front of it", log_card_path());
        return 1;
    }
    if (second < first) {
        snprintf(note, n, "the stamps went backwards: %ld ms then %ld ms", first, second);
        return 1;
    }
    snprintf(note, n, "stamped %ld ms and %ld ms, %ld ms apart across a 20 ms sleep", first, second, second - first);
    return 0;
}

static int t_the_file_restarts_past_the_cap(char *note, unsigned n) {
    const char *path = log_card_path();
    long        grown, after;
    FILE       *f;

    if (no_card(note, n)) return -1;
    log_to_card(1);
    log_flush();

    /* Grown from outside: the log goes on the size the file has, not on what
       it wrote. */
    f = fopen(path, "r+b");
    if (!f) {
        snprintf(note, n, "cannot open %s to grow it", path);
        log_to_card(0);
        return -1;
    }
    fseek(f, LOG_KEEP_BYTES + 16, SEEK_SET);
    fputc('\n', f);
    fclose(f);

    /* The write that finds the file oversized still appends; the one after it
       starts the file again. */
    log_line("check: cap, the last line of the old file");
    log_flush();
    grown = file_bytes(path);

    log_line("check: cap, the first line of the new file");
    log_flush();
    after = file_bytes(path);

    log_to_card(0);

    if (grown <= LOG_KEEP_BYTES) {
        snprintf(note, n, "%s is %ld bytes and never reached the %ld byte cap", path, grown, (long)LOG_KEEP_BYTES);
        return 1;
    }
    if (after >= grown) {
        snprintf(note, n, "%s went %ld -> %ld bytes past the cap: it is still growing", path, grown, after);
        return 1;
    }
    if (after <= 0) {
        snprintf(note, n, "%s restarted and holds nothing", path);
        return 1;
    }
    snprintf(note, n, "%ld bytes past the %ld byte cap, then %ld: the file started again", grown, (long)LOG_KEEP_BYTES, after);
    return 0;
}

static int t_the_card_drops_and_counts(char *note, unsigned n) {
    log_stats before, after;
    unsigned  i;

    if (no_card(note, n)) return -1;
    log_to_card(1);
    log_get_stats(&before);
    log_fault_card(100000);
    for (i = 0; i < LOG_BUF_BYTES / 24u + 200u; i++) {
        char line[64];

        snprintf(line, sizeof(line), "check: card fill %u", i);
        log_line(line);
    }
    log_fault_card(0);
    log_get_stats(&after);
    log_flush();
    log_to_card(0);
    /* Manufactured drops, or every run would end calling its transcript
       incomplete. */
    log_reset_stats();

    if (after.card_dropped <= before.card_dropped) {
        snprintf(note, n, "the card refused every write and dropped nothing");
        return 1;
    }
    snprintf(note, n, "%u dropped while the card refused", after.card_dropped - before.card_dropped);
    return 0;
}

static int t_the_card_holds_what_it_cannot_write(char *note, unsigned n) {
    int refused;

    if (no_card(note, n)) return -1;
    log_to_card(1);
    log_fault_card(100000);
    log_line("check: held across a failed write");
    log_flush();
    refused = !card_holds("held across a failed write");
    log_fault_card(0);
    log_flush();
    log_to_card(0);

    if (!refused) {
        snprintf(note, n, "every write was refused and the line is in the file anyway");
        return 1;
    }
    if (!card_holds("held across a failed write")) {
        snprintf(note, n, "the line never landed once the card came back");
        return 1;
    }
    snprintf(note, n, "held through refused writes, then landed");
    return 0;
}

/* 31 us a line on the console. The ceiling catches a card write, 18,924 us,
   landing on the caller. */
#define LINE_COST_CEILING_US 5000u

static int t_what_a_line_costs(char *note, unsigned n) {
    unsigned i, t0, each;

    log_to_card(1);
    t0 = platform_clock_us();
    for (i = 0; i < 20; i++) log_line("check: cost");
    each = (platform_clock_us() - t0) / 20u;
    log_flush();
    log_to_card(0);

    if (each > LINE_COST_CEILING_US) {
        snprintf(note, n, "a line cost its caller %u us, over the %u us ceiling", each, LINE_COST_CEILING_US);
        return 1;
    }
    snprintf(note, n, "%u us a line", each);
    return 0;
}

static int t_a_breadcrumb_reaches_the_card_with_logging_off(char *note, unsigned n) {
    log_stats before, after;

    if (no_card(note, n)) return -1;
    log_to_card(0);
    log_get_stats(&before);
    log_line("check: an ordinary line while the card is off");
    log_get_stats(&after);
    if (after.card_lines != before.card_lines) {
        snprintf(note, n, "an ordinary line reached the card with logging off");
        return 1;
    }

    log_get_stats(&before);
    log_mark("check: a breadcrumb while the card is off");
    log_get_stats(&after);
    if (after.card_lines == before.card_lines) {
        snprintf(note, n, "a breadcrumb did NOT reach the card -- a hang would leave nothing behind");
        return 1;
    }

    log_get_stats(&before);
    log_line("check: an ordinary line after the breadcrumb");
    log_get_stats(&after);
    if (after.card_lines != before.card_lines) {
        snprintf(note, n, "the breadcrumb left the card switched on");
        return 1;
    }
    snprintf(note, n, "ordinary lines held back, the breadcrumb went through");
    return 0;
}

void test_log_register(void) {
    selftest_add("log", "a line reaches the card", t_a_line_reaches_the_card);
    selftest_add("log", "a long line is cut", t_a_long_line_is_cut);
    selftest_add("log", "a line carries a stamp", t_a_line_carries_a_stamp);
    selftest_add("log", "the file restarts past the cap", t_the_file_restarts_past_the_cap);
    selftest_add("log", "the card drops and counts", t_the_card_drops_and_counts);
    selftest_add("log", "the card holds what it cannot write", t_the_card_holds_what_it_cannot_write);
    selftest_add("log", "what a line costs", t_what_a_line_costs);
    selftest_add("log", "a breadcrumb reaches the card with logging off", t_a_breadcrumb_reaches_the_card_with_logging_off);
}

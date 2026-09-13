/* One stamped line at a time, to the cable and to the card.
 *
 * A card write costs ~18,924 us against ~19 us down the cable, so the card is
 * written by the log's own thread: a file write on the caller's thread once
 * stopped the application dead. Card bytes leave the buffer only once a write
 * has landed, since a line was lost across a standby.
 *
 * Stamped from the device's clock: pspsh buffers its stream, and a load
 * observed at 31 s had happened at 2 s. */
#ifndef BASE_LOG_H
#define BASE_LOG_H

#define LOG_BUF_BYTES 8192
#define LOG_LINE_MAX  256

/* A twenty-minute film logs about 90 kB to the card, so this keeps a couple of
 * runs: a freeze and the power-cycle after it leave their evidence. */
#define LOG_KEEP_BYTES 262144L

/* `post` returns how many bytes it took; `idle` is 1 once everything posted
 * has landed, and may be NULL. Both run under the log's lock. A line with no
 * room waits a moment, then is dropped and counted. */
void log_cable(unsigned (*post)(const void *bytes, unsigned n), int (*idle)(void));

int log_open(const char *path);

/* Off by default. A line with no room on the card is dropped and counted,
 * never waited on. */
void log_to_card(int on);

void log_line(const char *text);
void log_printf(const char *fmt, ...);

/* Reaches the card whatever log_to_card says, and is waited for, so a hang
 * leaves its last step behind. */
void log_mark(const char *line);

void log_flush(void);
void log_close(void);

const char *log_card_path(void);

typedef struct {
    unsigned lines;
    unsigned truncated;
    unsigned cable_dropped;
    unsigned card_lines;
    unsigned card_dropped;
} log_stats;

void log_get_stats(log_stats *out);

#ifdef POCKETFIN_CHECKS
void log_fault_card(unsigned n); /* the next n card writes fail */
void log_reset_stats(void);
#endif

#endif

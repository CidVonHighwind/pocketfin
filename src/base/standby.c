/* See standby.h. */

#include "base/standby.h"

#include "base/log.h"

/* Said once, then it keeps trying. The loop runs about 100 times a second
 * and a healthy wake took 82 attempts; at 20 this fired on every good
 * resume. */
#define RESTORE_SAY_AT 300

static void (*g_release)(void);
static int (*g_acquire)(void);
static int (*g_verify)(void);

static volatile unsigned      g_epoch = 1;
static volatile standby_phase g_state = STANDBY_UP;

/* Set by the callback, consumed by the loop: two flags rather than a state
 * written from two threads. */
static volatile int g_going, g_back;

static unsigned g_attempts;
static int      g_acquired;

unsigned      standby_epoch(void) { return g_epoch; }
standby_phase standby_state(void) { return g_state; }

const char *standby_text(standby_phase p) {
    switch (p) {
    case STANDBY_UP: return "up";
    case STANDBY_LOSING: return "losing";
    case STANDBY_DOWN: return "down";
    case STANDBY_RESTORING: return "restoring";
    default: return "?";
    }
}

void standby_watch(void (*release)(void), int (*acquire)(void), int (*verify)(void)) {
    g_release = release;
    g_acquire = acquire;
    g_verify  = verify;
}

void standby_note_going(void) { g_going = 1; }
void standby_note_back(void) { g_back = 1; }

/* 0 when it is back AND has been observed working. */
static int restore(void) {
    if (!g_acquired) {
        /* To the card: a resume that hangs leaves no other evidence, since
         * the cable is exactly what is down. */
        log_mark("device: acquiring");
        if (g_acquire && g_acquire() != 0) return -1;
        g_acquired = 1;
    }
    return (g_verify && g_verify() != 0) ? -1 : 0;
}

void standby_step(void) {
    if (g_going) {
        g_going = 0;
        if (g_state == STANDBY_UP) {
            log_line("device: going");
            g_state = STANDBY_LOSING;
        }
    }

    switch (g_state) {
    case STANDBY_LOSING:
        g_acquired = 0;
        if (g_release) g_release();
        g_state = STANDBY_DOWN;
        log_line("device: down");
        break;

    case STANDBY_DOWN:
        if (g_back) {
            g_back     = 0;
            g_attempts = 0;
            g_state    = STANDBY_RESTORING;
            log_line("device: restoring");
        }
        break;

    case STANDBY_RESTORING:
        g_attempts++;
        if (restore() == 0) {
            /* Only here, and only once. */
            g_epoch++;
            g_state = STANDBY_UP;
            log_printf("device: up in epoch %u after %u attempt(s)", g_epoch, g_attempts);
        } else if (g_attempts == RESTORE_SAY_AT) {
            log_printf("device: %u attempts and it has not come back; still trying", g_attempts);
        }
        break;

    case STANDBY_UP:
    default:
        /* RESUME_COMPLETE with nothing lost: the machine never went. */
        g_back = 0;
        break;
    }
}

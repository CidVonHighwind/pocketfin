/* Where the viewer is, sent on a worker of its own: /Sessions/Playing/Stopped
 * takes the server 1.85 s. START, PROGRESS and STOP can mark an item watched on
 * every client, so the viewer's setting gates them; ENCODING_OFF never is, or
 * the server keeps transcoding for a client that has left. */
#ifndef MODEL_REPORTS_H
#define MODEL_REPORTS_H

#include "jelly/api.h"

typedef enum { REPORT_START = 0, REPORT_PROGRESS, REPORT_STOP, REPORT_ENCODING_OFF } play_report;

typedef struct {
    play_report        what;
    char               id[JF_ID_LEN];
    char               session[JF_SESSION_LEN];
    unsigned long long ticks;
    int                paused;
} play_job;

int  reports_start(void);
void reports_stop(void);

/* A seek stops one session and starts another in the same frame, so these
 * queue in order; only a pending PROGRESS is replaced by a later one. */
void reports_send(play_report what, const char *item_id, const char *session, unsigned long long ticks, int paused);

int reports_take(play_job *out); /* oldest first; 0 when empty */
int reports_pending(void);

#endif

/* One Jellyfin server per run. The address, login and token live here; each
 * worker brings only its own reply buffer. */
#ifndef JELLY_API_H
#define JELLY_API_H

#include "io/http.h"
#include "jelly/item.h"
#include "jelly/query.h"

#define JF_ID_LEN      40
#define JF_NAME_LEN    64
#define JF_TOKEN_LEN   48
#define JF_HOST_LEN    48
#define JF_USER_LEN    32
#define JF_PASS_LEN    32
#define JF_SESSION_LEN 64

/* 140 episodes with Overview measured 250,178 B. */
#define JF_LIST_MAX 128
#define JF_LIST_BUF (256u * 1024u)

/* TranscodeReasons has no server cap: 796 chars measured. */
#define JF_URL_LEN     1024
#define JF_HLS_URL_LEN 1200

typedef enum {
    JF_OK = 0,
    JF_ERR_OFFLINE,
    JF_ERR_AUTH, /* 401 or 403, or not signed in */
    JF_ERR_MISSING,
    JF_ERR_SERVER,
    JF_ERR_GARBLED
} jf_err;

const char *jf_err_text(jf_err e);

/* Never shared between threads. */
typedef struct {
    char     *p;
    unsigned  cap;
    http_cost cost; /* of the last request made with it */
} jf_buf;

#define JF_BUF(a) {(a), (unsigned)sizeof(a), {0, 0, 0, 0}}

typedef struct {
    char host[JF_HOST_LEN];
    int  port;
    char user[JF_USER_LEN];
    char pass[JF_PASS_LEN];
    int  profile; /* 0: ask the viewer */
} jf_conn;

/* jellyfin.txt from the data directory, else run/. 0 when a host was found. */
int jf_load_conn(jf_conn *c);
int jf_conn_parse(const char *text, jf_conn *c, const char *path);

void jf_use(const jf_conn *c);

void        jf_address(char *host, unsigned n, int *port);
const char *jf_user(void);
const char *jf_server_name(void);
int         jf_signed_in(void);

/* The only sign-in; data calls before it answer JF_ERR_AUTH. */
jf_err jf_connect(void);

/* `refused` is the header or URL that carried the refused token. Signs in
 * again only if that token is still the one in force. */
jf_err jf_reauth(const char *refused);

jf_err jf_views(jf_buf *b, item *out, int max, int *got);
jf_err jf_items(jf_buf *b, const char *parent_id, item_sort sort, int descending, item_filter filter, item *out, int max, int *got,
                int *total);
/* Evangelion has 14 subtitle tracks; past the cap they are dropped. */
#define JF_TRACK_MAX  24
#define JF_TRACK_NAME 64

typedef struct {
    int  index; /* the server's stream index */
    char name[JF_TRACK_NAME];
    char lang[4]; /* ISO 639-2, "" when untagged */
} jf_track;

typedef struct {
    jf_track audio[JF_TRACK_MAX], sub[JF_TRACK_MAX];
    int      audio_n, sub_n;
    int      audio_default; /* a stream index, -1 when none is marked */
} jf_tracks;

/* Another file's track as the stream index of the same one here: the same
 * name, else the same language, else -1. An empty name is -1. */
int jf_track_find(const jf_track *t, int n, const jf_track *want);

/* `tracks` may be null. */
jf_err jf_item(jf_buf *b, const char *id, item *out_item, char *out, unsigned outlen, jf_tracks *tracks);

/* Marking played also clears the resume point. There is no undo. */
jf_err jf_set_played(jf_buf *b, const char *id, int on);
jf_err jf_set_favorite(jf_buf *b, const char *id, int on);

jf_err jf_resume(jf_buf *b, item *out, int max, int *got);
jf_err jf_next_up(jf_buf *b, item *out, int max, int *got);
jf_err jf_latest(jf_buf *b, const char *parent_id, item *out, int max, int *got);
jf_err jf_adjacent(jf_buf *b, const char *series_id, const char *item_id, item *out, int max, int *got);

/* 1.001 s. Against 2 s: first picture 2,769 ms instead of 3,468. 0.5 s is
 * HTTP 400. */
#define JF_HLS_SEG_TICKS 10010000ull

typedef struct {
    char dir[160];
    char query[JF_URL_LEN];
    int  first_seg;
} jf_hls;

/* `audio` -1 is the server's default track, `sub` -1 is none; a subtitle is
 * burned into the picture. */
jf_err jf_hls_open(jf_buf *b, const char *item_id, uint64_t start_ticks, unsigned max_bps, int audio, int sub, jf_hls *out,
                   char *session_out, unsigned sess_len);

/* seg < 0 is the init segment. Carries the token in force now; the transcode
 * is keyed by playSessionId, so a renewed token keeps the same encoder. */
void jf_hls_url(const jf_hls *h, int seg, char *out, unsigned outlen);

/* Writes watch state to the server with no undo; gated in model/catalog.c. */
jf_err jf_report_start(jf_buf *b, const char *item_id, const char *session, uint64_t ticks);
jf_err jf_report_progress(jf_buf *b, const char *item_id, const char *session, uint64_t ticks, int paused);
jf_err jf_report_stop(jf_buf *b, const char *item_id, const char *session, uint64_t ticks);
jf_err jf_stop_encoding(jf_buf *b, const char *session);

#endif

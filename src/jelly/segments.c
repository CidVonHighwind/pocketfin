#include "jelly/segments.h"

#include "base/log.h"
#include "base/standby.h"
#include "io/http.h"
#include "port/platform.h"

#include <stdio.h>
#include <string.h>

const char *seg_result_text(seg_result r) {
    switch (r) {
    case SEG_ARRIVED: return "arrived";
    case SEG_NOT_READY: return "not encoded yet";
    case SEG_END: return "past the last segment";
    default: return "failed";
    }
}

int seg_open(segments *g, const jf_hls *h, unsigned long long run_ticks, unsigned char *stage, unsigned cap) {
    if (!g || !h || !stage || cap < SEG_STAGE_MIN) return -1;

    seg_close(g);
    memset(g, 0, sizeof(*g));
    g->pre       = HTTP_SOCK_NONE;
    g->pre_seg   = -2;
    g->hls       = *h;
    g->stage     = stage;
    g->stage_cap = cap - SEG_INIT_MAX;
    g->init      = stage + g->stage_cap;
    g->last_seg  = run_ticks ? (int)(run_ticks / JF_HLS_SEG_TICKS) : -1;
    return 0;
}

static int near_the_end(const segments *g, int index) {
    if (g->last_seg < 0) return 1;
    return index >= g->last_seg - SEG_END_SLACK;
}

seg_result seg_verdict(const segments *g, int index, int status, http_result rc, unsigned len) {
    /* Written before the first frame is encoded. */
    if (index < 0) return (rc == HTTP_OK && status == 200 && len) ? SEG_ARRIVED : SEG_FAILED;

    if (rc == HTTP_NO_REPLY) return near_the_end(g, index) ? SEG_FAILED : SEG_NOT_READY;
    if (rc != HTTP_OK) return SEG_FAILED;

    /* A 416 read as a fault ended films 19 s after a resume. */
    if (status == 404 || status == 416) return near_the_end(g, index) ? SEG_END : SEG_NOT_READY;
    if (status != 200) return SEG_FAILED;

    /* A live transcode answers an empty 200 past its own front. */
    if (!len) return near_the_end(g, index) ? SEG_END : SEG_NOT_READY;
    return SEG_ARRIVED;
}

void seg_close(segments *g) {
    if (g->pre != HTTP_SOCK_NONE) {
        http_sock_close(g->pre);
        g->pre = HTTP_SOCK_NONE;
    }
    g->pre_seg = -2;
}

static void seg_presend(segments *g, int index) {
    char path[JF_HLS_URL_LEN], host[JF_HOST_LEN], err[64];
    int  port;

    if (index < 0 || g->pre != HTTP_SOCK_NONE) return;
    jf_hls_url(&g->hls, index, path, sizeof(path));
    jf_address(host, sizeof(host), &port);
    g->pre = http_presend(host, port, path, err, sizeof(err));
    if (g->pre != HTTP_SOCK_NONE) {
        g->pre_seg   = index;
        g->pre_epoch = standby_epoch();
    }
}

typedef struct {
    segments      *g;
    unsigned char *buf;
    unsigned       cap, len;
    int            next;
    int            over;
} into;

/* Headers in, body not yet read: the server's think time for the next one. */
static void ask_next(void *user) {
    into *d = (into *)user;

    seg_presend(d->g, d->next);
}

static int collect(const unsigned char *data, unsigned len, void *user) {
    into *d = (into *)user;

    if (d->len + len > d->cap) {
        d->over = 1;
        return 1;
    }
    memcpy(d->buf + d->len, data, len);
    d->len += len;
    return 0;
}

/* `path` receives the URL asked for. */
static seg_result fetch_once(segments *g, int index, int *status, char *path) {
    into        d;
    http_result rc;
    http_sock   pre = HTTP_SOCK_NONE;
    char        host[JF_HOST_LEN];
    unsigned    t0;
    http_cost   cost;
    int         port, init = index < 0;
    seg_result  r;

    g->err[0] = 0;
    *status   = 0;
    d.g       = g;
    d.buf     = init ? g->init : g->stage;
    d.cap     = init ? SEG_INIT_MAX : g->stage_cap;
    d.len     = 0;
    d.over    = 0;
    /* The init asks nothing ahead and leaves the staged segment and the next
       one's request be. */
    d.next = (init || (g->last_seg > 0 && index + 1 > g->last_seg)) ? -1 : index + 1;

    /* A standby may have killed it without a word: reused, one held the worker
       for its 15 s read bound while the picture froze. */
    if (g->pre != HTTP_SOCK_NONE && g->pre_epoch != standby_epoch()) seg_close(g);

    if (!init && g->pre != HTTP_SOCK_NONE && g->pre_seg == index) {
        pre        = g->pre;
        g->pre     = HTTP_SOCK_NONE;
        g->pre_seg = -2;
        g->pipelined++;
    } else if (!init && g->pre != HTTP_SOCK_NONE && g->pre_seg != d.next) {
        seg_close(g);
    }

    jf_hls_url(&g->hls, index, path, JF_HLS_URL_LEN);
    jf_address(host, sizeof(host), &port);

    t0 = platform_clock_us();
    rc = http_stream(pre, host, port, path, collect, &d, ask_next, &d, status, &cost, g->err, sizeof(g->err));
    g->fetch_ms += (platform_clock_us() - t0) / 1000u;
    g->fetch_n++;
    g->ttfb_ms += cost.ttfb_us / 1000u;
    g->body_ms += cost.body_us / 1000u;

    if (d.over) {
        log_printf("segments: %d is over %u bytes -- dropped whole", index, d.cap);
        snprintf(g->err, sizeof(g->err), "segment %d does not fit", index);
        r = SEG_FAILED;
    } else {
        r = seg_verdict(g, index, *status, rc, d.len);
        if (r != SEG_ARRIVED) {
            log_printf("segments: %d -- HTTP %d, %s -> %s", index, *status, http_result_text(rc), seg_result_text(r));
            if (r == SEG_FAILED && !g->err[0])
                snprintf(g->err, sizeof(g->err), "segment %d: HTTP %d, %s", index, *status, http_result_text(rc));
        }
    }
    if (r != SEG_ARRIVED) d.len = 0;
    if (init)
        g->init_len = d.len;
    else
        g->len = d.len;
    g->bytes += d.len;
    return r;
}

seg_result seg_fetch(segments *g, int index) {
    char       url[JF_HLS_URL_LEN];
    int        status;
    seg_result r;

    /* Past the end Jellyfin keeps answering 200: 648 segments beyond a
       24-minute episode were fetched and replayed. */
    if (g->last_seg >= 0 && index > g->last_seg) return SEG_END;

    r = fetch_once(g, index, &status, url);
    /* Once: a second refusal is the credentials, not the token. */
    if (r == SEG_FAILED && (status == 401 || status == 403) && jf_reauth(url) == JF_OK) {
        log_printf("segments: %d -- refused; asking again with the token now in force", index);
        seg_close(g);
        r = fetch_once(g, index, &status, url);
    }
    return r;
}

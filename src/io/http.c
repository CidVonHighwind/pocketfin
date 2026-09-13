/* See http.h. */

#include "io/http.h"

#include "base/log.h"
#include "port/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The request line repeats the HLS query, most of a kilobyte: at 1024 the
   builder refused real segment requests. */
#define HEAD_MAX 2048

/* Syscall count is what a body costs on a 333 MHz CPU: at 1024 a megabyte
   pays four times the syscalls of 4096, and 8192 swept 4% faster than 4096.
   The buffer is on the stack, so it must stay well inside a 32 kB worker
   stack; in BSS, shared between threads, it spliced one reply into another. */
#define CHUNK 8192u

static unsigned g_timeout_ms = HTTP_TIMEOUT_MS_DEFAULT;

static volatile int g_cancel;

/* Every read in flight, not one: several workers can be blocked in recv at
   once, and with a single slot a cancel reached only the last to register
   while the others sat out their own timeouts. */
#define READS_MAX 8
/* Spelled out because NONE is -1, not 0: zero-initialised, every slot would
   read as taken by descriptor 0. */
static volatile http_sock g_reading[READS_MAX] = {HTTP_SOCK_NONE, HTTP_SOCK_NONE, HTTP_SOCK_NONE, HTTP_SOCK_NONE,
                                                 HTTP_SOCK_NONE, HTTP_SOCK_NONE, HTTP_SOCK_NONE, HTTP_SOCK_NONE};

/* ponytail: made by the first read. That is the library's sign-in, which runs
   alone before any other worker has anything to fetch. */
static platform_lock *g_reads_lock;

static void reading_on(http_sock s) {
    int i;

    if (!g_reads_lock) g_reads_lock = platform_lock_new("http");
    platform_lock_take(g_reads_lock);
    for (i = 0; i < READS_MAX; i++)
        if (g_reading[i] == HTTP_SOCK_NONE) {
            g_reading[i] = s;
            break;
        }
    platform_lock_give(g_reads_lock);
}

static void reading_off(http_sock s) {
    int i;

    platform_lock_take(g_reads_lock);
    for (i = 0; i < READS_MAX; i++)
        if (g_reading[i] == s) g_reading[i] = HTTP_SOCK_NONE;
    platform_lock_give(g_reads_lock);
}

void     http_set_timeout_ms(unsigned ms) { g_timeout_ms = ms ? ms : HTTP_TIMEOUT_MS_DEFAULT; }
unsigned http_timeout_ms(void) { return g_timeout_ms; }

void http_cancel(void) {
    int i;

    g_cancel = 1;

    /* Woken, not closed: closing frees a descriptor its owner still holds
       and will reuse. */
    for (i = 0; i < READS_MAX; i++) http_sock_wake(g_reading[i]);
}

void http_cancel_clear(void) { g_cancel = 0; }
int  http_cancelled(void) { return g_cancel; }

const char *http_result_text(http_result r) {
    switch (r) {
    case HTTP_OK: return "ok";
    case HTTP_NO_LINK: return "no link";
    case HTTP_NO_HOST: return "no host";
    case HTTP_SEND_FAILED: return "send failed";
    case HTTP_NO_REPLY: return "no reply";
    case HTTP_CUT: return "body cut short";
    case HTTP_TOO_BIG: return "reply too big";
    case HTTP_STOPPED: return "stopped";
    case HTTP_BAD_REQUEST: return "bad request";
    default: return "?";
    }
}

/* The stack takes what it has room for: a 1.3 kB POST body can go in two
   pieces. Treated as a failure, the sign-in reported "the server did not
   answer" for a request the server never saw. */
static int send_all(http_sock sock, const char *p, unsigned n) {
    while (n) {
        int sent = http_sock_send(sock, p, n);

        if (sent <= 0) return -1;
        p += sent;
        n -= (unsigned)sent;
    }
    return 0;
}

static void say(char *err, unsigned cap, const char *what) {
    if (err && cap) snprintf(err, cap, "%s", what);
}

typedef struct {
    const char *host;
    int         port;
    const char *method;
    const char *path;
    const char *extra;
    const char *body;
    /* A media parser wants an error body dropped; an API caller reads it. */
    int             drop_error_body;
    http_headers_fn on_headers;
    void           *on_headers_user;
    /* Connected and sent by http_presend(), or HTTP_SOCK_NONE. */
    http_sock presend;
} http_req;

static int build_request(char *head, unsigned cap, const char *host, int port, const char *method, const char *path, const char *extra,
                         const char *body, unsigned body_len) {
    int n, m;

    n = snprintf(head, cap,
                 "%s %s HTTP/1.1\r\n"
                 "Host: %s:%d\r\n"
                 "User-Agent: pocketfin/0.1\r\n"
                 "Accept: */*\r\n"
                 /* One exchange per socket is what makes a cut body
                    detectable at all. */
                 "Connection: close\r\n",
                 method, path, host ? host : "?", port);
    if (n < 0 || (unsigned)n >= cap) return -1;

    if (body) {
        m = snprintf(head + n, cap - (unsigned)n,
                     "Content-Type: application/json\r\n"
                     "Content-Length: %u\r\n",
                     body_len);
        if (m < 0 || (unsigned)(n + m) >= cap) return -1;
        n += m;
    }
    if (extra) {
        m = snprintf(head + n, cap - (unsigned)n, "%s", extra);
        if (m < 0 || (unsigned)(n + m) >= cap) return -1;
        n += m;
    }
    if ((unsigned)(n + 2) >= cap) return -1;
    memcpy(head + n, "\r\n", 2);
    return n + 2;
}

/* Reads to the header terminator, leaving body bytes from the same read in
   place. Returns bytes read, or -1; *timed_out says which kind of -1. */
static int read_headers(http_sock sock, char *hdr, unsigned cap, unsigned *hdr_len, int *status, int *timed_out) {
    unsigned used = 0;
    char    *end  = 0;

    while (used + 1 < cap) {
        int n = http_sock_recv(sock, hdr + used, cap - used - 1, timed_out);

        if (n <= 0) break;
        used += (unsigned)n;
        hdr[used] = 0;
        end       = strstr(hdr, "\r\n\r\n");
        if (end) break;
    }
    if (!end) return -1;

    *hdr_len = (unsigned)(end - hdr) + 4;
    *status  = 0;
    if (used > 12 && strncmp(hdr, "HTTP/1.", 7) == 0) *status = atoi(hdr + 9);
    return (int)used;
}

/* Jellyfin answers API calls and transcodes chunked and refuses Range, so the
 * framing comes off here. State persists across calls because a chunk
 * boundary falls wherever the network splits a packet. */
typedef struct {
    int      chunked;
    unsigned remaining;
    int      in_header;
    char     line[32];
    int      line_len;
    int      done;
} dechunk;

/* 0 to continue; non-zero when the sink stopped or the last chunk was seen. */
static int dechunk_feed(dechunk *d, const unsigned char *data, unsigned len, http_sink sink, void *user, int *stopped) {
    unsigned pos = 0;

    if (!d->chunked) {
        if (!len) return 0;
        if (sink(data, len, user) != 0) {
            *stopped = 1;
            return 1;
        }
        return 0;
    }

    while (pos < len && !d->done) {
        if (d->in_header) {
            char c = (char)data[pos++];

            if (c == '\n') {
                unsigned long v = strtoul(d->line, 0, 16);

                d->line_len  = 0;
                d->line[0]   = 0;
                d->remaining = (unsigned)v;
                d->in_header = 0;
                if (v == 0) {
                    d->done = 1;
                    return 1;
                }
            } else if (c != '\r' && d->line_len < (int)sizeof(d->line) - 1) {
                d->line[d->line_len++] = c;
                d->line[d->line_len]   = 0;
            }
            continue;
        }

        if (d->remaining == 0) {
            if (data[pos] == '\r' || data[pos] == '\n') {
                pos++;
                continue;
            }
            d->in_header = 1;
            continue;
        }

        {
            unsigned n = len - pos;

            if (n > d->remaining) n = d->remaining;
            if (sink(data + pos, n, user) != 0) {
                *stopped = 1;
                return 1;
            }
            pos += n;
            d->remaining -= n;
        }
    }
    return 0;
}

/* Jellyfin answers a 404 with a page of HTML, and a parser must not be fed
   it. */
static int drop_sink(const unsigned char *data, unsigned len, void *user) {
    (void)data;
    (void)len;
    (void)user;
    return 0;
}

static http_result exec_request(const http_req *q, http_sink sink, void *user, int *status_out, http_cost *cost, char *err,
                                unsigned errlen) {
    unsigned char buf[CHUNK];
    char          head[HEAD_MAX], hdr[HEAD_MAX];
    unsigned      hdr_len = 0, want = 0, got = 0;
    http_sock     sock;
    int           head_len, total, timed_out = 0, stopped = 0, status = 0;
    http_result   why = HTTP_OK;
    dechunk       dc;
    unsigned      body_at = 0;
    const char   *cl;
    http_cost     none;

    if (status_out) *status_out = 0;
    if (!cost) cost = &none;
    memset(cost, 0, sizeof(*cost));
    memset(&dc, 0, sizeof(dc));

    if (g_cancel) {
        say(err, errlen, "cancelled");
        if (q->presend != HTTP_SOCK_NONE) http_sock_close(q->presend);
        return HTTP_STOPPED;
    }

    head_len =
        build_request(head, sizeof(head), q->host, q->port, q->method, q->path, q->extra, q->body, q->body ? (unsigned)strlen(q->body) : 0);
    if (head_len < 0) {
        say(err, errlen, "the request would not fit");
        /* A presend socket handed over is this function's to close on every
           return, or it leaks a descriptor per request. */
        if (q->presend != HTTP_SOCK_NONE) http_sock_close(q->presend);
        return HTTP_BAD_REQUEST;
    }

    if (q->presend != HTTP_SOCK_NONE) {
        sock = q->presend;
    } else {
        unsigned t0 = platform_clock_us();

        sock             = http_sock_open(q->host, q->port, &why, err, errlen);
        cost->connect_us = platform_clock_us() - t0;
        if (sock == HTTP_SOCK_NONE) return why;

        /* Logged where the request is sent, not in build_request(): a
           pipelined segment's head is built and thrown away, and the log
           showed every one of them going out twice. */
        log_printf("http: %s %.90s", q->method, q->path ? q->path : "?");
        if (send_all(sock, head, (unsigned)head_len) != 0) {
            say(err, errlen, "the request could not be sent");
            http_sock_close(sock);
            return HTTP_SEND_FAILED;
        }
        if (q->body && q->body[0]) {
            if (send_all(sock, q->body, (unsigned)strlen(q->body)) != 0) {
                say(err, errlen, "the body could not be sent");
                http_sock_close(sock);
                return HTTP_SEND_FAILED;
            }
        }
    }

    /* Registered only now: a cancel reaching a descriptor that is still
       being connected would wake nothing. */
    reading_on(sock);
    {
        unsigned t0 = platform_clock_us();

        total         = read_headers(sock, hdr, sizeof(hdr), &hdr_len, &status, &timed_out);
        cost->ttfb_us = platform_clock_us() - t0;
        body_at       = platform_clock_us();
    }

    if (total >= 0 && q->on_headers && status >= 200 && status < 300) q->on_headers(q->on_headers_user);

    if (total < 0) {
        say(err, errlen, timed_out ? "the server sent nothing in time" : "the connection ended before any reply");
        reading_off(sock);
        http_sock_close(sock);
        return HTTP_NO_REPLY;
    }
    if (status_out) *status_out = status;

    if (q->drop_error_body && status != 200 && status != 206) {
        sink = drop_sink;
        user = 0;
    }

    /* Searched within the headers only: the recv that finished them usually
       carried the start of the body, and a body beginning with
       "Content-Length:" framed itself wrong. */
    {
        char saved = hdr[hdr_len];

        hdr[hdr_len] = 0;
        /* Two spellings are enough for Jellyfin; one this build does not
           recognise leaves the framing in, which the next reader notices at
           once. */
        if (strstr(hdr, "Transfer-Encoding: chunked") || strstr(hdr, "transfer-encoding: chunked")) dc.chunked = 1;

        /* Both together is a malformed reply, and believing the length over
           the framing truncates it. */
        if (!dc.chunked) {
            cl = strstr(hdr, "Content-Length:");
            if (!cl) cl = strstr(hdr, "content-length:");
            if (cl) want = (unsigned)strtoul(cl + 15, 0, 10);
        }
        hdr[hdr_len] = saved;
    }

    if ((unsigned)total > hdr_len) {
        unsigned n = (unsigned)total - hdr_len;

        got += n;
        if (dechunk_feed(&dc, (const unsigned char *)hdr + hdr_len, n, sink, user, &stopped) != 0) goto done;
    }

    for (;;) {
        int n;

        if (g_cancel) {
            stopped = 1;
            break;
        }
        n = http_sock_recv(sock, buf, sizeof(buf), &timed_out);
        if (n <= 0) break;
        got += (unsigned)n;
        if (dechunk_feed(&dc, buf, (unsigned)n, sink, user, &stopped) != 0) break;
    }

done:
    cost->body_us = platform_clock_us() - body_at;
    cost->bytes   = got;
    reading_off(sock);
    http_sock_close(sock);

    if (stopped && g_cancel) {
        say(err, errlen, "cancelled");
        return HTTP_STOPPED;
    }
    if (stopped) {
        say(err, errlen, "stopped");
        return HTTP_STOPPED;
    }

    /* A chunked body ends with the zero chunk, a measured one at
       Content-Length. Without both checks a connection dropped mid-file reads
       as a short file. */
    if (dc.chunked && !dc.done) {
        say(err, errlen, "the chunked body ended without its last chunk");
        return HTTP_CUT;
    }
    if (!dc.chunked && want && got < want) {
        say(err, errlen, "the body ended early");
        return HTTP_CUT;
    }
    return HTTP_OK;
}

typedef struct {
    char    *buf;
    unsigned cap, len;
    int      overflowed;
} collect;

static int collect_sink(const unsigned char *data, unsigned len, void *user) {
    collect *c = (collect *)user;

    if (c->len + len >= c->cap) {
        c->overflowed = 1;
        return 1; /* a truncated reply is worse than none */
    }
    memcpy(c->buf + c->len, data, len);
    c->len += len;
    c->buf[c->len] = 0;
    return 0;
}

http_result http_request(const char *host, int port, const char *method, const char *path, const char *extra_headers, const char *body,
                         char *buf, unsigned cap, http_resp *out) {
    collect     c;
    http_req    q;
    http_result r;
    int         status = 0;

    if (!buf || cap < 2 || !out) return HTTP_BAD_REQUEST;

    memset(out, 0, sizeof(*out));
    c.buf        = buf;
    c.cap        = cap;
    c.len        = 0;
    c.overflowed = 0;
    buf[0]       = 0;

    memset(&q, 0, sizeof(q));
    q.host   = host;
    q.port   = port;
    q.method = method;
    q.path   = path;
    q.extra  = extra_headers;
    q.body   = body;
    /* The server's message is the only account of why it refused. */
    q.drop_error_body = 0;
    q.presend         = HTTP_SOCK_NONE;

    r = exec_request(&q, collect_sink, &c, &status, &out->cost, out->err, sizeof(out->err));

    out->status = status;
    out->body   = buf;

    /* Reported as an overflow, not the stop it was made with: a caller told
       "stopped" would keep what arrived. */
    if (c.overflowed) {
        snprintf(out->err, sizeof(out->err), "the reply is larger than the %u bytes given", cap);
        return HTTP_TOO_BIG;
    }
    return r;
}

http_result http_stream(http_sock pre, const char *host, int port, const char *path, http_sink sink, void *user, http_headers_fn on_headers,
                        void *hdr_user, int *status_out, http_cost *cost, char *err, unsigned errlen) {
    http_req q;

    if (!sink) {
        if (pre != HTTP_SOCK_NONE) http_sock_close(pre);
        return HTTP_BAD_REQUEST;
    }
    if (err && errlen) err[0] = 0;

    memset(&q, 0, sizeof(q));
    q.host            = host;
    q.port            = port;
    q.method          = "GET";
    q.path            = path;
    q.drop_error_body = 1;
    q.presend         = pre;
    q.on_headers      = on_headers;
    q.on_headers_user = hdr_user;

    return exec_request(&q, sink, user, status_out, cost, err, errlen);
}

http_sock http_presend(const char *host, int port, const char *path, char *err, unsigned errlen) {
    char        head[HEAD_MAX];
    int         head_len;
    http_sock   sock;
    http_result why = HTTP_OK;

    head_len = build_request(head, sizeof(head), host, port, "GET", path, 0, 0, 0);
    if (head_len < 0) return HTTP_SOCK_NONE;

    sock = http_sock_open(host, port, &why, err, errlen);
    if (sock == HTTP_SOCK_NONE) return HTTP_SOCK_NONE;

    log_printf("http: GET %.90s (early)", path ? path : "?");
    if (send_all(sock, head, (unsigned)head_len) != 0) {
        /* Not reading_off(): this runs inside another request's headers
           callback, and would clear that read's registration. */
        http_sock_close(sock);
        return HTTP_SOCK_NONE;
    }
    return sock;
}

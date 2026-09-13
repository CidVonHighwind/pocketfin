/* Plain HTTP: no TLS stack is reachable from user mode. Requests block, so
 * none may run on the drawing thread. */
#ifndef IO_HTTP_H
#define IO_HTTP_H

typedef enum {
    HTTP_OK = 0, /* a reply arrived; read its status */
    HTTP_NO_LINK,
    HTTP_NO_HOST,
    HTTP_SEND_FAILED,
    HTTP_NO_REPLY,
    HTTP_CUT,
    HTTP_TOO_BIG,
    HTTP_STOPPED, /* by the sink or a cancel; not a failure */
    HTTP_BAD_REQUEST
} http_result;

const char *http_result_text(http_result r);

/* Per read, not per exchange. 0 restores the default. */
#define HTTP_TIMEOUT_MS_DEFAULT 15000
void     http_set_timeout_ms(unsigned ms);
unsigned http_timeout_ms(void);

/* From any thread. Stays set until cleared, so a request started after the
 * cancel cannot miss it. */
void http_cancel(void);
void http_cancel_clear(void);
int  http_cancelled(void);

typedef struct {
    unsigned connect_us, ttfb_us, body_us, bytes;
} http_cost;

typedef struct {
    int       status; /* 0 when nothing replied */
    char     *body;   /* points into the caller's buffer */
    char      err[96];
    http_cost cost;
} http_resp;

/* `extra_headers` must be whole CRLF-terminated lines. A reply that does not
 * fit `buf` is HTTP_TOO_BIG, never truncated: a cut JSON body still parses. */
http_result http_request(const char *host, int port, const char *method, const char *path, const char *extra_headers, const char *body,
                         char *buf, unsigned cap, http_resp *out);

/* Non-zero stops. An error body never reaches the sink, but `status_out` is
 * still set: a 404 past the last segment is how a film ends. */
typedef int (*http_sink)(const unsigned char *data, unsigned len, void *user);

typedef long http_sock;
#define HTTP_SOCK_NONE ((http_sock) - 1)

/* After a 2xx reply's headers, before its body: the moment to send the next
 * request. Sent earlier, the server answered both with the same bytes. */
typedef void (*http_headers_fn)(void *user);

/* Sends without reading, so the server thinks while the caller reads
 * something else: twelve posters went from 325 ms to 167 ms. The socket goes
 * to http_stream(), or to http_sock_close() unread. */
http_sock http_presend(const char *host, int port, const char *path, char *err, unsigned errlen);

/* A streaming GET. `pre` may be HTTP_SOCK_NONE, `on_headers` and `cost` NULL. */
http_result http_stream(http_sock pre, const char *host, int port, const char *path, http_sink sink, void *user, http_headers_fn on_headers,
                        void *hdr_user, int *status_out, http_cost *cost, char *err, unsigned errlen);

/* Supplied by each port. */
http_sock http_sock_open(const char *host, int port, http_result *why, char *err, unsigned errlen);

/* recv returns 0 on a clean close, and sets *timed_out when a held-open
 * connection sent nothing, as a live transcode does before a segment exists. */
int  http_sock_send(http_sock s, const void *buf, unsigned len);
int  http_sock_recv(http_sock s, void *buf, unsigned len, int *timed_out);
void http_sock_close(http_sock s);

/* Ends a read blocked in another thread without freeing the descriptor. A
 * flag is not enough on the console, whose recv blocks in the kernel. */
void http_sock_wake(http_sock s);

#endif

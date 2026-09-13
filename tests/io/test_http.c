/* See io/http.h. Against a loopback server of this file's own: a check that
 * needs a Jellyfin up fails on the server's state, and only a server built for
 * it splits a chunk at an awkward boundary on demand. Desktop only: the
 * console has no loopback, and runs the same io/http.c over its own sockets. */

#ifndef __PSP__

#include "io/http.h"
#include "io/net.h"
#include "base/standby.h"

#include "port/platform.h"
#include "tools/selftest.h"

#include "test_http.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#define CLOSESOCK closesocket
typedef SOCKET tsock;
#define BADSOCK INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSESOCK close
typedef int tsock;
#define BADSOCK   (-1)
#endif

/* A whole fMP4 fragment: the chain check serves one, and a segment with sound
   in it is tens of kilobytes. */
static char         g_reply[48 * 1024];
static unsigned     g_reply_len;
static unsigned     g_dribble;
static int          g_port;
static volatile int g_served;
/* A listener already gone costs a pipelined fetcher a whole connect timeout. */
static int g_times = 1;

/* A static, not the thread's void*: a SOCKET is 64 bits here and a long is 32. */
static tsock g_listen;

static int serve(void *arg) {
    tsock listen_s = g_listen, c;
    int   left     = g_times;

    (void)arg;

    while (left-- > 0) {
        c = accept(listen_s, 0, 0);
        if (c != BADSOCK) {
            char     scratch[512];
            unsigned sent = 0;

            (void)recv(c, scratch, sizeof(scratch), 0);

            while (sent < g_reply_len) {
                unsigned n = g_reply_len - sent;

                if (g_dribble && n > g_dribble) n = g_dribble;
                if (send(c, g_reply + sent, (int)n, 0) <= 0) break;
                sent += n;
                if (g_dribble) platform_sleep_us(2000);
            }
            CLOSESOCK(c);
        }
    }
    CLOSESOCK(listen_s);
    g_served = 1;
    return 0;
}

int fake_http_start(const char *reply, unsigned len, unsigned dribble) { return fake_http_start_n(reply, len, dribble, 1); }

int fake_http_start_n(const char *reply, unsigned len, unsigned dribble, int times) {
    struct sockaddr_in a;
    tsock              s;
    int                namelen = (int)sizeof(a);

    if (len > sizeof(g_reply)) return 0;
    g_times = times < 1 ? 1 : times;
    memcpy(g_reply, reply, len);
    g_reply_len = len;
    g_dribble   = dribble;
    g_served    = 0;

    if (net_start() != 0) return 0;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == BADSOCK) return 0;

    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(0x7F000001);
    a.sin_port        = 0; /* the machine picks */
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s, 1) != 0) {
        CLOSESOCK(s);
        return 0;
    }
    if (getsockname(s, (struct sockaddr *)&a, &namelen) != 0) {
        CLOSESOCK(s);
        return 0;
    }
    g_port = ntohs(a.sin_port);

    g_listen = s;
    if (platform_thread_start("httpsrv", serve, 0) != 0) {
        CLOSESOCK(s);
        return 0;
    }
    return 1;
}

int fake_http_port(void) { return g_port; }

void fake_http_wait(void) {
    unsigned t0 = platform_clock_us();

    while (!g_served && platform_clock_us() - t0 < 3000000u) platform_sleep_us(2000);
}

/* One request against a server that answers `reply`. 0 when no server started. */
static int ask(const char *reply, unsigned dribble, char *buf, unsigned cap, http_resp *r, http_result *rc) {
    if (!fake_http_start(reply, (unsigned)strlen(reply), dribble)) return 0;
    *rc = http_request("127.0.0.1", fake_http_port(), "GET", "/x", 0, 0, buf, cap, r);
    fake_http_wait();
    return 1;
}

#define R200_PLAIN "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\nhello there"

#define R200_CHUNKED                                        \
    "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" \
    "5\r\nhello\r\n1\r\n \r\n5\r\nthere\r\n0\r\n\r\n"

#define NO_SERVER "could not start a server"

static int t_a_measured_body_arrives_whole(char *note, unsigned n) {
    char        buf[256];
    http_resp   r;
    http_result rc;

    if (!ask(R200_PLAIN, 0, buf, sizeof(buf), &r, &rc)) {
        snprintf(note, n, NO_SERVER);
        return -1;
    }
    if (rc != HTTP_OK) {
        snprintf(note, n, "%s: %s", http_result_text(rc), r.err);
        return 1;
    }
    if (r.status != 200) {
        snprintf(note, n, "status %d", r.status);
        return 1;
    }
    if (strcmp(r.body, "hello there") != 0) {
        snprintf(note, n, "body \"%s\"", r.body);
        return 1;
    }
    snprintf(note, n, "200, 11 bytes, exact");
    return 0;
}

/* The framing comes off. Left on, the size lines reach a JSON reader and an
   MP4 parser as though they were content. */
static int t_a_chunked_body_loses_its_framing(char *note, unsigned n) {
    char        buf[256];
    http_resp   r;
    http_result rc;

    if (!ask(R200_CHUNKED, 0, buf, sizeof(buf), &r, &rc)) {
        snprintf(note, n, NO_SERVER);
        return -1;
    }
    if (rc != HTTP_OK) {
        snprintf(note, n, "%s: %s", http_result_text(rc), r.err);
        return 1;
    }
    if (strcmp(r.body, "hello there") != 0) {
        snprintf(note, n, "body came out \"%s\" -- the size lines are still in it", r.body);
        return 1;
    }
    snprintf(note, n, "three chunks became \"%s\"", r.body);
    return 0;
}

/* One byte at a time is the only split that exercises every branch of the
   header scanner. */
static int t_a_chunked_body_survives_being_split(char *note, unsigned n) {
    char        buf[256];
    http_resp   r;
    http_result rc;

    if (!ask(R200_CHUNKED, 1, buf, sizeof(buf), &r, &rc)) {
        snprintf(note, n, NO_SERVER);
        return -1;
    }
    if (rc != HTTP_OK) {
        snprintf(note, n, "%s: %s", http_result_text(rc), r.err);
        return 1;
    }
    if (strcmp(r.body, "hello there") != 0) {
        snprintf(note, n, "one byte at a time gave \"%s\"", r.body);
        return 1;
    }
    snprintf(note, n, "one byte at a time, still \"%s\"", r.body);
    return 0;
}

/* A poster cut short still decodes: a 27 kB image arrived as 2047 bytes and
   passed every check that looked at it. */
static int t_a_cut_measured_body_is_reported(char *note, unsigned n) {
    char        buf[256];
    http_resp   r;
    http_result rc;

    if (!ask("HTTP/1.1 200 OK\r\nContent-Length: 40\r\n\r\nonly this much", 0, buf, sizeof(buf), &r, &rc)) {
        snprintf(note, n, NO_SERVER);
        return -1;
    }
    if (rc != HTTP_CUT) {
        snprintf(note, n, "40 bytes promised, 14 sent, and it said %s", http_result_text(rc));
        return 1;
    }
    snprintf(note, n, "short body reported as \"%s\"", r.err);
    return 0;
}

/* A chunked body ends at its ZERO chunk, however many bytes arrived. */
static int t_a_chunked_body_without_its_last_chunk_is_reported(char *note, unsigned n) {
    char        buf[256];
    http_resp   r;
    http_result rc;

    if (!ask("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n", 0, buf, sizeof(buf), &r, &rc)) {
        snprintf(note, n, NO_SERVER);
        return -1;
    }
    if (rc != HTTP_CUT) {
        snprintf(note, n, "a chunked body with no zero chunk said %s", http_result_text(rc));
        return 1;
    }
    return 0;
}

/* Nothing here grows the caller's buffer: on a heap whose largest free run is
   about a megabyte, a realloc is a failure waiting for a bad moment. */
static int t_a_reply_that_does_not_fit_says_so(char *note, unsigned n) {
    char        buf[8];
    http_resp   r;
    http_result rc;

    if (!ask(R200_PLAIN, 0, buf, sizeof(buf), &r, &rc)) {
        snprintf(note, n, NO_SERVER);
        return -1;
    }
    if (rc != HTTP_TOO_BIG) {
        snprintf(note, n, "11 bytes into an 8 byte buffer said %s", http_result_text(rc));
        return 1;
    }
    snprintf(note, n, "%s", r.err);
    return 0;
}

/* A 404 is an answer, not a failure: past the last segment a server says 404,
   and that is how a film ends. */
static int t_a_404_is_reported_as_a_status(char *note, unsigned n) {
    char        buf[128];
    http_resp   r;
    http_result rc;

    if (!ask("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n", 0, buf, sizeof(buf), &r, &rc)) {
        snprintf(note, n, NO_SERVER);
        return -1;
    }
    if (rc != HTTP_OK) {
        snprintf(note, n, "a 404 came back as %s -- it is a status, not a fault", http_result_text(rc));
        return 1;
    }
    if (r.status != 404) {
        snprintf(note, n, "status read as %d", r.status);
        return 1;
    }
    return 0;
}

/* Refusing a dead port takes the platform's own time, 2.03 s on Windows, so
   these set their own bound, and the bound is what is checked. */
#define DEAD_PORT_MS 300u

/* Four failures that would otherwise be identical are why the result is an
   enum. */
static int t_nothing_listening_is_its_own_answer(char *note, unsigned n) {
    char        buf[64];
    http_resp   r;
    http_result rc;

    if (net_start() != 0) {
        snprintf(note, n, "no stack");
        return -1;
    }
    http_set_timeout_ms(DEAD_PORT_MS);
    /* A port nothing is on. Not zero, which some stacks treat specially. */
    rc = http_request("127.0.0.1", 9, "GET", "/x", 0, 0, buf, sizeof(buf), &r);
    http_set_timeout_ms(0);

    if (rc != HTTP_NO_HOST) {
        snprintf(note, n, "a closed port said %s", http_result_text(rc));
        return 1;
    }
    snprintf(note, n, "%s", r.err);
    return 0;
}

/* The player cancels from the teardown thread while another sits in a read; a
   cancel that expired on its own would be missed by the next request and hang
   the teardown. */
static int t_a_cancel_latches_until_it_is_cleared(char *note, unsigned n) {
    char        buf[256];
    http_resp   r;
    http_result rc;
    unsigned    t0, cost;

    if (net_start() != 0) {
        snprintf(note, n, "no stack");
        return -1;
    }

    http_cancel();
    if (!http_cancelled()) {
        snprintf(note, n, "http_cancel() did not set the flag");
        return 1;
    }

    t0   = platform_clock_us();
    rc   = http_request("127.0.0.1", 9, "GET", "/x", 0, 0, buf, sizeof(buf), &r);
    cost = platform_clock_us() - t0;
    if (rc != HTTP_STOPPED) {
        http_cancel_clear();
        snprintf(note, n, "a cancelled request said %s", http_result_text(rc));
        return 1;
    }
    /* A connect to a dead port is 2 s on this machine. */
    if (cost > 100000u) {
        http_cancel_clear();
        snprintf(note, n, "a cancelled request cost %u us -- it opened a socket", cost);
        return 1;
    }
    if (!http_cancelled()) {
        snprintf(note, n, "the cancel cleared itself when a request saw it");
        return 1;
    }

    http_cancel_clear();
    if (http_cancelled()) {
        snprintf(note, n, "http_cancel_clear() left the flag set");
        return 1;
    }
    if (!ask(R200_PLAIN, 0, buf, sizeof(buf), &r, &rc)) {
        snprintf(note, n, NO_SERVER);
        return -1;
    }
    if (rc != HTTP_OK) {
        snprintf(note, n, "after clearing, a good request said %s", http_result_text(rc));
        return 1;
    }

    snprintf(note, n, "refused in %u us, latched, then cleared", cost);
    return 0;
}

/* A standby as the loop sees it, with nothing to release or restore. */
static void sleep_once(void) {
    int i;

    standby_watch(0, 0, 0);
    standby_note_going();
    standby_step();
    standby_note_back();
    for (i = 0; i < 4 && standby_state() != STANDBY_UP; i++) standby_step();
}

/* After a sleep the radio has been off, and the association describes a world
   that no longer exists. */
static int t_a_link_from_an_older_generation_is_stale(char *note, unsigned n) {
    char ip[24];

    if (net_start() != 0) {
        snprintf(note, n, "no stack");
        return -1;
    }
    if (net_connect(1, ip, sizeof(ip)) != 0) {
        snprintf(note, n, "could not join");
        return 1;
    }
    if (net_state() != NET_UP) {
        snprintf(note, n, "joined and the state is %s", net_state_text(net_state()));
        return 1;
    }

    sleep_once();
    if (net_state() == NET_UP) {
        snprintf(note, n, "a link from before a standby is still UP");
        return 1;
    }

    /* A check that slept nothing needs the association accepted as it is. */
    if (net_revalidate() != 0 || net_state() != NET_UP) {
        snprintf(note, n, "revalidating left it %s", net_state_text(net_state()));
        return 1;
    }
    snprintf(note, n, "stale across a standby, current again after revalidate");
    net_disconnect();
    return 0;
}

/* The difference between "the radio came back" and "we never had one". */
static int t_revalidating_nothing_fails(char *note, unsigned n) {
    net_disconnect();
    if (net_revalidate() == 0) {
        snprintf(note, n, "revalidated a link that was never made");
        return 1;
    }
    return 0;
}

/* The connect takes the caller's bound too: a blocking connect() let the kernel
   pick, 2.03 s to refuse a loopback port here and minutes for a filtered one.
   Loopback with nothing on it is the one dead host a check can rely on. */
static int t_a_refusal_takes_the_bound_it_was_given(char *note, unsigned n) {
    char        buf[64];
    http_resp   r;
    http_result rc;
    unsigned    t0, cost;

    if (net_start() != 0) {
        snprintf(note, n, "no stack");
        return -1;
    }
    net_disconnect();

    http_set_timeout_ms(DEAD_PORT_MS);
    t0   = platform_clock_us();
    rc   = http_request("127.0.0.1", 9, "GET", "/x", 0, 0, buf, sizeof(buf), &r);
    cost = platform_clock_us() - t0;
    http_set_timeout_ms(0);

    if (rc == HTTP_OK) {
        snprintf(note, n, "a request with nothing listening succeeded");
        return 1;
    }
    /* Three times: a ceiling on the wait, not a stopwatch on the scheduler. */
    if (cost > DEAD_PORT_MS * 3000u) {
        snprintf(note, n, "refusing cost %u us against a %u ms bound", cost, DEAD_PORT_MS);
        return 1;
    }
    snprintf(note, n, "%s in %u us, bound %u ms", http_result_text(rc), cost, DEAD_PORT_MS);
    return 0;
}

#endif /* !__PSP__ */

void test_http_register(void) {
#ifndef __PSP__
    selftest_add("http", "a measured body arrives whole", t_a_measured_body_arrives_whole);
    selftest_add("http", "a chunked body loses its framing", t_a_chunked_body_loses_its_framing);
    selftest_add("http", "a chunked body survives being split", t_a_chunked_body_survives_being_split);
    selftest_add("http", "a cut measured body is reported", t_a_cut_measured_body_is_reported);
    selftest_add("http", "a chunked body without its last chunk is reported", t_a_chunked_body_without_its_last_chunk_is_reported);
    selftest_add("http", "a reply that does not fit says so", t_a_reply_that_does_not_fit_says_so);
    selftest_add("http", "a 404 is reported as a status", t_a_404_is_reported_as_a_status);
    selftest_add("http", "nothing listening is its own answer", t_nothing_listening_is_its_own_answer);
    selftest_add("http", "a cancel latches until it is cleared", t_a_cancel_latches_until_it_is_cleared);
    selftest_add("http", "a link from an older generation is stale", t_a_link_from_an_older_generation_is_stale);
    selftest_add("http", "revalidating nothing fails", t_revalidating_nothing_fails);
    selftest_add("http", "a refusal takes the bound it was given", t_a_refusal_takes_the_bound_it_was_given);
#endif
}

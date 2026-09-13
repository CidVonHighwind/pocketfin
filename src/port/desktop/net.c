/* net.h and http.h's socket seam for the desktop: no radio and no profile, so
 * the link is up before anything asks. */

#include "io/http.h"
#include "io/net.h"
#include "io/net_private.h"

#include "base/log.h"
#include "port/platform.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_up;

int net_start(void) {
    WSADATA w;

    if (g_up) return 0;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) {
        log_line("net: winsock would not start");
        return -1;
    }
    g_up = 1;
    return 0;
}

void net_stop(void) {
    if (!g_up) return;
    net_note_left();
    WSACleanup();
    g_up = 0;
}

int net_connect(int profile, char *ip, unsigned iplen) {
    if (!g_up && net_start() != 0) return -1;
    if (ip && iplen) snprintf(ip, iplen, "127.0.0.1");
    net_note_joined("127.0.0.1", profile > 0 ? profile : 1);
    return 0;
}

void net_disconnect(void) { net_note_left(); }

net_state_t net_state(void) {
    if (!g_up) return NET_OFF;
    return net_link_is_current() ? NET_UP : NET_DOWN;
}

net_state_t net_state_last(void) { return net_state(); }

/* The console's link on a machine without one: 1 is the console, 8 makes the
 * order things arrive in easy to watch. Measured on the console: 2.8 Mbit/s,
 * and 55-107 ms of server think time before a first byte.
 *
 * Think time is charged on the first byte read, not the connect: charged there,
 * http_presend() paid it on the same thread, serialising the three requests
 * artwork keeps in flight, and a poster looked 640 ms expensive at 8x where the
 * console pays 80 ms once for three. */
#define SLOW_BYTES_PER_S 350000 /* 2.8 Mbit/s */
#define SLOW_THINK_US    80000

static int g_slow = -1; /* -1 until asked for or read from the environment */

static void slow_say(int n) {
    if (n > 0)
        log_printf("net: pretending to be %dx slower than the console -- %d B/s, %u us before a first byte", n, SLOW_BYTES_PER_S / n,
                   (unsigned)SLOW_THINK_US * n);
    else
        log_line("net: full speed");
}

void net_pretend_slow(int times) {
    g_slow = times > 0 ? times : 0;
    slow_say(g_slow);
}

/* POCKETFIN_SLOW, for a script that cannot pass an argument. */
static int slow_factor(void) {
    if (g_slow < 0) {
        const char *s = getenv("POCKETFIN_SLOW");

        g_slow = s ? atoi(s) : 0;
        if (g_slow < 0) g_slow = 0;
        slow_say(g_slow);
    }
    return g_slow;
}

/* When each open socket sent its request, so its first byte waits only for the
   think time left: a server thinks about several requests at once, and charging
   each the full amount turned pipelining back into a queue. */
#define SLOW_PENDING 8

static struct {
    SOCKET   s;
    unsigned at_us;
} g_unread[SLOW_PENDING];

static void slow_note_open(SOCKET s) {
    int i;

    for (i = 0; i < SLOW_PENDING; i++)
        if (!g_unread[i].s) {
            g_unread[i].s     = s;
            g_unread[i].at_us = platform_clock_us();
            return;
        }
}

static unsigned slow_think_left(SOCKET s) {
    unsigned owed = SLOW_THINK_US * (unsigned)slow_factor();
    unsigned gone;
    int      i;

    for (i = 0; i < SLOW_PENDING; i++) {
        if (g_unread[i].s != s) continue;
        gone          = platform_clock_us() - g_unread[i].at_us;
        g_unread[i].s = 0;
        return gone >= owed ? 0 : owed - gone;
    }
    return 0;
}

static void slow_forget(SOCKET s) {
    int i;

    for (i = 0; i < SLOW_PENDING; i++)
        if (g_unread[i].s == s) g_unread[i].s = 0;
}

/* Everything the throttle charged, so traffic other than artwork cannot hide
   behind artwork's own accounting. */
static unsigned g_conns, g_bytes, g_slept_us;

static void slow_report(void) {
    static unsigned said_us, said_conns;
    unsigned        now = platform_clock_us();

    if (now - said_us < 1000000u) return;
    said_us = now;
    if (g_conns == said_conns) return;
    said_conns = g_conns;
    log_printf("net: %u connections, %u KB, %u ms of pretending", g_conns, g_bytes / 1024u, g_slept_us / 1000u);
}

http_sock http_sock_open(const char *host, int port, http_result *why, char *err, unsigned errlen) {
    struct addrinfo hints, *res = 0;
    char            service[16];
    SOCKET          s;
    unsigned        ms = http_timeout_ms();

    if (why) *why = HTTP_NO_HOST;
    if (!g_up && net_start() != 0) {
        if (why) *why = HTTP_NO_LINK;
        if (err && errlen) snprintf(err, errlen, "no network stack");
        return HTTP_SOCK_NONE;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(service, sizeof(service), "%d", port);

    if (getaddrinfo(host, service, &hints, &res) != 0 || !res) {
        if (err && errlen) snprintf(err, errlen, "%s does not resolve", host ? host : "?");
        return HTTP_SOCK_NONE;
    }

    s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) {
        freeaddrinfo(res);
        if (err && errlen) snprintf(err, errlen, "no socket");
        return HTTP_SOCK_NONE;
    }

    /* Non-blocking for the connect only: a blocking one takes the kernel's
       bound, 2.03 s to refuse a loopback port and far longer for a filtered
       one. */
    {
        u_long         nb = 1;
        int            rc;
        fd_set         w, e;
        struct timeval tv;

        ioctlsocket(s, FIONBIO, &nb);
        rc = connect(s, res->ai_addr, (int)res->ai_addrlen);
        freeaddrinfo(res);

        if (rc != 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
            FD_ZERO(&w);
            FD_SET(s, &w);
            FD_ZERO(&e);
            FD_SET(s, &e);
            tv.tv_sec  = (long)(ms / 1000u);
            tv.tv_usec = (long)((ms % 1000u) * 1000u);

            rc = select(0, 0, &w, &e, &tv) > 0 && FD_ISSET(s, &w) ? 0 : -1;
            if (rc == 0) {
                /* Writable is not connected: a refusal arrives the same way,
                   and SO_ERROR tells them apart. */
                int       soerr = 0;
                socklen_t len   = (socklen_t)sizeof(soerr);

                if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&soerr, &len) != 0 || soerr != 0) rc = -1;
            }
        }

        nb = 0;
        ioctlsocket(s, FIONBIO, &nb);

        if (rc != 0) {
            closesocket(s);
            if (err && errlen) snprintf(err, errlen, "%s:%d would not connect", host ? host : "?", port);
            return HTTP_SOCK_NONE;
        }
    }

    /* The console cannot rely on these and polls instead, with the same
       bound. */
    {
        DWORD tv = (DWORD)ms;

        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
    }

    if (slow_factor()) {
        slow_note_open(s);
        g_conns++;
        slow_report();
    }

    if (why) *why = HTTP_OK;
    return (http_sock)s;
}

int http_sock_send(http_sock s, const void *buf, unsigned len) { return send((SOCKET)s, (const char *)buf, (int)len, 0); }

int http_sock_recv(http_sock s, void *buf, unsigned len, int *timed_out) {
    int n;

    if (timed_out) *timed_out = 0;
    n = recv((SOCKET)s, (char *)buf, (int)len, 0);
    /* A live transcode that has not reached a segment holds the connection
       open and sends nothing, which is not a dead link. */
    if (n < 0 && WSAGetLastError() == WSAETIMEDOUT && timed_out) *timed_out = 1;

    if (n > 0 && slow_factor()) {
        unsigned owed = slow_think_left((SOCKET)s);
        unsigned wire = (unsigned)n * 1000000u / ((unsigned)SLOW_BYTES_PER_S / (unsigned)slow_factor());

        if (owed) platform_sleep_us(owed);
        platform_sleep_us(wire);
        g_bytes += (unsigned)n;
        g_slept_us += owed + wire;
    }
    return n;
}

void http_sock_close(http_sock s) {
    if (s == HTTP_SOCK_NONE) return;
    slow_forget((SOCKET)s);
    closesocket((SOCKET)s);
}

/* shutdown() both ways ends a recv another thread is blocked in and leaves the
   descriptor for its owner to close. */
void http_sock_wake(http_sock s) {
    if (s != HTTP_SOCK_NONE) shutdown((SOCKET)s, SD_BOTH);
}

#include "tools/speed.h"

#include "io/http.h"
#include "jelly/item.h"
#include "jelly/api.h"
#include "base/log.h"
#include "io/net.h"
#include "port/platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* stdout is what PSPLINK shows. */
static void say(const char *fmt, ...) {
    char    line[224];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    log_line(line);
    printf("%s\n", line);
    fflush(stdout);
}

/* Static files on the jellyfin.txt host, e.g. `python -m http.server`: the
   link with no media server in the number. */
#define SPEED_BULK_PATH "/pocketfin-speed.bin"
#define SPEED_PAIR_PATH "/pocketfin-speed-4m.bin"

static unsigned g_got;

static int count_sink(const unsigned char *data, unsigned len, void *user) {
    (void)data;
    (void)user;
    g_got += len;
    return 0;
}

/* Two sockets beating one is headroom a second connection would collect;
   matching it is the radio's ceiling. */
static unsigned     g_pair[2];
static volatile int g_pair_done;
static char         g_host[JF_HOST_LEN];
static int          g_port;

static int pair_sink(const unsigned char *data, unsigned len, void *user) {
    (void)data;
    g_pair[*(int *)user] += len;
    return 0;
}

static int pair_worker(void *arg) {
    int  status = 0;
    char err[96];

    http_stream(HTTP_SOCK_NONE, g_host, g_port, SPEED_PAIR_PATH, pair_sink, arg, 0, 0, &status, 0, err, sizeof(err));
    g_pair_done++;
    return 0;
}

int speed_run(void) {
    static char   reply[JF_LIST_BUF];
    static item   rows[JF_LIST_MAX];
    static jf_buf rb = JF_BUF(reply);
    jf_conn       conn;
    char          ip[24];
    int           profile, status = 0;
    char          err[96];
    http_cost     cost;

    if (jf_load_conn(&conn) != 0) return -1;
    jf_use(&conn);
    jf_address(g_host, sizeof(g_host), &g_port);
    /* Scripted: nobody to answer a dialog. */
    profile = conn.profile < 1 ? 1 : conn.profile;

    if (net_start() != 0) {
        say("speed: the network stack would not start");
        return -1;
    }
    if (net_connect(profile, ip, sizeof(ip)) != 0) {
        say("speed: profile %d did not associate -- is the WLAN switch on?", profile);
        return -1;
    }
    say("speed: %s, asking %s:%d", ip, g_host, g_port);

    g_got = 0;
    {
        http_result rc =
            http_stream(HTTP_SOCK_NONE, g_host, g_port, "/System/Info/Public", count_sink, 0, 0, 0, &status, &cost, err, sizeof(err));

        if (rc != HTTP_OK) {
            say("speed: %s (%s), status %d", http_result_text(rc), err, status);
            net_disconnect();
            return -1;
        }
    }

    say("speed: status %d, %u bytes -- connect %u us, first byte %u us, body %u us", status, g_got, cost.connect_us, cost.ttfb_us,
        cost.body_us);

    if (g_got >= 4096u && cost.body_us > 0)
        say("speed: %u kbit/s over the body", (unsigned)((unsigned long long)g_got * 8000ull / cost.body_us));
    else
        say("speed: %u bytes in %u us is too small to rate -- the fixed cost above is the useful number here", g_got, cost.body_us);

    {
        http_result rc;

        g_got  = 0;
        status = 0;
        rc     = http_stream(HTTP_SOCK_NONE, g_host, g_port, SPEED_BULK_PATH, count_sink, 0, 0, 0, &status, &cost, err, sizeof(err));

        if (rc == HTTP_OK && status == 200 && g_got >= 256u * 1024u && cost.body_us > 0)
            say("speed: BULK %u kB in %u us = %u kbit/s, first byte %u us -- no media server in this number", g_got >> 10, cost.body_us,
                (unsigned)((unsigned long long)g_got * 8000ull / cost.body_us), cost.ttfb_us);
        else if (rc == HTTP_OK && status == 200)
            say("speed: bulk got %u bytes -- serve at least 256 kB at %s to rate the link", g_got, SPEED_BULK_PATH);
        else
            say("speed: no %s on this host (status %d) -- skipping the link-only number", SPEED_BULK_PATH, status);
    }

    {
        static int slot[2] = {0, 1};
        unsigned   t0, spent, total;

        g_pair[0] = g_pair[1] = 0;
        g_pair_done           = 0;
        t0                    = platform_clock_us();
        if (platform_thread_start("speed-a", pair_worker, &slot[0]) != 0 || platform_thread_start("speed-b", pair_worker, &slot[1]) != 0) {
            say("speed: could not start the pair");
        } else {
            while (g_pair_done < 2 && platform_clock_us() - t0 < 90000000u) platform_sleep_us(50000);
            spent = platform_clock_us() - t0;
            total = g_pair[0] + g_pair[1];
            if (g_pair_done < 2 || total < 512u * 1024u || !spent)
                say("speed: the pair did not finish (%u kB in %u us) -- serve %s too", total >> 10, spent, SPEED_PAIR_PATH);
            else
                say("speed: PAIR %u kB over two sockets in %u us = %u kbit/s (%u + %u kB)", total >> 10, spent,
                    (unsigned)((unsigned long long)total * 8000ull / spent), g_pair[0] >> 10, g_pair[1] >> 10);
        }
    }

    /* Read-only. */
    {
        int    got = 0;
        jf_err e   = jf_connect();

        if (e != JF_OK) {
            say("speed: sign in -- %s", jf_err_text(e));
        } else {
            e = jf_views(&rb, rows, JF_LIST_MAX, &got);
            if (e != JF_OK)
                say("speed: views -- %s", jf_err_text(e));
            else {
                say("speed: %d libraries, first \"%s\" -- connect %u us, first byte %u us, body %u us for %u bytes", got,
                    got > 0 ? rows[0].name : "", rb.cost.connect_us, rb.cost.ttfb_us, rb.cost.body_us, rb.cost.bytes);

                if (got > 0) {
                    int items = 0;

                    e = jf_items(&rb, rows[0].id, ITEM_SORT_NAME, 0, ITEM_FILTER_ALL, rows, JF_LIST_MAX, &items, 0);
                    if (e != JF_OK)
                        say("speed: items -- %s", jf_err_text(e));
                    else
                        say("speed: %d items -- connect %u us, first byte %u us, body %u us for %u bytes%s", items, rb.cost.connect_us,
                            rb.cost.ttfb_us, rb.cost.body_us, rb.cost.bytes, items > 0 ? "" : " (empty)");
                    /* The first byte is the server thinking: 107 ms for an
                       87-item listing over Ethernet, body 2 ms. */
                    if (e == JF_OK && items > 0 && rb.cost.body_us > 0 && rb.cost.bytes >= 4096u)
                        say("speed: %u kbit/s on the listing body over %s",
                            (unsigned)((unsigned long long)rb.cost.bytes * 8000ull / rb.cost.body_us),
                            platform_cpu_mhz() ? "this console's radio" : "the development machine's link");
                }
            }
        }
    }

    net_disconnect();
    return 0;
}

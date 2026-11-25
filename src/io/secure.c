/* The http_sock_* calls of io/http.h, over the port's raw connection
 * (io/sock.h), with TLS when the server is reached over HTTPS.
 *
 * Every request opens a new connection, and a full handshake costs the console
 * about a second -- a one-second segment each. So the first handshake's session
 * is kept and every later connection resumes it: no key exchange, one round
 * trip. Built with mbedTLS where POCKETFIN_TLS is defined (the console build);
 * elsewhere an HTTPS server is refused with a reason. */

#include "io/http.h"
#include "io/sock.h"

#include "base/log.h"
#include "port/platform.h"

#include <stdio.h>
#include <string.h>

static int  g_secure;
static char g_sni[128];

void http_set_secure(int on, const char *server_name) {
    g_secure = on;
    snprintf(g_sni, sizeof(g_sni), "%s", server_name ? server_name : "");
}

int http_secure(void) { return g_secure; }

#ifndef POCKETFIN_TLS

int http_tls_available(void) { return 0; }

http_sock http_sock_open(const char *host, int port, http_result *why, char *err, unsigned errlen) {
    if (g_secure) {
        if (why) *why = HTTP_NO_HOST;
        if (err && errlen) snprintf(err, errlen, "this build has no HTTPS");
        return HTTP_SOCK_NONE;
    }
    return port_sock_open(host, port, why, err, errlen);
}
int  http_sock_send(http_sock s, const void *buf, unsigned len) { return port_sock_send(s, buf, len); }
int  http_sock_recv(http_sock s, void *buf, unsigned len, int *timed_out) { return port_sock_recv(s, buf, len, timed_out); }
void http_sock_close(http_sock s) { port_sock_close(s); }
void http_sock_wake(http_sock s) { port_sock_wake(s); }

#else

#include "io/ca_bundle.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h> /* only for its error codes */
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

/* A handle is an index here, plain and TLS alike, so callers keep one type.
   Posters, a segment and its presend are about four at once. */
#define CONN_MAX 16

typedef struct {
    int                 used, tls;
    http_sock           raw;
    volatile int        timed_out; /* the last raw read ran out of time */
    mbedtls_ssl_context ssl;
} conn;

static conn                     g_conn[CONN_MAX];
static platform_lock           *g_lock; /* the table, the RNG and the saved session */
static int                      g_ready;
static mbedtls_entropy_context  g_entropy;
static mbedtls_ctr_drbg_context g_drbg;
static mbedtls_x509_crt         g_roots;
static mbedtls_ssl_config       g_cfg;
static mbedtls_ssl_session      g_saved;
static int                      g_have_saved;
static char                     g_saved_for[128];

int http_tls_available(void) { return 1; }

/* Timer jitter: the console has no entropy device user mode can read. */
static int clock_entropy(void *data, unsigned char *out, size_t len, size_t *olen) {
    size_t i;

    (void)data;
    for (i = 0; i < len; i++) {
        unsigned a = platform_clock_us(), spin = 0, b;

        while ((b = platform_clock_us()) == a && spin < 2000) spin++;
        out[i] = (unsigned char)(b ^ (b >> 8) ^ spin ^ (a << 3));
    }
    *olen = len;
    return 0;
}

/* The console's clock says 2009 until somebody sets it, which makes every
   certificate "not yet valid". The chain and the name are still checked. */
static int verify_cb(void *data, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
    (void)data;
    (void)crt;
    (void)depth;
    *flags &= ~(uint32_t)(MBEDTLS_X509_BADCERT_FUTURE | MBEDTLS_X509_BADCERT_EXPIRED);
    return 0;
}

static int tls_init(void) {
    int rc;

    if (g_ready) return 0;
    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_drbg);
    mbedtls_x509_crt_init(&g_roots);
    mbedtls_ssl_config_init(&g_cfg);
    mbedtls_ssl_session_init(&g_saved);
    mbedtls_entropy_add_source(&g_entropy, clock_entropy, 0, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);
    rc = mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy, (const unsigned char *)"pocketfin", 9);
    if (rc == 0) rc = mbedtls_x509_crt_parse(&g_roots, (const unsigned char *)CA_BUNDLE_PEM, sizeof(CA_BUNDLE_PEM));
    if (rc < 0) {
        log_printf("tls: set-up failed, -0x%04X", (unsigned)-rc);
        return -1;
    }
    mbedtls_ssl_config_defaults(&g_cfg, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_authmode(&g_cfg, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&g_cfg, &g_roots, 0);
    mbedtls_ssl_conf_verify(&g_cfg, verify_cb, 0);
    mbedtls_ssl_conf_rng(&g_cfg, mbedtls_ctr_drbg_random, &g_drbg);
    mbedtls_ssl_conf_session_tickets(&g_cfg, MBEDTLS_SSL_SESSION_TICKETS_ENABLED);
    g_ready = 1;
    return 0;
}

static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
    conn *c = (conn *)ctx;
    int   n = port_sock_send(c->raw, buf, (unsigned)len);

    return n > 0 ? n : MBEDTLS_ERR_NET_SEND_FAILED;
}

/* A quiet held-open connection (a transcode not yet ready) is not an error:
   it surfaces as WANT_READ with timed_out set, and the next read resumes. */
static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
    conn *c  = (conn *)ctx;
    int   to = 0;
    int   n  = port_sock_recv(c->raw, buf, (unsigned)len, &to);

    if (n > 0) return n;
    if (n == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    if (to) {
        c->timed_out = 1;
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

static conn *slot(http_sock s) { return (s >= 0 && s < CONN_MAX && g_conn[s].used) ? &g_conn[s] : 0; }

static void release(http_sock s) {
    conn *c = slot(s);

    if (!c) return;
    if (c->tls) mbedtls_ssl_free(&c->ssl);
    port_sock_close(c->raw);
    platform_lock_take(g_lock);
    c->used = 0;
    platform_lock_give(g_lock);
}

http_sock http_sock_open(const char *host, int port, http_result *why, char *err, unsigned errlen) {
    http_sock raw, h = HTTP_SOCK_NONE;
    conn     *c;
    int       rc, i;

    if (!g_lock) g_lock = platform_lock_new("tls");
    raw = port_sock_open(host, port, why, err, errlen);
    if (raw == HTTP_SOCK_NONE) return HTTP_SOCK_NONE;

    platform_lock_take(g_lock);
    for (i = 0; i < CONN_MAX; i++)
        if (!g_conn[i].used) {
            h = i;
            memset(&g_conn[i], 0, sizeof(g_conn[i]));
            g_conn[i].used = 1;
            g_conn[i].raw  = raw;
            break;
        }
    platform_lock_give(g_lock);
    if (h == HTTP_SOCK_NONE) {
        port_sock_close(raw);
        if (why) *why = HTTP_SEND_FAILED;
        if (err && errlen) snprintf(err, errlen, "too many connections open");
        return HTTP_SOCK_NONE;
    }
    if (!g_secure) return h;

    c = &g_conn[h];
    platform_lock_take(g_lock);
    rc = tls_init();
    platform_lock_give(g_lock);
    if (rc != 0) {
        release(h);
        if (why) *why = HTTP_NO_HOST;
        if (err && errlen) snprintf(err, errlen, "HTTPS could not start");
        return HTTP_SOCK_NONE;
    }

    c->tls = 1;
    mbedtls_ssl_init(&c->ssl);
    rc = mbedtls_ssl_setup(&c->ssl, &g_cfg);
    if (rc == 0) rc = mbedtls_ssl_set_hostname(&c->ssl, g_sni[0] ? g_sni : host);
    mbedtls_ssl_set_bio(&c->ssl, c, bio_send, bio_recv, 0);

    /* One handshake at a time: they share the RNG, and the first one's session
       is what the rest resume. */
    platform_lock_take(g_lock);
    if (rc == 0 && g_have_saved && strcmp(g_saved_for, host) == 0) (void)mbedtls_ssl_set_session(&c->ssl, &g_saved);
    {
        unsigned began = platform_clock_us();

        while (rc == 0 && (rc = mbedtls_ssl_handshake(&c->ssl)) != 0) {
            if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) break;
            if (http_cancelled() || (platform_clock_us() - began) / 1000u >= http_timeout_ms()) break;
            rc = 0;
        }
        if (rc == 0) {
            int kept;

            mbedtls_ssl_session_free(&g_saved);
            mbedtls_ssl_session_init(&g_saved);
            kept = mbedtls_ssl_get_session(&c->ssl, &g_saved) == 0;
            if (kept) {
                g_have_saved = 1;
                snprintf(g_saved_for, sizeof(g_saved_for), "%s", host);
            }
            log_printf("tls: %s %s in %u ms", host, mbedtls_ssl_get_ciphersuite(&c->ssl), (platform_clock_us() - began) / 1000u);
        }
    }
    platform_lock_give(g_lock);

    if (rc != 0) {
        char     why_s[80];
        uint32_t flags = mbedtls_ssl_get_verify_result(&c->ssl);

        mbedtls_strerror(rc, why_s, sizeof(why_s));
        log_printf("tls: %s:%d handshake failed, -0x%04X %s, verify 0x%X", host, port, (unsigned)-rc, why_s, (unsigned)flags);
        release(h);
        if (why) *why = HTTP_NO_HOST;
        if (err && errlen) {
            if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH)
                snprintf(err, errlen, "the certificate is not for %s", host);
            else if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED)
                snprintf(err, errlen, "the certificate is not from a known authority");
            else
                snprintf(err, errlen, "HTTPS failed with %s", host);
        }
        return HTTP_SOCK_NONE;
    }
    return h;
}

int http_sock_send(http_sock s, const void *buf, unsigned len) {
    conn *c = slot(s);
    int   n;

    if (!c) return -1;
    if (!c->tls) return port_sock_send(c->raw, buf, len);
    do n = mbedtls_ssl_write(&c->ssl, (const unsigned char *)buf, len);
    while (n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_WANT_READ);
    return n;
}

int http_sock_recv(http_sock s, void *buf, unsigned len, int *timed_out) {
    conn *c = slot(s);
    int   n;

    if (timed_out) *timed_out = 0;
    if (!c) return -1;
    if (!c->tls) return port_sock_recv(c->raw, buf, len, timed_out);
    c->timed_out = 0;
    n            = mbedtls_ssl_read(&c->ssl, (unsigned char *)buf, len);
    if (n >= 0) return n;
    if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
    if (n == MBEDTLS_ERR_SSL_WANT_READ && c->timed_out && timed_out) *timed_out = 1;
    return -1;
}

void http_sock_close(http_sock s) { release(s); }

void http_sock_wake(http_sock s) {
    conn *c = slot(s);

    if (c) port_sock_wake(c->raw);
}

#endif

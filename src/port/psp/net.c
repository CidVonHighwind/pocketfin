/* net.h and http.h's socket seam, for the console. */

#include "io/http.h"
#include "io/net.h"
#include "io/net_private.h"

#include "base/log.h"
#include "port/platform.h"

#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_apctl.h>
#include <pspnet_inet.h>
#include <pspnet_resolver.h>
#include <psputility.h>
#include <psputility_netmodules.h>
#include <pspwlan.h>
#include <psputility_netparam.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdio.h>
#include <string.h>

/* The stack lives in the kernel and outlives this module, so a load that did
 * not leave cleanly hands the next one a stack that is already up. */
#define INET_ALREADY_UP  0x80410201u
#define APCTL_ALREADY_UP 0x80410A01u

#define APCTL_GOT_IP   4
#define JOIN_BOUND_MS  20000u
#define JOIN_STEP_US   100000u
#define LEAVE_BOUND_US 3000000u

static int         g_up;
static int         g_inet_ours, g_apctl_ours; /* term only what this module put up */
static net_state_t g_last = NET_OFF;

/* sceNetApctlGetState refuses while another thread is inside it, and a refusal
   reads as a radio that is off: a film opening beside a library fetch was told
   so. */
static platform_lock *g_apctl;

int net_start(void) {
    int rc;

    if (g_up) return 0;
    if (!g_apctl) g_apctl = platform_lock_new("apctl");

    /* Asked: every failure below would otherwise read as the switch being off. */
    if (sceWlanGetSwitchState() == 0) {
        log_line("net: the WLAN switch on the side of the console is off");
        return NET_START_NO_SWITCH;
    }

    /* Unloaded first: a load that did not leave cleanly leaves the utility
       answering "already loaded" for a module whose library is gone, and every
       sceNetInetInit after that is 0x8002013A. */
    {
        int m1, m2;

        sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
        sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
        m1 = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
        m2 = sceUtilityLoadNetModule(PSP_NET_MODULE_INET);
        if (m1 < 0 || m2 < 0) log_printf("net: module load COMMON=0x%08X INET=0x%08X", m1, m2);
    }

    /* Largest first: the kernel partition holds about 619 kB, so 512 kB is
       sometimes refused (0x80020190). A smaller pool only costs throughput. */
    {
        static const int kPool[] = {512, 384, 256, 192, 128};
        unsigned         i;
        int              first_refusal = 0;

        rc = -1;
        for (i = 0; i < sizeof(kPool) / sizeof(kPool[0]); i++) {
            rc = sceNetInit(kPool[i] * 1024, 42, 4 * 1024, 42, 4 * 1024);
            if (rc >= 0) {
                if (i == 0)
                    log_printf("net: pool %d KB", kPool[0]);
                else
                    log_printf("net: pool %d KB (%d KB refused, 0x%08X)", kPool[i], kPool[0], first_refusal);
                break;
            }
            if (i == 0) first_refusal = rc;
        }
        if (rc < 0) {
            log_printf("net: no pool from 512 KB down to 128 KB, last 0x%08X", rc);
            return -1;
        }
    }

    /* Already-up is used as it stands: terminating a layer this module did
       not initialise wedged the console outright. On a refusal the pool goes
       back, or it poisons every later load. */
    rc = sceNetInetInit();
    if (rc == (int)INET_ALREADY_UP) {
        log_line("net: the inet layer was already up -- using it as it is");
        g_inet_ours = 0;
    } else if (rc < 0) {
        log_printf("net: sceNetInetInit 0x%08X, taking the stack back down", rc);
        sceNetTerm();
        return -1;
    } else {
        g_inet_ours = 1;
    }

    rc = sceNetApctlInit(0x8000, 48);
    if (rc == (int)APCTL_ALREADY_UP) {
        log_line("net: apctl was already up -- using it as it is");
        g_apctl_ours = 0;
    } else if (rc < 0) {
        log_printf("net: sceNetApctlInit 0x%08X", rc);
        if (g_inet_ours) sceNetInetTerm();
        sceNetTerm();
        return -1;
    } else {
        g_apctl_ours = 1;
    }

    g_up = 1;
    log_line("net: stack up");
    return 0;
}

void net_stop(void) {
    if (g_up) {
        net_disconnect();
        if (g_apctl_ours) sceNetApctlTerm();
        if (g_inet_ours) sceNetInetTerm();
        sceNetTerm();
        g_up = 0;
    }

    /* Left loaded they stay in the user partition: a load after a stop had
       21,908,480 bytes in its largest run where a power cycle had 25,149,440.
       Unconditional: a start that failed halfway loaded them without g_up. */
    sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
    sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
    log_line("net: stack down");
}

/* Which saved connection is associated, or 0. apctl answers only its name, and
 * without the number a wake after a network picked in the dialog asks again. */
static int profile_in_force(void) {
    union SceNetApctlInfo info;
    int                   i;

    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(0 /* PSP_NET_APCTL_INFO_PROFILE_NAME */, &info) < 0) return 0;

    for (i = 1; i <= 10; i++) {
        netData saved;

        if (sceUtilityCheckNetParam(i) != 0) continue;
        memset(&saved, 0, sizeof(saved));
        if (sceUtilityGetNetParam(i, PSP_NETPARAM_NAME, &saved) < 0) continue;
        if (strncmp(saved.asString, info.name, sizeof(info.name)) == 0) return i;
    }
    log_printf("net: associated with \"%s\", which is none of the saved connections", info.name);
    return 0;
}

int net_connect(int profile, char *ip, unsigned iplen) {
    unsigned              began;
    int                   rc;
    union SceNetApctlInfo info;

    if (ip && iplen) ip[0] = 0;
    if (!g_up && net_start() != 0) return -1;

    /* Left associated by a load that did not leave cleanly, apctl refuses a
       connect (0x80410A80); the address it has is the answer. */
    {
        int state = 0;

        if (sceNetApctlGetState(&state) >= 0 && state == APCTL_GOT_IP) {
            log_line("net: already associated -- adopting the connection");
            rc = 0;
            goto joined;
        }
    }

    /* The stack stays up behind a refusal: the dialog that follows is built on
       the modules net_stop() unloads, and without them it came up on a black
       panel and took the application with it. */
    if (profile < 1) {
        log_line("net: no connection profile chosen -- the viewer picks one");
        return -1;
    }

    rc = sceNetApctlConnect(profile);
    if (rc < 0) {
        log_printf("net: the console refused connection profile %d (0x%08X)", profile, rc);
        {
            char have[64];
            int  i, n = 0;

            have[0] = 0;
            for (i = 1; i <= 10; i++)
                if (sceUtilityCheckNetParam(i) == 0) n += snprintf(have + n, sizeof(have) - (unsigned)n, "%s%d", n ? ", " : "", i);
            log_printf("net: the console has %s", have[0] ? have : "no saved connections at all");
        }
        return -1;
    }

    /* Elapsed time, not a count of sleeps: below the drawing thread a nominal
       100 ms sleep takes longer. */
    began = platform_clock_us();
    for (;;) {
        int state = 0;

        if (sceNetApctlGetState(&state) < 0) {
            log_line("net: the access point stopped answering");
            sceNetApctlDisconnect();
            return -1;
        }
        if (state == APCTL_GOT_IP) break;

        if ((platform_clock_us() - began) / 1000u >= JOIN_BOUND_MS) {
            log_printf("net: profile %d did not associate in %u ms (state %d)", profile, JOIN_BOUND_MS, state);
            sceNetApctlDisconnect();
            return -1;
        }
        platform_sleep_us(JOIN_STEP_US);
    }

joined:
    memset(&info, 0, sizeof(info));
    if (sceNetApctlGetInfo(8 /* PSP_NET_APCTL_INFO_IP */, &info) < 0) {
        log_line("net: associated but would not say the address");
        sceNetApctlDisconnect();
        return -1;
    }

    /* A buffering report is unreadable without these: power save sleeps the
       receiver between beacons, and strength sets the PHY rate. */
    {
        union SceNetApctlInfo v;
        int                   strength = -1, channel = -1, save = -1;

        memset(&v, 0, sizeof(v));
        if (sceNetApctlGetInfo(5 /* STRENGTH */, &v) >= 0) strength = v.strength;
        memset(&v, 0, sizeof(v));
        if (sceNetApctlGetInfo(6 /* CHANNEL */, &v) >= 0) channel = v.channel;
        memset(&v, 0, sizeof(v));
        if (sceNetApctlGetInfo(7 /* POWER_SAVE */, &v) >= 0) save = v.powerSave;
        log_printf("net: signal %d%%, channel %d, power save %s", strength, channel, save > 0 ? "ON" : save == 0 ? "off" : "?");
    }

    /* Seeded: the sign-in's socket opens before any frame has asked. */
    g_last = NET_UP;
    net_note_joined(info.ip, profile > 0 ? profile : profile_in_force());
    if (ip && iplen) snprintf(ip, iplen, "%s", info.ip);
    return 0;
}

void net_disconnect(void) {
    unsigned began;
    int      state = 0;

    if (!net_joined()) return;
    sceNetApctlDisconnect();
    /* It returns before the radio has left: a connect 27 ms later found
       APCTL_GOT_IP, adopted the association being torn down, and the next
       requests hung inside the stack. */
    began = platform_clock_us();
    while (sceNetApctlGetState(&state) >= 0 && state != 0 && platform_clock_us() - began < LEAVE_BOUND_US) platform_sleep_us(JOIN_STEP_US);
    net_note_left();
    log_printf("net: disconnected (state %d)", state);
}

net_state_t net_state_last(void) { return g_last; }

net_state_t net_state(void) {
    int state = 0, rc;

    if (!g_up) return (g_last = NET_OFF);
    platform_lock_take(g_apctl);
    rc = sceNetApctlGetState(&state);
    platform_lock_give(g_apctl);
    if (rc < 0) return (g_last = NET_OFF);
    /* A film once lost "the link" mid-stream and nothing said which state. */
    {
        static int said = APCTL_GOT_IP;

        if (state != said) {
            log_printf("net: apctl state %d", state);
            said = state;
        }
    }
    if (state != APCTL_GOT_IP) return (g_last = NET_DOWN);
    /* After a sleep the stack still believes it is associated. */
    return (g_last = net_link_is_current() ? NET_UP : NET_DOWN);
}

http_sock http_sock_open(const char *host, int port, http_result *why, char *err, unsigned errlen) {
    struct sockaddr_in a;
    int                s;

    if (why) *why = HTTP_NO_HOST;

    /* With the radio off an attempt still costs what the stack spends
       before admitting it. */
    if (net_state_last() != NET_UP) {
        log_printf("net: socket refused, last state %s", net_state_text(net_state_last()));
        if (why) *why = HTTP_NO_LINK;
        if (err && errlen) snprintf(err, errlen, "the radio is %s", net_state_text(net_state_last()));
        return HTTP_SOCK_NONE;
    }

    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port   = htons((unsigned short)port);
    /* A dotted quad, not a name: no resolver is started. */
    if (!host || !inet_aton(host, &a.sin_addr)) {
        if (err && errlen) snprintf(err, errlen, "\"%s\" is not a dotted address", host ? host : "");
        return HTTP_SOCK_NONE;
    }

    s = sceNetInetSocket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        log_printf("net: socket() refused, 0x%08X errno %d", (unsigned)s, sceNetInetGetErrno());
        if (err && errlen) snprintf(err, errlen, "no socket");
        return HTTP_SOCK_NONE;
    }
    /* The default few kilobytes cap how far the server runs ahead, and the
       fetch worker reads only when the decoder leaves it CPU. */
    {
        int rcv = 64 * 1024;

        sceNetInetSetsockopt(s, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv));
    }

    if (sceNetInetConnect(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
        sceNetInetClose(s);
        if (err && errlen) snprintf(err, errlen, "%s:%d would not connect", host, port);
        return HTTP_SOCK_NONE;
    }

    if (why) *why = HTTP_OK;
    return (http_sock)s;
}

int http_sock_send(http_sock s, const void *buf, unsigned len) { return (int)sceNetInetSend((int)s, buf, len, 0); }

/* Polled: the stack does not honour SO_RCVTIMEO consistently. 1 ms, not 10:
 * a live transcode arrives in spurts, and at 4 kB a read a 10 ms poll caps
 * ~3.2 Mbit/s -- segments measured 2.7 Mbit/s against 4.1 for a static file
 * on the same console. */
#define RECV_STEP_US 1000u

int http_sock_recv(http_sock s, void *buf, unsigned len, int *timed_out) {
    unsigned began = platform_clock_us();

    if (timed_out) *timed_out = 0;
    for (;;) {
        int n;

        if (http_cancelled()) return -1;

        n = (int)sceNetInetRecv((int)s, buf, len, 0);
        if (n >= 0) return n;

        if ((platform_clock_us() - began) / 1000u >= http_timeout_ms()) {
            if (timed_out) *timed_out = 1;
            return -1;
        }
        platform_sleep_us(RECV_STEP_US);
    }
}

void http_sock_close(http_sock s) {
    if (s != HTTP_SOCK_NONE) sceNetInetClose((int)s);
}

/* The owner still closes it. */
void http_sock_wake(http_sock s) {
    if (s != HTTP_SOCK_NONE) sceNetInetShutdown((int)s, 2 /* SHUT_RDWR */);
}

/* See jelly/api.h. Against a server of this file's own, for the reason
 * test_http.c gives; the one that needs a real server skips without one.
 *
 * Development machine only: the console's stack has no loopback. */

#ifndef __PSP__

#include "io/http.h"
#include "jelly/api.h"
#include "io/net.h"

#include "port/platform.h"
#include "tools/selftest.h"

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

static char         g_reply[8192];
static unsigned     g_reply_len;
static int          g_port;
static volatile int g_served;
static char         g_request[1024];

/* A SOCKET is 64 bits on this machine and a long is 32. */
static tsock g_listen;

static int serve(void *arg) {
    tsock listen_s = g_listen, c;

    (void)arg;

    c = accept(listen_s, 0, 0);
    if (c != BADSOCK) {
        unsigned sent = 0;
        char     drain[256];
        int      got = recv(c, g_request, (int)sizeof(g_request) - 1, 0);

        g_request[got > 0 ? got : 0] = 0;

        while (sent < g_reply_len) {
            int m = send(c, g_reply + sent, (int)(g_reply_len - sent), 0);

            if (m <= 0) break;
            sent += (unsigned)m;
        }
        /* Closed with a POST body unread, the socket resets and the client
           sees no reply. */
        shutdown(c, 1);
        while (recv(c, drain, sizeof(drain), 0) > 0) {}
        CLOSESOCK(c);
    }
    CLOSESOCK(listen_s);
    g_served = 1;
    return 0;
}

/* One reply on a port the machine picks. 0 on failure. */
static int start_server_status(int status, const char *body) {
    struct sockaddr_in a;
    tsock              s;
    int                namelen = (int)sizeof(a);
    int                len;

    len = snprintf(g_reply, sizeof(g_reply), "HTTP/1.1 %d %s\r\nContent-Length: %u\r\n\r\n%s", status, status == 204 ? "No Content" : "OK",
                   (unsigned)strlen(body), body);
    if (len < 0 || (unsigned)len >= sizeof(g_reply)) return 0;
    g_reply_len  = (unsigned)len;
    g_served     = 0;
    g_request[0] = 0;

    if (net_start() != 0) return 0;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == BADSOCK) return 0;

    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(0x7F000001);
    a.sin_port        = 0;
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s, 1) != 0) {
        CLOSESOCK(s);
        return 0;
    }
    if (getsockname(s, (struct sockaddr *)&a, &namelen) != 0) {
        CLOSESOCK(s);
        return 0;
    }
    g_port   = ntohs(a.sin_port);
    g_listen = s;
    if (platform_thread_start("jfquerysrv", serve, 0) != 0) {
        CLOSESOCK(s);
        return 0;
    }
    return 1;
}

static int start_server(const char *body) { return start_server_status(200, body); }

static void wait_served(void) {
    unsigned t0 = platform_clock_us();

    while (!g_served && platform_clock_us() - t0 < 3000000u) platform_sleep_us(2000);
}

#define AUTH_REPLY "{\"User\":{\"Id\":\"u1\"},\"AccessToken\":\"t0ken\"}"

static char   g_buf[16 * 1024];
static jf_buf g_b = JF_BUF(g_buf);

static void serve_at(int port) {
    jf_conn c;

    memset(&c, 0, sizeof(c));
    snprintf(c.host, sizeof(c.host), "127.0.0.1");
    c.port = port;
    jf_use(&c);
}

static int sign_in_here(char *note, unsigned n) {
    jf_err e;

    if (!start_server(AUTH_REPLY)) {
        snprintf(note, n, "could not start a server");
        return -1;
    }
    serve_at(g_port);
    e = jf_connect();
    wait_served();
    if (e != JF_OK) {
        snprintf(note, n, "the loopback sign-in answered %s", jf_err_text(e));
        return -1;
    }
    return 0;
}

/* Signed in, with one reply waiting at the address in use. */
static int answer(int status, const char *body, char *note, unsigned n) {
    if (sign_in_here(note, n) != 0) return -1;
    if (!start_server_status(status, body)) {
        snprintf(note, n, "could not start a server");
        return -1;
    }
    serve_at(g_port);
    return 0;
}

/* Both answer an empty 204, so only the method tells them apart. */
static int mark_sends(char *note, unsigned n, const char *what, int on, const char *want_method, const char *want_route) {
    jf_err e;

    if (answer(204, "", note, n) != 0) return -1;
    e = strcmp(what, "played") == 0 ? jf_set_played(&g_b, "it3m", on) : jf_set_favorite(&g_b, "it3m", on);
    wait_served();

    if (e != JF_OK) {
        snprintf(note, n, "%s %s answered %s", what, on ? "on" : "off", jf_err_text(e));
        return 1;
    }
    if (strncmp(g_request, want_method, strlen(want_method)) != 0) {
        snprintf(note, n, "%s %s did not send %s: %.40s", what, on ? "on" : "off", want_method, g_request);
        return 1;
    }
    if (!strstr(g_request, want_route) || !strstr(g_request, "/it3m?userId=u1")) {
        snprintf(note, n, "%s went to the wrong place: %.60s", what, g_request);
        return 1;
    }
    return 0;
}

static int t_a_mark_is_a_post_and_unmarking_a_delete(char *note, unsigned n) {
    int rc;

    rc = mark_sends(note, n, "played", 1, "POST ", "/UserPlayedItems/");
    if (rc != 0) return rc;
    rc = mark_sends(note, n, "played", 0, "DELETE ", "/UserPlayedItems/");
    if (rc != 0) return rc;
    rc = mark_sends(note, n, "favorite", 1, "POST ", "/UserFavoriteItems/");
    if (rc != 0) return rc;
    rc = mark_sends(note, n, "favorite", 0, "DELETE ", "/UserFavoriteItems/");
    if (rc != 0) return rc;

    snprintf(note, n, "played and favorite, POST to set and DELETE to clear");
    return 0;
}

#define ITEM_REPLY                                                       \
    "{\"Items\":[{\"Id\":\"it3m\",\"Name\":\"Paprika\","                 \
    "\"Overview\":\"A device that records dreams.\",\"Type\":\"Movie\"," \
    "\"RunTimeTicks\":54000000000,"                                      \
    "\"UserData\":{\"Played\":true,\"IsFavorite\":true,"                 \
    "\"PlaybackPositionTicks\":12000000000}}],\"TotalRecordCount\":1}"

#define ITEM_REPLY_NO_OVERVIEW "{\"Items\":[{\"Id\":\"it3m\",\"Name\":\"Paprika\",\"Type\":\"Movie\"}],\"TotalRecordCount\":1}"

static int t_one_item_brings_its_synopsis_and_its_marks(char *note, unsigned n) {
    item   it;
    char   over[256];
    jf_err e;

    if (answer(200, ITEM_REPLY, note, n) != 0) return -1;
    e = jf_item(&g_b, "it3m", &it, over, sizeof(over), 0);
    wait_served();

    if (e != JF_OK) {
        snprintf(note, n, "asking for one item answered %s", jf_err_text(e));
        return 1;
    }
    if (!strstr(g_request, "ids=it3m") || !strstr(g_request, "Fields=Overview")) {
        snprintf(note, n, "the request did not ask for the synopsis: %.60s", g_request);
        return 1;
    }
    if (strcmp(over, "A device that records dreams.") != 0) {
        snprintf(note, n, "the synopsis read \"%s\"", over);
        return 1;
    }
    if (!it.played || !it.favorite || it.resume_ticks != 12000000000ULL) {
        snprintf(note, n, "the marks came back played=%d fav=%d resume=%llu", it.played, it.favorite, (unsigned long long)it.resume_ticks);
        return 1;
    }

    if (answer(200, ITEM_REPLY_NO_OVERVIEW, note, n) != 0) return -1;
    e = jf_item(&g_b, "it3m", &it, over, sizeof(over), 0);
    wait_served();
    if (e != JF_OK || over[0] != 0) {
        snprintf(note, n, "an item with no synopsis gave %s and left \"%s\"", jf_err_text(e), over);
        return 1;
    }

    snprintf(note, n, "synopsis and marks in one ask; absent is empty, not a fault");
    return 0;
}

#define TRACKS_REPLY                                                                                        \
    "{\"Items\":[{\"Id\":\"it3m\",\"Name\":\"Paprika\",\"Type\":\"Movie\",\"MediaStreams\":["               \
    "{\"Index\":0,\"Type\":\"Video\",\"DisplayTitle\":\"1080p H264\"},"                                     \
    "{\"Index\":1,\"Type\":\"Audio\",\"DisplayTitle\":\"English - AAC\",\"IsDefault\":false},"             \
    "{\"Index\":2,\"Type\":\"Audio\",\"DisplayTitle\":\"Japanese - AAC - Default\",\"Language\":\"jpn\",\"IsDefault\":true}," \
    "{\"Index\":3,\"Type\":\"Subtitle\",\"DisplayTitle\":\"English - SUBRIP\"}]}],\"TotalRecordCount\":1}"

#define PLAYBACK_REPLY                                                                           \
    "{\"MediaSources\":[{\"TranscodingUrl\":\"/videos/it3m/stream.mp4?MediaSourceId=it3m"         \
    "&AudioStreamIndex=2&SubtitleStreamIndex=3&SubtitleMethod=Encode&ApiKey=k\"}],"              \
    "\"PlaySessionId\":\"s1\"}"

static int t_tracks_are_read_and_the_chosen_ones_are_asked_for(char *note, unsigned n) {
    item      it;
    char      over[64];
    jf_tracks tr;
    jf_hls    h;
    jf_err    e;

    if (answer(200, TRACKS_REPLY, note, n) != 0) return -1;
    e = jf_item(&g_b, "it3m", &it, over, sizeof(over), &tr);
    wait_served();
    if (e != JF_OK || !strstr(g_request, "MediaStreams")) {
        snprintf(note, n, "the item answered %s, or the request did not ask for its streams", jf_err_text(e));
        return 1;
    }
    if (tr.audio_n != 2 || tr.sub_n != 1 || tr.audio_default != 2 || tr.sub[0].index != 3 || strcmp(tr.audio[1].name, "Japanese - AAC - Default") ||
        strcmp(tr.audio[1].lang, "jpn") || tr.audio[0].lang[0]) {
        snprintf(note, n, "read %d audio, %d subtitle, default %d, \"%s\" in \"%s\"", tr.audio_n, tr.sub_n, tr.audio_default, tr.audio[1].name,
                 tr.audio[1].lang);
        return 1;
    }

    if (answer(200, PLAYBACK_REPLY, note, n) != 0) return -1;
    e = jf_hls_open(&g_b, "it3m", 0, 1000000u, 2, 3, &h, 0, 0);
    wait_served();
    /* Without mediaSourceId the server ignores both indexes. */
    if (e != JF_OK || !strstr(g_request, "mediaSourceId=it3m&subtitleStreamIndex=3&audioStreamIndex=2")) {
        snprintf(note, n, "%s, asked %.120s", jf_err_text(e), g_request);
        return 1;
    }
    if (!strstr(h.query, "SubtitleStreamIndex=3&SubtitleMethod=Encode")) {
        snprintf(note, n, "a chosen subtitle was dropped: %.120s", h.query);
        return 1;
    }

    if (answer(200, PLAYBACK_REPLY, note, n) != 0) return -1;
    e = jf_hls_open(&g_b, "it3m", 0, 1000000u, -1, -1, &h, 0, 0);
    wait_served();
    if (e != JF_OK || strstr(g_request, "audioStreamIndex") || !strstr(h.query, "SubtitleStreamIndex=-1&SubtitleMethod=External")) {
        snprintf(note, n, "no subtitle still streamed one: %.120s", h.query);
        return 1;
    }
    snprintf(note, n, "2 audio and 1 subtitle read; the chosen pair asked for and kept; none still forced off");
    return 0;
}

/* The next episode's file numbers its tracks its own way. */
static int t_a_track_is_found_again_in_another_file(char *note, unsigned n) {
    static const jf_track next[] = {{1, "English - AC3", "eng"}, {2, "Japanese - AAC", "jpn"}, {5, "Signs - English - ASS", "eng"}};
    static const struct {
        jf_track want;
        int      index;
    } CASE[] = {
        {{9, "Signs - English - ASS", "eng"}, 5}, /* the name wins over the language */
        {{9, "Japanese - AAC - Default", "jpn"}, 2},
        {{9, "Deutsch", "deu"}, -1},
        {{9, "", "eng"}, -1}, /* none chosen */
    };
    int i;

    for (i = 0; i < (int)(sizeof(CASE) / sizeof(CASE[0])); i++) {
        int got = jf_track_find(next, 3, &CASE[i].want);

        if (got != CASE[i].index) {
            snprintf(note, n, "\"%s\" (%s) found %d, wanted %d", CASE[i].want.name, CASE[i].want.lang, got, CASE[i].index);
            return 1;
        }
    }
    snprintf(note, n, "by name, then language, else none");
    return 0;
}

/* adjacentTo's episodes carry no index numbers, so the order can only be the
   server's. */
#define ADJACENT_REPLY                                                   \
    "{\"Items\":["                                                       \
    "{\"Id\":\"e1\",\"Name\":\"Asteroid Blues\",\"Type\":\"Episode\","   \
    "\"SeriesName\":\"Cowboy Bebop\",\"SeriesId\":\"s9\"},"              \
    "{\"Id\":\"e2\",\"Name\":\"Stray Dog Strut\",\"Type\":\"Episode\","  \
    "\"SeriesName\":\"Cowboy Bebop\",\"SeriesId\":\"s9\"},"              \
    "{\"Id\":\"e3\",\"Name\":\"Honky Tonk Women\",\"Type\":\"Episode\"," \
    "\"SeriesName\":\"Cowboy Bebop\",\"SeriesId\":\"s9\"}"               \
    "],\"TotalRecordCount\":3}"

static int t_adjacent_asks_the_episodes_route_and_reads_it_back(char *note, unsigned n) {
    item   out[8];
    int    got = 0;
    jf_err e;

    if (answer(200, ADJACENT_REPLY, note, n) != 0) return -1;
    e = jf_adjacent(&g_b, "s9", "e2", out, 8, &got);
    wait_served();

    if (e != JF_OK) {
        snprintf(note, n, "%s", jf_err_text(e));
        return 1;
    }
    if (!strstr(g_request, "GET /Shows/s9/Episodes?") || !strstr(g_request, "adjacentTo=e2") || !strstr(g_request, "userId=u1")) {
        snprintf(note, n, "the request was not /Shows/s9/Episodes with adjacentTo=e2");
        return 1;
    }
    if (got != 3) {
        snprintf(note, n, "%d neighbours, wanted 3", got);
        return 1;
    }
    if (strcmp(out[0].name, "Asteroid Blues") != 0 || strcmp(out[1].id, "e2") != 0 || strcmp(out[2].name, "Honky Tonk Women") != 0) {
        snprintf(note, n, "the order came back \"%s\", \"%s\", \"%s\"", out[0].name, out[1].name, out[2].name);
        return 1;
    }
    if (out[1].kind != ITEM_KIND_EPISODE || strcmp(out[1].series_id, "s9") != 0) {
        snprintf(note, n, "the middle row is kind %d series \"%s\"", (int)out[1].kind, out[1].series_id);
        return 1;
    }
    snprintf(note, n, "3 in the server's order, /Shows/s9/Episodes?adjacentTo=e2");
    return 0;
}

/* Port 9 has nothing on it: a request would not come back JF_ERR_GARBLED. */
static int t_adjacent_without_a_series_makes_no_request(char *note, unsigned n) {
    item out[4];
    int  got = 7;

    serve_at(9);
    if (jf_adjacent(&g_b, "", "e2", out, 4, &got) != JF_ERR_GARBLED) {
        snprintf(note, n, "an empty series id was not refused");
        return 1;
    }
    if (got != 0) {
        snprintf(note, n, "a refused call left got at %d", got);
        return 1;
    }
    if (jf_adjacent(&g_b, "s9", "", out, 4, &got) != JF_ERR_GARBLED) {
        snprintf(note, n, "an empty item id was not refused");
        return 1;
    }
    return 0;
}

/* Two threads refused together: the second signing in again revokes the token
   the first just got. Port 9 makes any sign-in answer JF_ERR_OFFLINE. */
static int t_only_the_token_in_force_signs_in_again(char *note, unsigned n) {
    jf_err e;

    if (sign_in_here(note, n) != 0) return -1;
    serve_at(9);

    e = jf_reauth("Token=\"stale\"");
    if (e != JF_OK) {
        snprintf(note, n, "a stale token signed in again: %s", jf_err_text(e));
        return 1;
    }
    e = jf_reauth("Token=\"t0ken\"");
    if (e != JF_ERR_OFFLINE) {
        snprintf(note, n, "the token in force did not sign in again: %s", jf_err_text(e));
        return 1;
    }
    if (!jf_signed_in()) {
        snprintf(note, n, "a failed sign-in lost the token it had");
        return 1;
    }
    return 0;
}

static char   g_real[JF_LIST_BUF];
static jf_buf g_rb = JF_BUF(g_real);

static int real_server(char *note, unsigned n) {
    jf_conn c;

    if (jf_load_conn(&c) != 0) {
        snprintf(note, n, "no connection file");
        return -1;
    }
    jf_use(&c);
    if (net_start() != 0) {
        snprintf(note, n, "no stack");
        return -1;
    }
    if (jf_connect() != JF_OK) {
        snprintf(note, n, "nothing answered at %s:%d", c.host, c.port);
        return -1;
    }
    return 0;
}

/* A loopback server answers 200 to anything; only a real one says whether it
   accepts the query. */
static int t_real_adjacent_episodes_are_accepted(char *note, unsigned n) {
    item   rail[16], neigh[8];
    int    got = 0, i, have = -1;
    jf_err e;

    if (real_server(note, n) != 0) return -1;

    if (jf_next_up(&g_rb, rail, 16, &got) != JF_OK || got < 1) {
        got = 0;
        if (jf_resume(&g_rb, rail, 16, &got) != JF_OK) {
            snprintf(note, n, "neither rail answered");
            return -1;
        }
    }
    for (i = 0; i < got; i++)
        if (rail[i].kind == ITEM_KIND_EPISODE && rail[i].series_id[0]) {
            have = i;
            break;
        }
    if (have < 0) {
        snprintf(note, n, "no episode among %d rail rows to ask about", got);
        return -1;
    }

    got = 0;
    e   = jf_adjacent(&g_rb, rail[have].series_id, rail[have].id, neigh, 8, &got);
    if (e != JF_OK) {
        snprintf(note, n, "the server refused adjacentTo: %s", jf_err_text(e));
        return 1;
    }
    /* The episode asked about is always in the answer. */
    if (got < 1) {
        snprintf(note, n, "adjacentTo returned nothing at all");
        return 1;
    }
    snprintf(note, n, "%d around \"%s\"", got, rail[have].name);
    return 0;
}

#endif /* !__PSP__ */

void test_jellyfin_query_register(void) {
#ifndef __PSP__
    selftest_add("jfquery", "a mark is a post and unmarking a delete", t_a_mark_is_a_post_and_unmarking_a_delete);
    selftest_add("jfquery", "one item brings its synopsis and its marks", t_one_item_brings_its_synopsis_and_its_marks);
    selftest_add("jfquery", "tracks are read and the chosen ones are asked for", t_tracks_are_read_and_the_chosen_ones_are_asked_for);
    selftest_add("jfquery", "a track is found again in another file", t_a_track_is_found_again_in_another_file);
    selftest_add("jfquery", "adjacent asks the episodes route and reads it back", t_adjacent_asks_the_episodes_route_and_reads_it_back);
    selftest_add("jfquery", "adjacent without a series makes no request", t_adjacent_without_a_series_makes_no_request);
    selftest_add("jfquery", "only the token in force signs in again", t_only_the_token_in_force_signs_in_again);
    selftest_add("jfquery", "real adjacent episodes are accepted", t_real_adjacent_episodes_are_accepted);
#endif
}

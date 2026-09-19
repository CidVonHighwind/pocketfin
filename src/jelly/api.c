#include "jelly/api.h"

#include "io/http.h"
#include "base/json.h"
#include "base/log.h"
#include "base/version.h"
#include "port/platform.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONN_FILE "jellyfin.txt"

static jf_conn        g_conn;
static char           g_device[JF_ID_LEN];
static char           g_token[JF_TOKEN_LEN];
static char           g_user_id[JF_ID_LEN];
static char           g_server_name[JF_NAME_LEN];
static platform_lock *g_lock;

static platform_lock *lock(void) {
    if (!g_lock) g_lock = platform_lock_new("jellyfin");
    return g_lock;
}

const char *jf_err_text(jf_err e) {
    switch (e) {
    case JF_OK: return "ok";
    case JF_ERR_OFFLINE: return "the server did not answer";
    case JF_ERR_AUTH: return "the session is no longer valid";
    case JF_ERR_MISSING: return "it is not there";
    case JF_ERR_SERVER: return "the server refused";
    case JF_ERR_GARBLED: return "the reply could not be read";
    default: return "?";
    }
}

static jf_err err_from_status(int status) {
    if (status <= 0) return JF_ERR_OFFLINE;
    if (status == 401 || status == 403) return JF_ERR_AUTH;
    if (status == 404) return JF_ERR_MISSING;
    return JF_ERR_SERVER;
}

static jf_err err_from_result(http_result rc, int status) {
    if (rc == HTTP_NO_LINK || rc == HTTP_NO_HOST || rc == HTTP_NO_REPLY) return JF_ERR_OFFLINE;
    if (rc == HTTP_CUT || rc == HTTP_TOO_BIG) return JF_ERR_GARBLED;
    return err_from_status(status);
}

static void auth_header(const char *token, char *out, unsigned cap) {
    if (token[0])
        snprintf(out, cap,
                 "Authorization: MediaBrowser Token=\"%s\", Client=\"Pocketfin\", "
                 "Device=\"PSP\", DeviceId=\"%s\", Version=\"" POCKETFIN_VERSION "\"\r\n",
                 token, g_device);
    else
        snprintf(out, cap,
                 "Authorization: MediaBrowser Client=\"Pocketfin\", Device=\"PSP\", "
                 "DeviceId=\"%s\", Version=\"" POCKETFIN_VERSION "\"\r\n",
                 g_device);
}

static void trim(char *s) {
    char *e;

    while (*s == ' ' || *s == '\t') memmove(s, s + 1, strlen(s));
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
}

static void put(char *dst, unsigned cap, const char *src, const char *name, const char *path) {
    unsigned n = (unsigned)strlen(src);

    if (n >= cap) {
        log_printf("jellyfin: %s gives a %s of %u characters and only %u fit", path, name, n, cap - 1);
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static int named(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return !*a && !*b;
}

/* Hand-edited: `=` or `:` read as a space, names ignore case, and a value
   runs to the end of the line so a password may hold spaces. */
int jf_conn_parse(const char *text, jf_conn *c, const char *path) {
    char line[160];
    int  got_host = 0;

    if (!c || !text) return -1;
    memset(c, 0, sizeof(*c));
    c->port = 8096;
    if (!path) path = "the connection file";

    while (*text) {
        const char *eol = strchr(text, '\n');
        unsigned    len = eol ? (unsigned)(eol - text) : (unsigned)strlen(text);
        char       *hash;
        char       *value;
        char        name[32];
        unsigned    i = 0;

        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, text, len);
        line[len] = 0;
        text += eol ? (len + 1) : len;

        hash = strchr(line, '#');
        if (hash) *hash = 0;
        trim(line);
        if (!line[0]) continue;

        value = line;
        while (*value && *value != ' ' && *value != '\t' && *value != '=' && *value != ':') {
            if (i + 1 < sizeof(name)) name[i++] = *value;
            value++;
        }
        name[i] = 0;
        while (*value == ' ' || *value == '\t' || *value == '=' || *value == ':') value++;
        trim(value);
        if (!*value) continue;

        if (named(name, "host") || named(name, "server")) {
            put(c->host, sizeof(c->host), value, "host", path);
            got_host = 1;
        } else if (named(name, "port")) {
            c->port = atoi(value);
        } else if (named(name, "user") || named(name, "username")) {
            put(c->user, sizeof(c->user), value, "user", path);
        } else if (named(name, "password") || named(name, "pass")) {
            put(c->pass, sizeof(c->pass), value, "password", path);
        } else if (named(name, "profile")) {
            c->profile = atoi(value);
        } else {
            log_printf("jellyfin: %s has a setting called \"%s\" that nothing reads", path, name);
        }
    }

    if (!got_host) {
        log_printf("jellyfin: %s names no host", path);
        return -1;
    }
    if (c->port <= 0 || c->port > 65535) {
        log_printf("jellyfin: %s asks for port %d, which is not a port", path, c->port);
        return -1;
    }
    return 0;
}

static unsigned read_file(const char *path, char *out, unsigned cap) {
    FILE    *f = fopen(path, "rb");
    unsigned n = 0;

    if (f) {
        n = (unsigned)fread(out, 1, cap - 1, f);
        fclose(f);
    }
    out[n] = 0;
    return n;
}

int jf_load_conn(jf_conn *c) {
    char     path[128];
    char     text[1024];
    unsigned got;

    if (!c) return -1;

    snprintf(path, sizeof(path), "%s%s", platform_data_dir(), CONN_FILE);
    got = read_file(path, text, sizeof(text));
    if (!got) {
        snprintf(path, sizeof(path), "run/%s", CONN_FILE);
        got = read_file(path, text, sizeof(text));
    }
    if (!got) {
        log_printf("jellyfin: no %s", path);
        return -1;
    }

    if (jf_conn_parse(text, c, path) != 0) return -1;
    log_printf("jellyfin: %s:%d", c->host, c->port);
    return 0;
}

void jf_use(const jf_conn *c) {
    platform_lock_take(lock());
    g_conn = *c;
    /* Signing in revokes the token of anything else using the same id: a
       desktop build sharing the console's killed its film. */
    snprintf(g_device, sizeof(g_device), "%s", platform_cpu_mhz() ? "pocketfin-psp" : "pocketfin-dev");
    platform_lock_give(lock());
}

void jf_address(char *host, unsigned n, int *port) {
    platform_lock_take(lock());
    snprintf(host, n, "%s", g_conn.host);
    if (port) *port = g_conn.port;
    platform_lock_give(lock());
}

const char *jf_user(void) { return g_conn.user; }
const char *jf_server_name(void) { return g_server_name; }
int         jf_signed_in(void) { return g_user_id[0] != 0; }

static int creds(char *token, char *user_id) {
    int in;

    platform_lock_take(lock());
    snprintf(token, JF_TOKEN_LEN, "%s", g_token);
    if (user_id) snprintf(user_id, JF_ID_LEN, "%s", g_user_id);
    in = g_user_id[0] != 0;
    platform_lock_give(lock());
    return in;
}

/* Caller holds the lock, so two threads refused together sign in once. The
   old token stays until a new one arrives: cleared, one failed sign-in left
   every later call refused without ever trying again. */
static jf_err sign_in(void) {
    /* 3,040 B measured. Static memory here is usbhostfs's: see the heap note
       in shell/psp/main.c. */
    static char reply[8 * 1024];
    char        hdr[256], body[128], token[JF_TOKEN_LEN], user_id[JF_ID_LEN];
    http_resp   r;
    http_result rc;

    auth_header("", hdr, sizeof(hdr));
    snprintf(body, sizeof(body), "{\"Username\":\"%s\",\"Pw\":\"%s\"}", g_conn.user, g_conn.pass);
    rc = http_request(g_conn.host, g_conn.port, "POST", "/Users/AuthenticateByName", hdr, body, reply, sizeof(reply), &r);
    memset(body, 0, sizeof(body));

    if (rc != HTTP_OK) {
        log_printf("jellyfin: sign in -- %s%s%s", http_result_text(rc), r.err[0] ? ", " : "", r.err);
        return err_from_result(rc, r.status);
    }
    if (r.status != 200) {
        log_printf("jellyfin: sign in -- HTTP %d", r.status);
        return err_from_status(r.status);
    }
    /* The first "Id" is the User's. */
    if (json_str(r.body, "AccessToken", token, sizeof(token)) != 0 || json_str(r.body, "Id", user_id, sizeof(user_id)) != 0) {
        log_line("jellyfin: the sign-in reply carried no token or user");
        return JF_ERR_GARBLED;
    }
    memcpy(g_token, token, sizeof(g_token));
    memcpy(g_user_id, user_id, sizeof(g_user_id));
    (void)json_str(r.body, "ServerName", g_server_name, sizeof(g_server_name));
    log_printf("jellyfin: signed in, token %u chars", (unsigned)strlen(g_token));
    return JF_OK;
}

jf_err jf_connect(void) {
    jf_err e;

    platform_lock_take(lock());
    e = sign_in();
    platform_lock_give(lock());
    return e;
}

jf_err jf_reauth(const char *refused) {
    jf_err e = JF_OK;

    platform_lock_take(lock());
    if (strstr(refused, g_token)) {
        log_line("jellyfin: the token was refused -- signing in again");
        e = sign_in();
    }
    platform_lock_give(lock());
    return e;
}

/* `ok` is a status accepted beside 200. The body is resent on the retry: a
   PlaybackInfo without its device profile offers direct play this console
   cannot decode. */
static jf_err exchange(jf_buf *b, const char *method, const char *path, const char *body, int ok, http_resp *r) {
    char        hdr[256], token[JF_TOKEN_LEN], host[JF_HOST_LEN];
    int         port, retried = 0;
    http_result rc;

    r->status = 0;
    if (!b || !b->p || !b->cap) return JF_ERR_GARBLED;
    for (;;) {
        if (!creds(token, 0)) return JF_ERR_AUTH;
        auth_header(token, hdr, sizeof(hdr));
        jf_address(host, sizeof(host), &port);
        rc      = http_request(host, port, method, path, hdr, body, b->p, b->cap, r);
        b->cost = r->cost;

        if (rc != HTTP_OK) {
            log_printf("jellyfin: %s -- %s%s%s", path, http_result_text(rc), r->err[0] ? ", " : "", r->err);
            return err_from_result(rc, r->status);
        }
        if ((r->status == 401 || r->status == 403) && !retried) {
            jf_err e;

            retried = 1;
            e       = jf_reauth(hdr);
            if (e != JF_OK) return e;
            continue;
        }
        if (r->status != 200 && r->status != ok) {
            log_printf("jellyfin: %s -- HTTP %d", path, r->status);
            return err_from_status(r->status);
        }
        return JF_OK;
    }
}

static jf_err get_json(jf_buf *b, const char *path, http_resp *r) { return exchange(b, "GET", path, 0, 0, r); }

static jf_err user_flag(jf_buf *b, const char *route, const char *id, int on) {
    char      path[192], uid[JF_ID_LEN], token[JF_TOKEN_LEN];
    http_resp r;
    jf_err    e;

    if (!id || !id[0]) return JF_ERR_GARBLED;
    if (!creds(token, uid)) return JF_ERR_AUTH;
    snprintf(path, sizeof(path), "/%s/%s?userId=%s", route, id, uid);
    e = exchange(b, on ? "POST" : "DELETE", path, 0, 204, &r);
    log_printf("jellyfin: %s %s -- %s, HTTP %d", on ? "POST" : "DELETE", route, jf_err_text(e), r.status);
    return e;
}

jf_err jf_set_played(jf_buf *b, const char *id, int on) { return user_flag(b, "UserPlayedItems", id, on); }

jf_err jf_set_favorite(jf_buf *b, const char *id, int on) { return user_flag(b, "UserFavoriteItems", id, on); }

static void read_item(const char *obj, unsigned len, item *out) {
    char      kind[32], series[ITEM_NAME_LEN];
    long long v;

    memset(out, 0, sizeof(*out));
    out->season_no = out->episode_no = -1;

    /* Bounded to this object: a row missing a field otherwise reads the next
       row's. A null IndexNumber showed 22, from two items down. */
    (void)json_str_in(obj, len, "Id", out->id, sizeof(out->id));
    (void)json_str_in(obj, len, "Name", out->name, sizeof(out->name));

    kind[0] = 0;
    (void)json_str_in(obj, len, "Type", kind, sizeof(kind));
    if (!strcmp(kind, "Movie"))
        out->kind = ITEM_KIND_MOVIE;
    else if (!strcmp(kind, "Episode"))
        out->kind = ITEM_KIND_EPISODE;
    else if (!strcmp(kind, "Series"))
        out->kind = ITEM_KIND_SERIES;
    else if (!strcmp(kind, "Season"))
        out->kind = ITEM_KIND_SEASON;
    /* 2:3 posters; only a library itself is 16:9. */
    else if (!strcmp(kind, "BoxSet") || !strcmp(kind, "Folder"))
        out->kind = ITEM_KIND_COLLECTION;
    else
        out->kind = ITEM_KIND_FOLDER;

    series[0] = 0;
    (void)json_str_in(obj, len, "SeriesName", series, sizeof(series));
    snprintf(out->series, sizeof(out->series), "%s", series);
    (void)json_str_in(obj, len, "SeriesId", out->series_id, sizeof(out->series_id));

    v               = json_num_in(obj, len, "ParentIndexNumber", -1);
    out->season_no  = (int)v;
    v               = json_num_in(obj, len, "IndexNumber", -1);
    out->episode_no = (int)v;

    out->run_ticks    = (unsigned long long)json_num_in(obj, len, "RunTimeTicks", 0);
    out->played       = json_bool_in(obj, len, "Played", 0);
    out->favorite     = json_bool_in(obj, len, "IsFavorite", 0);
    out->resume_ticks = (unsigned long long)json_num_in(obj, len, "PlaybackPositionTicks", 0);

    {
        const char *tags = json_key_in(obj, len, "ImageTags");

        out->has_image = (tags && json_key_in(tags, len - (size_t)(tags - obj), "Primary")) ? 1 : 0;
    }
}

/* Not Overview: the longest field, and only the detail page shows it. */
#define LIST_FIELDS "UserData"

/* /Items/Latest answers a bare array, every other listing an envelope. */
static jf_err read_list(const char *json, item *out, int max, int *got, int *total) {
    int n, i, want, bare = 0;

    if (total) *total = 0;
    n = json_array_count(json, "Items");
    if (n <= 0 && !strstr(json, "\"Items\"")) {
        n    = json_bare_count(json);
        bare = 1;
    }
    if (n <= 0) {
        if (got) *got = 0;
        return (bare && json_bare_item(json, 0, 0) == 0 && json[0] != '[') ? JF_ERR_GARBLED : JF_OK;
    }

    want = n < max ? n : max;
    {
        size_t      len = 0;
        const char *obj = bare ? json_bare_item(json, 0, &len) : json_array_item(json, "Items", 0, &len);

        for (i = 0; i < want && obj; i++) {
            read_item(obj, (unsigned)len, &out[i]);
            obj = json_next_item(obj, len, &len);
        }
    }
    if (got) *got = i;
    if (total) *total = (int)json_num(json, "TotalRecordCount", (long long)i);
    return JF_OK;
}

static jf_err get_list(jf_buf *b, const char *path, item *out, int max, int *got, int *total) {
    http_resp r;
    jf_err    e;

    if (!out || max <= 0) return JF_ERR_GARBLED;
    e = get_json(b, path, &r);
    if (e != JF_OK) return e;
    return read_list(r.body, out, max, got, total);
}

int jf_track_find(const jf_track *t, int n, const jf_track *want) {
    int i;

    if (!want->name[0]) return -1;
    for (i = 0; i < n; i++)
        if (!strcmp(t[i].name, want->name)) return t[i].index;
    for (i = 0; i < n && want->lang[0]; i++)
        if (!strcmp(t[i].lang, want->lang)) return t[i].index;
    return -1;
}

static void read_tracks(const char *obj, size_t len, jf_tracks *t) {
    size_t      slen = 0;
    const char *s    = json_key_in(obj, len, "MediaStreams") ? json_array_item(obj, "MediaStreams", 0, &slen) : 0;

    t->audio_n = t->sub_n = 0;
    t->audio_default      = -1;
    for (; s; s = json_next_item(s, slen, &slen)) {
        char      kind[16] = "";
        jf_track *to       = 0;

        (void)json_str_in(s, slen, "Type", kind, sizeof(kind));
        if (!strcmp(kind, "Audio") && t->audio_n < JF_TRACK_MAX) {
            to = &t->audio[t->audio_n++];
            if (t->audio_default < 0 && json_bool_in(s, slen, "IsDefault", 0)) t->audio_default = (int)json_num_in(s, slen, "Index", -1);
        } else if (!strcmp(kind, "Subtitle") && t->sub_n < JF_TRACK_MAX) {
            to = &t->sub[t->sub_n++];
        }
        if (!to) continue;

        to->index = (int)json_num_in(s, slen, "Index", -1);
        (void)json_str_in(s, slen, "Language", to->lang, sizeof(to->lang));
        if (json_str_in(s, slen, "DisplayTitle", to->name, sizeof(to->name)) != 0) snprintf(to->name, sizeof(to->name), "Track %d", to->index);
    }
}

jf_err jf_item(jf_buf *b, const char *id, item *out_item, char *out, unsigned outlen, jf_tracks *tracks) {
    char        path[192], uid[JF_ID_LEN], token[JF_TOKEN_LEN];
    http_resp   r;
    jf_err      e;
    size_t      len = 0;
    const char *obj;

    if (out && outlen) out[0] = 0;
    if (out_item) memset(out_item, 0, sizeof(*out_item));
    if (tracks) memset(tracks, 0, sizeof(*tracks));
    if (!id || !id[0] || !out || !outlen) return JF_ERR_GARBLED;
    if (!creds(token, uid)) return JF_ERR_AUTH;

    snprintf(path, sizeof(path), "/Items?userId=%s&ids=%s&Fields=Overview,MediaStreams," LIST_FIELDS, uid, id);
    e = get_json(b, path, &r);
    if (e != JF_OK) return e;

    obj = json_array_item(r.body, "Items", 0, &len);
    if (!obj) return JF_ERR_MISSING;
    (void)json_str_in(obj, len, "Overview", out, outlen);
    if (out_item) read_item(obj, (unsigned)len, out_item);
    if (tracks) read_tracks(obj, len, tracks);
    return JF_OK;
}

jf_err jf_views(jf_buf *b, item *out, int max, int *got) {
    char path[128], uid[JF_ID_LEN], token[JF_TOKEN_LEN];

    if (!creds(token, uid)) return JF_ERR_AUTH;
    snprintf(path, sizeof(path), "/Users/%s/Views", uid);
    return get_list(b, path, out, max, got, 0);
}

jf_err jf_resume(jf_buf *b, item *out, int max, int *got) {
    char path[192], uid[JF_ID_LEN], token[JF_TOKEN_LEN];
    int  limit = max < JF_LIST_MAX ? max : JF_LIST_MAX;

    if (!creds(token, uid)) return JF_ERR_AUTH;
    snprintf(path, sizeof(path), "/UserItems/Resume?userId=%s&Limit=%d&Fields=" LIST_FIELDS "&MediaTypes=Video&EnableTotalRecordCount=true",
             uid, limit);
    return get_list(b, path, out, max, got, 0);
}

jf_err jf_next_up(jf_buf *b, item *out, int max, int *got) {
    char path[224], uid[JF_ID_LEN], token[JF_TOKEN_LEN];
    int  limit = max < JF_LIST_MAX ? max : JF_LIST_MAX;

    if (!creds(token, uid)) return JF_ERR_AUTH;
    snprintf(path, sizeof(path), "/Shows/NextUp?userId=%s&Limit=%d&Fields=" LIST_FIELDS, uid, limit);
    return get_list(b, path, out, max, got, 0);
}

jf_err jf_adjacent(jf_buf *b, const char *series_id, const char *item_id, item *out, int max, int *got) {
    char path[224], uid[JF_ID_LEN], token[JF_TOKEN_LEN];

    if (got) *got = 0;
    if (!out || max <= 0) return JF_ERR_GARBLED;
    if (!series_id || !series_id[0] || !item_id || !item_id[0]) return JF_ERR_GARBLED;
    if (!creds(token, uid)) return JF_ERR_AUTH;
    snprintf(path, sizeof(path), "/Shows/%s/Episodes?userId=%s&adjacentTo=%s&Fields=" LIST_FIELDS, series_id, uid, item_id);
    return get_list(b, path, out, max, got, 0);
}

jf_err jf_latest(jf_buf *b, const char *parent_id, item *out, int max, int *got) {
    char path[192], uid[JF_ID_LEN], token[JF_TOKEN_LEN];
    int  limit = max < JF_LIST_MAX ? max : JF_LIST_MAX;

    if (!creds(token, uid)) return JF_ERR_AUTH;
    if (parent_id && parent_id[0])
        snprintf(path, sizeof(path), "/Items/Latest?userId=%s&parentId=%s&Limit=%d&Fields=" LIST_FIELDS, uid, parent_id, limit);
    else
        snprintf(path, sizeof(path), "/Items/Latest?userId=%s&Limit=%d&Fields=" LIST_FIELDS, uid, limit);
    return get_list(b, path, out, max, got, 0);
}

jf_err jf_items(jf_buf *b, const char *parent_id, item_sort sort, int descending, item_filter filter, item *out, int max, int *got,
                int *total) {
    char path[288], uid[JF_ID_LEN], token[JF_TOKEN_LEN];
    int  limit = max < JF_LIST_MAX ? max : JF_LIST_MAX;
    int  n;

    if (!creds(token, uid)) return JF_ERR_AUTH;

    n = snprintf(path, sizeof(path), "/Items?userId=%s&Limit=%d", uid, limit);
    /* Not Recursive, which flattens the whole library. */
    if (parent_id && parent_id[0]) n += snprintf(path + n, sizeof(path) - (unsigned)n, "&parentId=%s", parent_id);
    n += snprintf(path + n, sizeof(path) - (unsigned)n, "&SortBy=%s&SortOrder=%s", item_sort_key(sort),
                  descending ? "Descending" : "Ascending");
    if (item_filter_key(filter)) n += snprintf(path + n, sizeof(path) - (unsigned)n, "&Filters=%s", item_filter_key(filter));
    snprintf(path + n, sizeof(path) - (unsigned)n, "&Fields=%s", LIST_FIELDS);
    return get_list(b, path, out, max, got, total);
}

/* In place: the server ignores a repeated parameter. */
static void url_set(char *url, unsigned cap, const char *key, const char *value) {
    char    *at = strstr(url, key);
    char    *val, *end;
    unsigned klen = (unsigned)strlen(key), vlen = (unsigned)strlen(value), oldlen;
    unsigned len = (unsigned)strlen(url);

    if (!at) return;
    val = at + klen;
    end = val;
    while (*end && *end != '&') end++;
    oldlen = (unsigned)(end - val);

    if (len - oldlen + vlen + 1 > cap) return;
    memmove(val + vlen, end, strlen(end) + 1);
    memcpy(val, value, vlen);
}

/* The %u fields: streaming ceiling, static ceiling, video track (130 kbit/s
   left to audio). Empty DirectPlayProfiles forces a transcode: the originals
   are H.264 High, which the Media Engine refuses. */
static const char kDeviceProfile[] =
    "{\"DeviceProfile\":{\"Name\":\"PSP\",\"MaxStreamingBitrate\":%u,"
    "\"MaxStaticBitrate\":%u,\"MusicStreamingTranscodingBitrate\":128000,"
    "\"DirectPlayProfiles\":[],"
    "\"TranscodingProfiles\":[{\"Type\":\"Video\",\"Container\":\"mp4\","
    "\"VideoCodec\":\"h264\",\"AudioCodec\":\"aac\",\"Protocol\":\"http\","
    "\"Context\":\"Streaming\",\"MaxAudioChannels\":\"2\",\"CopyTimestamps\":false,"
    "\"EnableSubtitlesInManifest\":false,\"BreakOnNonKeyFrames\":false}],"
    "\"ContainerProfiles\":[],"
    "\"CodecProfiles\":[{\"Type\":\"Video\",\"Codec\":\"h264\",\"Conditions\":["
    "{\"Condition\":\"EqualsAny\",\"Property\":\"VideoProfile\","
    "\"Value\":\"baseline|constrained baseline|main\",\"IsRequired\":true},"
    "{\"Condition\":\"LessThanEqual\",\"Property\":\"VideoLevel\",\"Value\":\"31\",\"IsRequired\":true},"
    "{\"Condition\":\"LessThanEqual\",\"Property\":\"Width\",\"Value\":\"480\",\"IsRequired\":true},"
    "{\"Condition\":\"LessThanEqual\",\"Property\":\"Height\",\"Value\":\"272\",\"IsRequired\":true},"
    "{\"Condition\":\"LessThanEqual\",\"Property\":\"VideoBitrate\",\"Value\":\"%u\",\"IsRequired\":true}]},"
    "{\"Type\":\"VideoAudio\",\"Codec\":\"aac\",\"Conditions\":["
    "{\"Condition\":\"LessThanEqual\",\"Property\":\"AudioChannels\",\"Value\":\"2\",\"IsRequired\":true},"
    "{\"Condition\":\"LessThanEqual\",\"Property\":\"AudioSampleRate\",\"Value\":\"44100\",\"IsRequired\":true},"
    "{\"Condition\":\"LessThanEqual\",\"Property\":\"AudioBitrate\",\"Value\":\"128000\",\"IsRequired\":true},"
    "{\"Condition\":\"EqualsAny\",\"Property\":\"AudioProfile\",\"Value\":\"lc\",\"IsRequired\":true}]}],"
    "\"SubtitleProfiles\":[]}}";

static jf_err stream_url(jf_buf *b, const char *item_id, unsigned max_bps, int audio, int sub, char *out, unsigned outlen,
                         char *session_out, unsigned sess_len) {
    char      path[320], uid[JF_ID_LEN], token[JF_TOKEN_LEN];
    char      body[1600]; /* 1,368 B measured */
    http_resp r;
    jf_err    e;
    int       n;

    if (!item_id || !item_id[0] || !out || !outlen) return JF_ERR_GARBLED;
    out[0] = 0;
    if (session_out && sess_len) session_out[0] = 0;
    if (!creds(token, uid)) return JF_ERR_AUTH;

    /* startTimeTicks=0 even when resuming: the segment index is the position.
       Without mediaSourceId the server ignores both stream indexes. Burned in,
       SRT, ASS and PGS all measured within 480x272. */
    n = snprintf(path, sizeof(path),
                 "/Items/%s/PlaybackInfo?userId=%s&startTimeTicks=0"
                 "&autoOpenLiveStream=true&maxStreamingBitrate=%u"
                 "&mediaSourceId=%s&subtitleStreamIndex=%d",
                 item_id, uid, max_bps, item_id, sub < 0 ? -1 : sub);
    if (audio >= 0) snprintf(path + n, sizeof(path) - (unsigned)n, "&audioStreamIndex=%d", audio);
    snprintf(body, sizeof(body), kDeviceProfile, max_bps, max_bps, max_bps > 130000u ? max_bps - 130000u : max_bps);

    e = exchange(b, "POST", path, body, 0, &r);
    if (e != JF_OK) return e;

    if (session_out && sess_len) (void)json_str(r.body, "PlaySessionId", session_out, sess_len);

    if (json_str(r.body, "TranscodingUrl", out, outlen) != 0) {
        log_line("jellyfin: PlaybackInfo offered no TranscodingUrl");
        return JF_ERR_SERVER;
    }

    if (sub < 0) {
        url_set(out, outlen, "SubtitleStreamIndex=", "-1");
        url_set(out, outlen, "SubtitleMethod=", "External");
    }
    /* No Framerate= override: the server restamps 23.976 as 29.97 without
       adding frames, and every segment arrives 20% short. */
    return JF_OK;
}

jf_err jf_hls_open(jf_buf *b, const char *item_id, uint64_t start_ticks, unsigned max_bps, int audio, int sub, jf_hls *out,
                   char *session_out, unsigned sess_len) {
    char     url[JF_URL_LEN];
    char    *q, *slash;
    unsigned dlen;
    jf_err   e;

    if (!out) return JF_ERR_GARBLED;
    memset(out, 0, sizeof(*out));

    e = stream_url(b, item_id, max_bps, audio, sub, url, sizeof(url), session_out, sess_len);
    if (e != JF_OK) return e;

    q = strchr(url, '?');
    if (!q) {
        log_line("jellyfin: the transcoding url has no query");
        return JF_ERR_GARBLED;
    }
    *q++ = 0;

    /* "/videos/{id}/stream.mp4" -> "/videos/{id}" */
    slash = strrchr(url, '/');
    if (!slash) {
        log_line("jellyfin: the transcoding url has no path");
        return JF_ERR_GARBLED;
    }
    *slash = 0;
    dlen   = (unsigned)strlen(url);
    if (dlen + 1 > sizeof(out->dir)) {
        log_printf("jellyfin: the transcoding path is %u bytes, over %u", dlen, (unsigned)sizeof(out->dir) - 1u);
        return JF_ERR_GARBLED;
    }
    memcpy(out->dir, url, dlen + 1);

    if (snprintf(out->query, sizeof(out->query), "%s&segmentContainer=mp4&segmentLength=1", q) >= (int)sizeof(out->query)) {
        log_line("jellyfin: the segment query does not fit");
        return JF_ERR_GARBLED;
    }

    out->first_seg = (int)(start_ticks / JF_HLS_SEG_TICKS);
    log_printf("hls: %s, first segment %d, audio %d, subtitle %d", out->dir, out->first_seg, audio, sub);
    return JF_OK;
}

/* Newer servers write ApiKey, older ones api_key. */
static int is_api_key(const char *name, unsigned n) {
    static const char *SPELT[] = {"api_key", "apikey"};
    unsigned           i, j;

    for (i = 0; i < 2; i++) {
        if (strlen(SPELT[i]) != n) continue;
        for (j = 0; j < n && tolower((unsigned char)name[j]) == SPELT[i][j]; j++) {}
        if (j == n) return 1;
    }
    return 0;
}

static void with_token(const char *q, const char *token, char *out, unsigned n) {
    unsigned used = 0;

    out[0] = 0;
    while (*q) {
        const char *amp = strchr(q, '&');
        const char *end = amp ? amp : q + strlen(q);
        const char *eq  = (const char *)memchr(q, '=', (size_t)(end - q));
        int         w;

        if (eq && is_api_key(q, (unsigned)(eq - q)))
            w = snprintf(out + used, n - used, "%s%.*s=%s", used ? "&" : "", (int)(eq - q), q, token);
        else
            w = snprintf(out + used, n - used, "%s%.*s", used ? "&" : "", (int)(end - q), q);
        if (w < 0 || (unsigned)w >= n - used) return;
        used += (unsigned)w;
        q = amp ? amp + 1 : end;
    }
}

void jf_hls_url(const jf_hls *h, int seg, char *out, unsigned outlen) {
    char               query[JF_URL_LEN], token[JF_TOKEN_LEN];
    unsigned long long at = (seg > 0) ? (unsigned long long)seg * JF_HLS_SEG_TICKS : 0ull;

    if (!h || !out || !outlen) return;
    creds(token, 0);
    with_token(h->query, token, query, sizeof(query));
    /* Both ticks values are ignored by the server and required: HTTP 400. */
    snprintf(out, outlen, "%s/hls1/main/%d.mp4?%s&runtimeTicks=%llu&actualSegmentLengthTicks=%llu", h->dir, seg, query, at,
             (seg >= 0) ? (unsigned long long)JF_HLS_SEG_TICKS : 0ull);
}

static jf_err report(jf_buf *b, const char *path, const char *item_id, const char *session, uint64_t ticks, int paused) {
    char      body[320];
    http_resp r;

    if (!item_id || !item_id[0]) return JF_ERR_GARBLED;
    snprintf(body, sizeof(body),
             "{\"ItemId\":\"%s\",\"PlaySessionId\":\"%s\",\"PositionTicks\":%llu,"
             "\"IsPaused\":%s,\"CanSeek\":true}",
             item_id, session ? session : "", (unsigned long long)ticks, paused ? "true" : "false");
    return exchange(b, "POST", path, body, 204, &r);
}

jf_err jf_report_start(jf_buf *b, const char *item_id, const char *session, uint64_t ticks) {
    return report(b, "/Sessions/Playing", item_id, session, ticks, 0);
}

jf_err jf_report_progress(jf_buf *b, const char *item_id, const char *session, uint64_t ticks, int paused) {
    return report(b, "/Sessions/Playing/Progress", item_id, session, ticks, paused);
}

jf_err jf_report_stop(jf_buf *b, const char *item_id, const char *session, uint64_t ticks) {
    return report(b, "/Sessions/Playing/Stopped", item_id, session, ticks, 0);
}

jf_err jf_stop_encoding(jf_buf *b, const char *session) {
    char      path[256];
    http_resp r;

    snprintf(path, sizeof(path), "/Videos/ActiveEncodings?deviceId=%s&playSessionId=%s", g_device, session ? session : "");
    return exchange(b, "DELETE", path, 0, 204, &r);
}

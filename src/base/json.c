/* See json.h. The escape handling and UTF-8 fold are hard-won; re-deriving
   them is how a transcoding URL arrives with &amp; in place of every
   ampersand. */
#include "base/json.h"

#include <string.h>

/* Past the key and any spaces, at the value; 0 when the key is not between
   `json` and `end` (`end` NULL means the document's own terminator).

   The pattern carries the opening quote and colon, so "Name" cannot match
   inside "SortName". Searched on the key's second character, since every key
   opens with a quote, the most common byte in the document. The bound is
   searched rather than checked afterwards: a key absent from this item is
   absent from the next too, and scanning to the terminator would rescan the
   whole reply once per item. */
static const char *value_between(const char *json, const char *end, const char *key) {
    char        pattern[64];
    size_t      plen;
    const char *p;

    if (!json || !key) return 0;
    plen = strlen(key) + 3;
    if (plen >= sizeof(pattern)) return 0;
    pattern[0] = '"';
    memcpy(pattern + 1, key, plen - 3);
    pattern[plen - 2] = '"';
    pattern[plen - 1] = ':';

    if (!end) end = json + strlen(json);
    if ((size_t)(end - json) < plen) return 0;

    for (p = json;;) {
        const char *at = (const char *)memchr(p + 1, pattern[1], (size_t)(end - plen - p) + 1);
        if (!at) return 0;
        if (memcmp(at - 1, pattern, plen) == 0) {
            at += plen - 1;
            while (at < end && *at == ' ') at++;
            return at;
        }
        p = at;
        if (p > end - plen) return 0;
    }
}

/* Jellyfin escapes every ampersand in a URL as \u0026, so a value copied
   verbatim has its query string joined by six-character noise. Only the
   escapes these replies contain are handled: \uXXXX and the single-character
   ones. */
int json_str_in(const char *json, size_t len, const char *key, char *out, size_t outlen) {
    const char *end;
    const char *p;
    size_t      n = 0;

    if (!json || !key || !out || outlen == 0) return -1;
    out[0] = 0;
    end    = len ? json + len : 0;

    p = value_between(json, end, key);
    if (!p) return -1;
    if (!end) end = json + strlen(json);
    if (p >= end || *p != '"') return -1;
    p++;

    while (p < end && *p && *p != '"' && n + 1 < outlen) {
        if (*p != '\\') {
            /* Raw UTF-8 folded to Latin-1: C2/C3 two-byte sequences are the
             * Latin-1 block; longer ones become one '?' and are consumed
             * whole, so continuation bytes cannot reach the screen as
             * punctuation. */
            unsigned char c = (unsigned char)*p;

            if ((c & 0xE0u) == 0xC0u && (p[1] & 0xC0) == 0x80) {
                unsigned cp = ((unsigned)(c & 0x1Fu) << 6) | (unsigned)(p[1] & 0x3F);

                out[n++] = (char)(cp < 256u ? cp : '?');
                p += 2;
            } else if ((c & 0xF0u) == 0xE0u && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
                out[n++] = '?';
                p += 3;
            } else if ((c & 0xF8u) == 0xF0u && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
                out[n++] = '?';
                p += 4;
            } else {
                out[n++] = *p++;
            }
            continue;
        }

        p++;
        switch (*p) {
        case 'u': {
            unsigned v = 0;
            int      i;
            for (i = 1; i <= 4; i++) {
                char c = p[i];
                if (c >= '0' && c <= '9')
                    v = (v << 4) | (unsigned)(c - '0');
                else if (c >= 'a' && c <= 'f')
                    v = (v << 4) | (unsigned)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F')
                    v = (v << 4) | (unsigned)(c - 'A' + 10);
                else {
                    v = 0;
                    break;
                }
            }
            if (v > 0 && v < 256) {
                out[n++] = (char)v;
                p += 5;
            } else if (v > 0) {
                /* Above Latin-1. Always consumed, so \u2013 cannot reach the
                 * screen spelt out; substitutes are what turns up in titles
                 * and synopses, the dashes above all. */
                switch (v) {
                case 0x2010:
                case 0x2011:
                case 0x2012:
                case 0x2013:
                case 0x2014:
                case 0x2015: out[n++] = '-'; break;
                case 0x2018:
                case 0x2019:
                case 0x201B: out[n++] = '\''; break;
                case 0x201C:
                case 0x201D:
                case 0x201F: out[n++] = '"'; break;
                case 0x00A0:
                case 0x2007:
                case 0x202F: out[n++] = ' '; break;
                case 0x2022:
                case 0x00B7: out[n++] = '*'; break;
                case 0x2026:
                    if (n + 3 < outlen) {
                        out[n++] = '.';
                        out[n++] = '.';
                        out[n++] = '.';
                    } else {
                        out[n++] = '.';
                    }
                    break;
                default: out[n++] = '?'; break;
                }
                p += 5;
            } else {
                /* Not four hex digits, so not an escape. */
                out[n++] = '\\';
            }
            break;
        }
        case '"':
            out[n++] = '"';
            p++;
            break;
        case '\\':
            out[n++] = '\\';
            p++;
            break;
        case '/':
            out[n++] = '/';
            p++;
            break;
        case 'n':
            out[n++] = '\n';
            p++;
            break;
        case 'r':
            out[n++] = '\r';
            p++;
            break;
        case 't':
            out[n++] = '\t';
            p++;
            break;
        case 0: break;
        default: out[n++] = *p++; break;
        }
    }

    /* Ran out of field before running out of value: marked, or a name cut
     * at the bound is indistinguishable from one that ended there. */
    if (p < end && *p && *p != '"' && n >= 3) {
        out[n - 3] = '.';
        out[n - 2] = '.';
        out[n - 1] = '.';
    }

    out[n] = 0;
    return 0;
}

int json_str(const char *json, const char *key, char *out, size_t outlen) { return json_str_in(json, 0, key, out, outlen); }

/* Whole numbers only; a fraction after the point is ignored, since
   everything read here is a count. A `null` value has no digits, so `def`
   comes back rather than a quiet zero. */
long long json_num_in(const char *json, size_t len, const char *key, long long def) {
    const char *end = len ? json + len : 0;
    const char *p   = value_between(json, end, key);
    long long   v   = 0;
    int         neg = 0, any = 0;

    if (!p) return def;
    if (!end) end = json + strlen(json);

    if (p < end && *p == '-') {
        neg = 1;
        p++;
    }
    while (p < end && *p >= '0' && *p <= '9') {
        v   = v * 10 + (*p++ - '0');
        any = 1;
    }
    if (!any) return def;
    return neg ? -v : v;
}

long long json_num(const char *json, const char *key, long long def) { return json_num_in(json, 0, key, def); }

int json_bool_in(const char *json, size_t len, const char *key, int def) {
    const char *end = len ? json + len : 0;
    const char *p   = value_between(json, end, key);

    if (!p) return def;
    if (!end) end = json + strlen(json);
    if ((size_t)(end - p) >= 4 && strncmp(p, "true", 4) == 0) return 1;
    if ((size_t)(end - p) >= 5 && strncmp(p, "false", 5) == 0) return 0;
    return def;
}

const char *json_key_in(const char *json, size_t len, const char *key) { return value_between(json, len ? json + len : 0, key); }

/* Braces are counted and quoted strings skipped, so a brace inside a title
   does not end the object early. */
static const char *object_at(const char *arr, int index, size_t *len) {
    int at = 0;

    while (*arr) {
        const char *start;
        int         depth = 0, in_str = 0;

        while (*arr && *arr != '{' && *arr != ']') arr++;
        if (*arr != '{') return 0;

        start = arr;
        for (; *arr; arr++) {
            if (in_str) {
                if (*arr == '\\' && arr[1])
                    arr++;
                else if (*arr == '"')
                    in_str = 0;
                continue;
            }
            if (*arr == '"') {
                in_str = 1;
                continue;
            }
            if (*arr == '{')
                depth++;
            else if (*arr == '}') {
                if (--depth == 0) {
                    arr++;
                    break;
                }
            }
        }
        if (depth != 0) return 0;

        if (at == index) {
            if (len) *len = (size_t)(arr - start);
            return start;
        }
        at++;
    }
    return 0;
}

/* Just inside the array's opening bracket, or 0. */
static const char *array_of(const char *json, const char *key) {
    const char *p = value_between(json, 0, key);

    return (p && *p == '[') ? p + 1 : 0;
}

static const char *bare(const char *json) {
    if (!json) return 0;
    while (*json == ' ' || *json == 0x0A || *json == 0x0D || *json == 0x09) json++;
    return *json == '[' ? json + 1 : 0;
}

const char *json_array_item(const char *json, const char *key, int index, size_t *len) {
    const char *p = array_of(json, key);

    return p ? object_at(p, index, len) : 0;
}

/* One forward pass: counting with object_at(N) walked the array once per
   item, about 8,000 traversals for a 128-row listing. */
static int count_from(const char *p) {
    size_t len = 0;
    int    n   = 0;

    for (p = object_at(p, 0, &len); p; p = object_at(p + len, 0, &len)) n++;
    return n;
}

int json_array_count(const char *json, const char *key) {
    const char *p = array_of(json, key);

    return p ? count_from(p) : 0;
}

const char *json_bare_item(const char *json, int index, size_t *len) {
    const char *p = bare(json);

    return p ? object_at(p, index, len) : 0;
}

int json_bare_count(const char *json) {
    const char *p = bare(json);

    return p ? count_from(p) : 0;
}

const char *json_next_item(const char *obj, size_t obj_len, size_t *len) {
    if (!obj) return 0;
    return object_at(obj + obj_len, 0, len);
}

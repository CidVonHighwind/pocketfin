/* See base/json.h. Every fault here is a reply READ WRONG rather than one that
 * failed to arrive, which a check of request success never sees. */

#include "base/json.h"

#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

/* A bare search for "Name" finds "SortName", which reads as a server sending
   nonsense rather than a reader bug. */
static int t_a_key_does_not_match_inside_a_longer_one(char *note, unsigned n) {
    static const char J[] = "{\"SortName\":\"wrong\",\"Name\":\"right\"}";
    char              out[32];

    if (json_str(J, "Name", out, sizeof(out)) != 0) {
        snprintf(note, n, "Name was not found at all");
        return 1;
    }
    if (strcmp(out, "right") != 0) {
        snprintf(note, n, "Name read \"%s\" -- it matched inside SortName", out);
        return 1;
    }
    return 0;
}

/* Jellyfin escapes every ampersand, and a URL copied with the escape still in
   it is refused by the server. */
static int t_escapes_are_decoded(char *note, unsigned n) {
    static const char J[] = "{\"Url\":\"a\\u0026b\\u0026c\",\"Q\":\"say \\\"hi\\\"\"}";
    char              out[64];

    if (json_str(J, "Url", out, sizeof(out)) != 0 || strcmp(out, "a&b&c") != 0) {
        snprintf(note, n, "the url read \"%s\", wanted a&b&c", out);
        return 1;
    }
    if (json_str(J, "Q", out, sizeof(out)) != 0 || strcmp(out, "say \"hi\"") != 0) {
        snprintf(note, n, "the quoted string read \"%s\"", out);
        return 1;
    }
    return 0;
}

/* Above Latin-1 there is no glyph, and the ESCAPE IS ALWAYS CONSUMED: left in
   place it reaches the screen spelt out. An en dash becomes a hyphen because
   Jellyfin's synopses are full of them. */
static int t_characters_with_no_glyph_are_substituted(char *note, unsigned n) {
    static const char J[] = "{\"A\":\"one\\u2013two\",\"B\":\"\\u201Cq\\u201D\","
                            "\"C\":\"x\\u4E2Dy\"}";
    char              out[64];

    if (json_str(J, "A", out, sizeof(out)) != 0 || strcmp(out, "one-two") != 0) {
        snprintf(note, n, "an en dash gave \"%s\"", out);
        return 1;
    }
    if (json_str(J, "B", out, sizeof(out)) != 0 || strcmp(out, "\"q\"") != 0) {
        snprintf(note, n, "curly quotes gave \"%s\"", out);
        return 1;
    }
    if (json_str(J, "C", out, sizeof(out)) != 0 || strcmp(out, "x?y") != 0) {
        snprintf(note, n,
                 "a CJK character gave \"%s\" -- it must become one mark and "
                 "be consumed whole",
                 out);
        return 1;
    }
    return 0;
}

/* Raw UTF-8 arrives too: the server escapes some accents and sends others as
   themselves, and a two-byte sequence IS the Latin-1 block. */
static int t_raw_utf8_folds_to_one_byte(char *note, unsigned n) {
    static const char J[] = "{\"N\":\"Am\xC3\xA9lie\"}";
    char              out[32];

    if (json_str(J, "N", out, sizeof(out)) != 0) {
        snprintf(note, n, "not found");
        return 1;
    }
    if (strlen(out) != 6) {
        snprintf(note, n,
                 "\"Amelie\" with an accent came out %u bytes, wanted 6 -- a "
                 "continuation byte reached the string",
                 (unsigned)strlen(out));
        return 1;
    }
    return 0;
}

/* Unmarked, a truncated name reads as a rendering fault. */
static int t_a_truncated_value_says_so(char *note, unsigned n) {
    static const char J[] = "{\"N\":\"a very long name indeed, far longer than the room\"}";
    char              out[12];

    if (json_str(J, "N", out, sizeof(out)) != 0) {
        snprintf(note, n, "not found");
        return 1;
    }
    if (strcmp(out + strlen(out) - 3, "...") != 0) {
        snprintf(note, n, "cut to \"%s\" with no mark", out);
        return 1;
    }
    return 0;
}

static int t_numbers_and_booleans_are_read(char *note, unsigned n) {
    static const char J[] = "{\"Ticks\":72000000000,\"Idx\":-1,\"Played\":true,"
                            "\"Fav\":false}";

    if (json_num(J, "Ticks", 0) != 72000000000LL) {
        snprintf(note, n, "a run time read %lld", json_num(J, "Ticks", 0));
        return 1;
    }
    if (json_num(J, "Idx", 99) != -1) {
        snprintf(note, n, "a negative read %lld", json_num(J, "Idx", 99));
        return 1;
    }
    if (json_num(J, "Absent", 42) != 42) {
        snprintf(note, n, "a missing key did not give the default");
        return 1;
    }
    if (json_bool_in(J, 0, "Played", 0) != 1 || json_bool_in(J, 0, "Fav", 1) != 0) {
        snprintf(note, n, "true/false read %d/%d", json_bool_in(J, 0, "Played", 0), json_bool_in(J, 0, "Fav", 1));
        return 1;
    }
    return 0;
}

/* A listing where every row took its neighbour's name reads as a sorting
   fault, not a parsing one. */
static int t_an_array_hands_over_whole_objects(char *note, unsigned n) {
    static const char J[] = "{\"Items\":[{\"Id\":\"1\",\"Name\":\"first\"},"
                            "{\"Id\":\"2\",\"Name\":\"second\"},"
                            "{\"Id\":\"3\",\"Name\":\"third\"}],\"TotalRecordCount\":3}";
    const char       *obj;
    size_t            len = 0;
    char              buf[512], name[32];

    if (json_array_count(J, "Items") != 3) {
        snprintf(note, n, "counted %d, wanted 3", json_array_count(J, "Items"));
        return 1;
    }

    obj = json_array_item(J, "Items", 1, &len);
    if (!obj || len == 0 || len >= sizeof(buf)) {
        snprintf(note, n, "the second object came back %u bytes", (unsigned)len);
        return 1;
    }
    memcpy(buf, obj, len);
    buf[len] = 0;

    if (json_str(buf, "Name", name, sizeof(name)) != 0 || strcmp(name, "second") != 0) {
        snprintf(note, n, "the second object's Name read \"%s\"", name);
        return 1;
    }
    if (strstr(buf, "third")) {
        snprintf(note, n, "the second object carried the third's fields");
        return 1;
    }
    snprintf(note, n, "3 objects, the second is %u bytes and its own", (unsigned)len);
    return 0;
}

/* Counting braces without skipping strings splits the array, and every item
   after it is wrong. */
static int t_a_brace_in_a_string_does_not_end_an_object(char *note, unsigned n) {
    static const char J[] = "{\"Items\":[{\"Name\":\"a } brace\",\"Id\":\"1\"},{\"Name\":\"next\",\"Id\":\"2\"}]}";
    const char       *obj;
    size_t            len = 0;
    char              buf[256], id[8];

    if (json_array_count(J, "Items") != 2) {
        snprintf(note, n, "counted %d objects, wanted 2 -- a brace in a title split one", json_array_count(J, "Items"));
        return 1;
    }
    obj = json_array_item(J, "Items", 0, &len);
    if (!obj || len >= sizeof(buf)) {
        snprintf(note, n, "the first object is %u bytes", (unsigned)len);
        return 1;
    }
    memcpy(buf, obj, len);
    buf[len] = 0;
    if (json_str(buf, "Id", id, sizeof(id)) != 0 || strcmp(id, "1") != 0) {
        snprintf(note, n, "the first object's Id read \"%s\"", id);
        return 1;
    }
    return 0;
}

static int t_an_empty_array_is_not_a_fault(char *note, unsigned n) {
    static const char J[] = "{\"Items\":[],\"TotalRecordCount\":0}";

    if (json_array_count(J, "Items") != 0) {
        snprintf(note, n, "an empty array counted %d", json_array_count(J, "Items"));
        return 1;
    }
    if (json_array_item(J, "Items", 0, 0) != 0) {
        snprintf(note, n, "an empty array handed over an object");
        return 1;
    }
    return 0;
}

/* Jellyfin omits a field rather than sending it null on most rows. Read without
 * a bound, an episode with a null IndexNumber once displayed 22, belonging to
 * an item two places down. */
static const char kRagged[] = "{\"Items\":["
                              "{\"Name\":\"Family wraps a homestead\",\"SeriesName\":\"Kirsten Dirksen\","
                              "\"ParentIndexNumber\":2026,\"IndexNumber\":null},"
                              "{\"Name\":\"Let 'Em Eat Cake\",\"SeriesName\":\"Arrested Development\","
                              "\"ParentIndexNumber\":1,\"IndexNumber\":22,\"RunTimeTicks\":12000,"
                              "\"Played\":true}"
                              "]}";

static int t_a_field_the_item_lacks_is_not_taken_from_the_next(char *note, unsigned n) {
    size_t      len = 0;
    const char *obj = json_array_item(kRagged, "Items", 0, &len);
    char        name[64];
    long long   ep, run;

    if (!obj) {
        snprintf(note, n, "the first item did not come back at all");
        return 1;
    }

    ep = json_num_in(obj, len, "IndexNumber", -1);
    if (ep != -1) {
        snprintf(note, n,
                 "an absent episode number read as %lld -- it belongs "
                 "to the item below",
                 ep);
        return 1;
    }
    run = json_num_in(obj, len, "RunTimeTicks", 0);
    if (run != 0) {
        snprintf(note, n, "an absent runtime read as %lld", run);
        return 1;
    }
    if (json_bool_in(obj, len, "Played", 0) != 0) {
        snprintf(note, n, "an absent Played read as watched");
        return 1;
    }
    if (json_key_in(obj, len, "RunTimeTicks") != 0) {
        snprintf(note, n, "a key that is not in this object was found in it");
        return 1;
    }

    name[0] = 0;
    (void)json_str_in(obj, len, "SeriesName", name, sizeof(name));
    if (strcmp(name, "Kirsten Dirksen") != 0) {
        snprintf(note, n, "its own series read as \"%s\"", name);
        return 1;
    }
    if (json_num_in(obj, len, "ParentIndexNumber", -1) != 2026) {
        snprintf(note, n, "its own season number did not read back");
        return 1;
    }

    snprintf(note, n,
             "absent stays absent: episode -1, runtime 0, unwatched; "
             "series \"%s\"",
             name);
    return 0;
}

static const char *const kThree[] = {"one", "two", "three"};

static int walk_three(const char *obj, size_t len, char *note, unsigned n, const char *shape) {
    char name[32];
    int  i;

    for (i = 0; i < 3; i++) {
        if (!obj) {
            snprintf(note, n, "the %s ran out after %d of 3", shape, i);
            return 1;
        }
        name[0] = 0;
        if (json_str_in(obj, len, "Name", name, sizeof(name)) != 0 || strcmp(name, kThree[i]) != 0) {
            snprintf(note, n, "%s item %d read \"%s\", wanted \"%s\"", shape, i, name, kThree[i]);
            return 1;
        }
        obj = json_next_item(obj, len, &len);
    }
    if (obj) {
        snprintf(note, n, "the %s handed over a fourth item", shape);
        return 1;
    }
    return 0;
}

/* Asking for the Nth instead walks from the start every time, about 8,000
 * traversals for 128 rows, slow in a way only the console shows. And
 * /Items/Latest answers a BARE array where every other listing sends an
 * envelope; a reader that knows only the envelope reports Recently Added as
 * empty. */
static int t_a_listing_is_walked_forward_in_either_shape(char *note, unsigned n) {
    static const char kEnvelope[] = "{\"Items\":[{\"Name\":\"one\"},{\"Name\":\"two\"},{\"Name\":\"three\"}],\"TotalRecordCount\":3}";
    static const char kBare[]     = "[{\"Name\":\"one\"},{\"Name\":\"two\"},{\"Name\":\"three\"}]";
    size_t            len         = 0;

    if (walk_three(json_array_item(kEnvelope, "Items", 0, &len), len, note, n, "envelope") != 0) return 1;

    if (json_bare_count(kBare) != 3) {
        snprintf(note, n, "a bare array counted %d, wanted 3 -- the rail reads as empty", json_bare_count(kBare));
        return 1;
    }
    len = 0;
    if (walk_three(json_bare_item(kBare, 0, &len), len, note, n, "bare array") != 0) return 1;

    snprintf(note, n, "3 walked forward as an envelope and as a bare array");
    return 0;
}

void test_json_register(void) {
    selftest_add("json", "a key does not match inside a longer one", t_a_key_does_not_match_inside_a_longer_one);
    selftest_add("json", "escapes are decoded", t_escapes_are_decoded);
    selftest_add("json", "characters with no glyph are substituted", t_characters_with_no_glyph_are_substituted);
    selftest_add("json", "raw utf8 folds to one byte", t_raw_utf8_folds_to_one_byte);
    selftest_add("json", "a truncated value says so", t_a_truncated_value_says_so);
    selftest_add("json", "numbers and booleans are read", t_numbers_and_booleans_are_read);
    selftest_add("json", "an array hands over whole objects", t_an_array_hands_over_whole_objects);
    selftest_add("json", "a brace in a string does not end an object", t_a_brace_in_a_string_does_not_end_an_object);
    selftest_add("json", "an empty array is not a fault", t_an_empty_array_is_not_a_fault);
    selftest_add("json", "a field the item lacks is not taken from the next", t_a_field_the_item_lacks_is_not_taken_from_the_next);
    selftest_add("json", "a listing is walked forward in either shape", t_a_listing_is_walked_forward_in_either_shape);
}

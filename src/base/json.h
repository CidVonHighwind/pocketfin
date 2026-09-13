/* NOT A PARSER: a scan for "key":"value" that copies the value out. Every
 * field this client reads is a string or a number next to a known key, and
 * no allocation means no allocator to fail mid-film. */
#ifndef BASE_JSON_H
#define BASE_JSON_H

#include <stddef.h>

/* Decodes the escapes Jellyfin emits and folds UTF-8 to Latin-1 as it
 * passes. 0 on success. */
int json_str(const char *json, const char *key, char *out, size_t outlen);

/* "key":123, unquoted. `def` when the key is absent or is not a number. */
long long json_num(const char *json, const char *key, long long def);

/* The Nth object of "key":[ {...}, {...} ], as a pointer to its opening brace
 * and its length, or 0 past the end. Handed over whole so json_str reading
 * inside it cannot match a field of the next one. */
const char *json_array_item(const char *json, const char *key, int index, size_t *len);

int json_array_count(const char *json, const char *key);

/* The object after this one, or 0 at the end. Walking a listing with
 * json_array_item() instead is about 8,000 traversals for 128 rows, visibly
 * slow on the console. */
const char *json_next_item(const char *obj, size_t obj_len, size_t *len);

/* Over a document that IS a bare array. /Items/Latest answers that way and
 * nothing else does. */
const char *json_bare_item(const char *json, int index, size_t *len);
int         json_bare_count(const char *json);

/* Bounded to `len` bytes. Jellyfin omits a field rather than sending it
 * null, so an unbounded read lands on whichever later item does have it:
 * an episode with "IndexNumber":null once displayed the number 22, belonging
 * to an item two places down. Everything read out of a listing goes through
 * these. */
int       json_str_in(const char *json, size_t len, const char *key, char *out, size_t outlen);
long long json_num_in(const char *json, size_t len, const char *key, long long def);
int       json_bool_in(const char *json, size_t len, const char *key, int def);

/* Where the key's value starts within `len` bytes, or 0. For presence. */
const char *json_key_in(const char *json, size_t len, const char *key);

#endif

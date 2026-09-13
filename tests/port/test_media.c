/* See port/media.h. Each check is a way the engine, the video memory or the
 * panel was actually misused in this tree. The desktop keeps the same state
 * machine, so the rules are checked on both machines and the console also
 * checks the real addresses. */

#include "port/media.h"

#include "tools/selftest.h"

#include <stdio.h>
#include <string.h>

static void reset(void) {
    media_film_give();
    if (media_still_begin() == 0) media_still_end();
}

/* sceJpeg and sceMpeg are the same silicon: a poster decoding through a
   film's claim drives the hardware from two threads at once. */
static int t_a_still_is_refused_while_a_film_has_it(char *note, unsigned n) {
    media_film held;

    reset();
    if (media_film_take(&held) != 0) {
        snprintf(note, n, "the film could not take the hardware at all");
        return 1;
    }
    if (media_still_begin() == 0) {
        media_still_end();
        media_film_give();
        snprintf(note, n, "a still was allowed while a film held the engine");
        return 1;
    }
    media_film_give();
    snprintf(note, n, "refused, and the poster is asked for again later");
    return 0;
}

/* A claim that is never released is a browser that never shows another
   poster. */
static int t_a_still_is_allowed_once_the_film_gives_it_back(char *note, unsigned n) {
    media_film held;

    reset();
    media_film_take(&held);
    media_film_give();

    if (media_still_begin() != 0) {
        snprintf(note, n, "still refused after the film gave the hardware back");
        return 1;
    }
    media_still_end();
    snprintf(note, n, "the engine comes back");
    return 0;
}

/* Laid out by hand over the video memory the browser draws into; an
   arithmetic slip puts the decoder's output on top of what the panel shows. */
static int t_the_films_buffers_do_not_overlap(char *note, unsigned n) {
    media_film    held;
    unsigned char c;
    int           i, j;

    reset();
    if (media_film_take(&held) != 0) {
        snprintf(note, n, "the film could not take the hardware");
        return 1;
    }
    if (held.count < 2 || !held.picture) {
        media_film_give();
        snprintf(note, n, "only %d panel buffers, picture %s", held.count, held.picture ? "set" : "null");
        return 1;
    }

    /* Two names for one address show up as a byte that changed under the
       other write. */
    for (i = 0; i < held.count; i++) *(unsigned char *)held.panel_u[i] = (unsigned char)(0xA0 + i);
    *(unsigned char *)held.picture = 0x5B;

    for (i = 0; i < held.count; i++) {
        c = *(unsigned char *)held.panel_u[i];
        if (c != (unsigned char)(0xA0 + i)) {
            media_film_give();
            snprintf(note, n, "panel %d reads 0x%02X, not 0x%02X -- it shares an address", i, c, 0xA0 + i);
            return 1;
        }
        for (j = i + 1; j < held.count; j++)
            if (held.panel_u[i] == held.panel_u[j]) {
                media_film_give();
                snprintf(note, n, "panel %d and %d are the same buffer", i, j);
                return 1;
            }
    }
    if (*(unsigned char *)held.picture != 0x5B) {
        media_film_give();
        snprintf(note, n, "the decoder's buffer shares an address with a panel");
        return 1;
    }

    media_film_give();
    snprintf(note, n, "%d panels and a picture buffer, all distinct", held.count);
    return 0;
}

/* decode_open takes the hardware for a check that runs it alone, and a film
   already playing must not have its surfaces cleared under it. */
static int t_taking_twice_does_not_clear_the_film(char *note, unsigned n) {
    media_film first, again;

    reset();
    if (media_film_take(&first) != 0) {
        snprintf(note, n, "the film could not take the hardware");
        return 1;
    }
    *(unsigned char *)first.panel_u[0] = 0x7E;

    if (media_film_take(&again) != 0) {
        media_film_give();
        snprintf(note, n, "the second take was refused");
        return 1;
    }
    if (again.panel_u[0] != first.panel_u[0]) {
        media_film_give();
        snprintf(note, n, "the second take handed out different buffers");
        return 1;
    }
    if (*(unsigned char *)again.panel_u[0] != 0x7E) {
        media_film_give();
        snprintf(note, n, "the second take cleared what the film had drawn");
        return 1;
    }
    media_film_give();
    snprintf(note, n, "the same claim, and the picture survives it");
    return 0;
}

/* Shutdown gives back on paths that never opened a film. */
static int t_giving_back_without_taking_is_safe(char *note, unsigned n) {
    reset();
    media_film_give();
    media_film_give();

    if (media_film_has_it()) {
        snprintf(note, n, "it thinks a film holds the hardware");
        return 1;
    }
    if (media_still_begin() != 0) {
        snprintf(note, n, "stills are refused after a give with no take");
        return 1;
    }
    media_still_end();
    snprintf(note, n, "twice over, and stills still work");
    return 0;
}

/* The film's bookkeeping keys off this: a claim that reports itself idle is
   one nothing will ever release. */
static int t_it_says_who_has_it(char *note, unsigned n) {
    media_film held;

    reset();
    if (media_film_has_it()) {
        snprintf(note, n, "a film held it before anything asked");
        return 1;
    }
    media_film_take(&held);
    if (!media_film_has_it()) {
        media_film_give();
        snprintf(note, n, "took the hardware and still reported idle");
        return 1;
    }
    media_film_give();
    if (media_film_has_it()) {
        snprintf(note, n, "gave the hardware back and still reported held");
        return 1;
    }
    snprintf(note, n, "idle, held, idle");
    return 0;
}

void test_media_register(void) {
    selftest_add("media", "a still is refused while a film has it", t_a_still_is_refused_while_a_film_has_it);
    selftest_add("media", "a still is allowed once the film gives it back", t_a_still_is_allowed_once_the_film_gives_it_back);
    selftest_add("media", "the film's buffers do not overlap", t_the_films_buffers_do_not_overlap);
    selftest_add("media", "taking twice does not clear the film", t_taking_twice_does_not_clear_the_film);
    selftest_add("media", "giving back without taking is safe", t_giving_back_without_taking_is_safe);
    selftest_add("media", "it says who has it", t_it_says_who_has_it);
}

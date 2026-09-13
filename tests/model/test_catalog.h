/* The real server, for checks that draw what it holds. -1 with `note` saying
 * why when there is none to ask: a check that goes red because a machine in
 * the next room is off teaches everyone to ignore it. */
#ifndef TESTS_TEST_CATALOG_H
#define TESTS_TEST_CATALOG_H

#include "model/catalog.h"

/* Signed in, with the front page answered. */
int real_library(char *note, unsigned n);

/* The library with the most rows among those holding one of `kind` (-1 for
 * any), loaded exactly as the listing page asks for it. `parent` is JF_ID_LEN,
 * `title` ITEM_NAME_LEN; `at` is the first row of that kind. */
int real_listing(int kind, char *parent, char *title, int *count, int *at, char *note, unsigned n);

#endif

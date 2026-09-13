/* test_http.c's loopback server, lent to other checks. Desktop only: the
 * console's stack has no loopback. */
#ifndef TESTS_IO_TEST_HTTP_H
#define TESTS_IO_TEST_HTTP_H

/* `dribble` bytes at a time when non-zero, so a boundary can be made to fall
   somewhere a well-behaved server would never put one. 1 when it started. */
int fake_http_start(const char *reply, unsigned len, unsigned dribble);

/* Answers `times` connections: a pipelined fetcher asks for the next thing
 * while reading this one. */
int fake_http_start_n(const char *reply, unsigned len, unsigned dribble, int times);

int fake_http_port(void);

/* Blocks until it has answered, or three seconds. */
void fake_http_wait(void);

#endif

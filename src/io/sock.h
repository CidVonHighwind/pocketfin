/* The raw TCP connection, supplied by each port. io/secure.c builds the
 * http_sock_* calls in io/http.h on top of these, adding TLS when the server
 * is reached over HTTPS. Nothing else calls them. */
#ifndef IO_SOCK_H
#define IO_SOCK_H

#include "io/http.h"

/* `host` is a dotted address or a name; a name is resolved here. */
http_sock port_sock_open(const char *host, int port, http_result *why, char *err, unsigned errlen);

/* As the http_sock_* calls they stand under: send may take part of the buffer. */
int  port_sock_send(http_sock s, const void *buf, unsigned len);
int  port_sock_recv(http_sock s, void *buf, unsigned len, int *timed_out);
void port_sock_close(http_sock s);
void port_sock_wake(http_sock s);

#endif

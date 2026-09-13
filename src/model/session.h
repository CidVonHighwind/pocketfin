/* The radio, the sign-in, and whether the server is reachable now. */
#ifndef MODEL_SESSION_H
#define MODEL_SESSION_H

#include "jelly/api.h"

typedef enum {
    LIB_STAGE_STARTING = 0,
    LIB_STAGE_LINKING,
    LIB_STAGE_SIGNIN,
    LIB_STAGE_READY,
    LIB_STAGE_STOPPED /* session_error() says why */
} lib_stage;

lib_stage   session_stage(void);
const char *session_stage_text(lib_stage s);
const char *session_error(void);
int         session_connected(void);

/* Reached once and not any more; raises the offline screen. */
int session_lost(void);

void session_retry(void); /* only asks; the worker does the work */
int  session_wants_connect(void);

/* On the catalogue's worker: DHCP alone is about three seconds. */
void session_connect(void);

void session_note_no_choice(void);
void session_note_stopped(const char *why);

/* What the start-up screen says when signing in at `address` failed, naming
   what the viewer should check. */
void session_sign_in_text(jf_err e, const char *address, char *out, unsigned n);

/* Once a frame. */
void session_watch(void);
int  session_link_returned(void);

/* The frame session_lost() became true. The offline screen pops itself, so
   pushing it must be an edge. */
int session_link_went(void);

void session_note_reached(void);
void session_note_failure(jf_err e);

/* "host:port", or "" before the connection file was read. */
const char *session_address(void);

void session_init(void);

#ifdef POCKETFIN_CHECKS
void session_fake_stage(lib_stage s, const char *fault);
void session_fake_lost(int lost);
#endif

#endif

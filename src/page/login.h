/* Signing in: the server's address, then Quick Connect (a code approved from a
 * signed-in Jellyfin app) or a user name and password typed on the console's
 * keyboard. What it keeps is the server and the session token
 * (jelly/api.h, account.txt) -- never the password. */
#ifndef PAGE_LOGIN_H
#define PAGE_LOGIN_H

#include "view/stack.h"
#include "jelly/api.h"

extern const screen_def login_page_screen;

#define LOGIN_ROW_SERVER   0
#define LOGIN_ROW_PORT     1
#define LOGIN_ROW_HTTPS    2
#define LOGIN_ROW_QUICK    3
#define LOGIN_ROW_PASSWORD 4
#define LOGIN_ROW_N        5

/* What the viewer typed for the server, into `c`: "https://name:8920/web"
   gives the scheme, the port and the bare name. */
void login_take_address(const char *typed, jf_conn *c);

/* For checks: the value a row shows, and the status line. */
const char *login_page_value(int row);
const char *login_page_status(void);

#endif

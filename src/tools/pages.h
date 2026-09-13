/* Land on a page by name, for the command channel. A developer's instrument:
 * every real route between pages belongs to the screens. */
#ifndef TOOLS_PAGES_H
#define TOOLS_PAGES_H

/* 0 if nothing is called that. */
int pages_go(const char *name);

const char *pages_names(void);

#endif

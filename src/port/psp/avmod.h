/* The one owner of the AV modules, which JPEG, video and sound all want:
 * taken by id, once, and given back together at shutdown in reverse.
 *
 * They come out of the user partition and outlive this module. A run that
 * played a film gives back 25,100,288 bytes where one that played none gives
 * back all 25,149,440, and those 49,152 sit in the middle -- 2.4 MB off the
 * largest free run, not 48 kB off the total.
 *
 * A module an earlier run left behind answers "already loaded" and is not
 * ours to free, so a run releases only what it loaded itself. Both sides are
 * logged: "nothing to release" and "released nothing" look identical. */
#ifndef PORT_PSP_AVMOD_H
#define PORT_PSP_AVMOD_H

/* Idempotent. */
void av_module_take(int id);

/* Only after the decoders that use them are closed. */
void av_modules_release(void);

#endif

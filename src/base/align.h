/* The engine wants a texture base on a 16-byte boundary. */
#ifndef BASE_ALIGN_H
#define BASE_ALIGN_H

#if defined(_MSC_VER)
#define POCKETFIN_ALIGN16 __declspec(align(16))
#else
#define POCKETFIN_ALIGN16 __attribute__((aligned(16)))
#endif

#endif

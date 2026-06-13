#ifndef STRNDUP_H
#define STRNDUP_H

#include <string.h>
#include <stdlib.h>

#if defined(_WIN32) && !defined(_UCRT)
// Legacy-MSVCRT implementation of strndup. The UCRT (msys2 ucrt64 / gcc16)
// already declares strndup in <string.h>, so defining our own there collides
// ('declared extern and later static' under -fpermissive). Guard it out on UCRT.
static inline char* strndup(const char* s, size_t n) {
    size_t len = strnlen(s, n);
    char* dup = (char*)malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len);
        dup[len] = '\0';
    }
    return dup;
}
#endif

#endif // STRNDUP_H 
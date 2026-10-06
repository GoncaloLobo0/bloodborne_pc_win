/* bbport (Windows): the C library's stdlib.h plus setenv/unsetenv (src/compat/win32/compat.c). */
#ifndef BB_COMPAT_STDLIB_H
#define BB_COMPAT_STDLIB_H
#include_next <stdlib.h>
#ifdef __cplusplus
extern "C" {
#endif
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
#ifdef __cplusplus
}
#endif
#endif

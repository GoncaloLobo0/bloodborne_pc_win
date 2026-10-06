/* bbport (Windows): the C library's string.h plus strcasestr (src/compat/win32/compat.c). */
#ifndef BB_COMPAT_STRING_H
#define BB_COMPAT_STRING_H
#include_next <string.h>
#ifdef __cplusplus
extern "C" {
#endif
char *strcasestr(const char *haystack, const char *needle);
#ifdef __cplusplus
}
#endif
#endif

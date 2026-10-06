/* bbport (Windows): MinGW's unistd.h plus the Linux calls the port uses. Implemented in
 * src/compat/win32/compat.c. */
#ifndef BB_COMPAT_UNISTD_H
#define BB_COMPAT_UNISTD_H
#include_next <unistd.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
/* OS thread id of the calling thread (GetCurrentThreadId). */
int gettid(void);
ssize_t pread(int fd, void *buffer, size_t size, off_t offset);
ssize_t pwrite(int fd, const void *buffer, size_t size, off_t offset);
#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE 30
#define _SC_NPROCESSORS_ONLN 84
#endif
long sysconf(int name);
#ifdef __cplusplus
}
#endif
#endif

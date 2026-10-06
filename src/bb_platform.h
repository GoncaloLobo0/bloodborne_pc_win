/* bbport: what the Linux-only parts of the runtime and the GPU library need on Windows (MinGW,
 * clang). Included by C and C++ code; on Linux it adds nothing. */
#ifndef BB_PLATFORM_H
#define BB_PLATFORM_H
#ifdef _WIN32
#include <stdint.h>
#include <stddef.h>
/* Recovery points that jump over guest frames. MinGW's longjmp unwinds through SEH frames,
 * which guest code has none of (no unwind tables): the builtin pair restores registers only. */
typedef void *sigjmp_buf[5];
#define sigsetjmp(buf, savemask) __builtin_setjmp(buf)
#define siglongjmp(buf, value) __builtin_longjmp(buf, 1)
typedef sigjmp_buf bb_jmp_buf;
#define bb_setjmp(buf) __builtin_setjmp(buf)
#define bb_longjmp(buf) __builtin_longjmp(buf, 1)
#ifdef __cplusplus
extern "C" {
#endif
/* Thread name (debuggers, crash reports); at most 15 characters are kept as on Linux. */
void bb_set_thread_name(const char *name);
int bb_get_thread_name(char *out, size_t size);
/* OS thread id of the calling thread. */
int bb_gettid(void);
/* Lower and upper end of the calling thread's stack. */
void bb_thread_stack(uintptr_t *low, uintptr_t *high);
/* Seconds east of UTC at `when` (struct tm has no tm_gmtoff here). */
long bb_utc_offset(long long when);
#ifdef __cplusplus
}
#endif
#ifndef PROT_NONE
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#endif
#else
#include <setjmp.h>
typedef sigjmp_buf bb_jmp_buf;
#define bb_setjmp(buf) sigsetjmp(buf, 0)
#define bb_longjmp(buf) siglongjmp(buf, 1)
#endif
#endif

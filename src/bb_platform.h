/* bbport: what the Linux-only parts of the runtime and the GPU library need on Windows (MinGW,
 * clang). Included by C and C++ code; on Linux it adds nothing. */
#ifndef BB_PLATFORM_H
#define BB_PLATFORM_H
#ifdef _WIN32
#include <stdint.h>
#include <stddef.h>
/* Recovery points that jump over guest frames. MinGW's longjmp unwinds through SEH frames, which
 * guest code has none of (no unwind tables); clang's __builtin_setjmp keeps a biased frame address
 * on Win64 and restores a wrong rbp. This pair (compat.c) saves and restores the callee-saved
 * registers of the Windows ABI, nothing else. */
typedef unsigned long long bb_jmp_buf[32];
typedef bb_jmp_buf sigjmp_buf;
#ifdef __cplusplus
extern "C" {
#endif
__attribute__((returns_twice)) int bb_setjmp(bb_jmp_buf buf);
__attribute__((noreturn)) void bb_longjmp(bb_jmp_buf buf);
#ifdef __cplusplus
}
#endif
#define sigsetjmp(buf, savemask) bb_setjmp(buf)
#define siglongjmp(buf, value) bb_longjmp(buf)
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

/* bbport (Windows): glibc's backtrace functions for the diagnostics (frames by
 * RtlCaptureStackBackTrace, printed as module+offset). Implemented in src/compat/win32/compat.c. */
#ifndef BB_COMPAT_EXECINFO_H
#define BB_COMPAT_EXECINFO_H
#ifdef __cplusplus
extern "C" {
#endif
int backtrace(void **frames, int size);
void backtrace_symbols_fd(void *const *frames, int size, int fd);
#ifdef __cplusplus
}
#endif
#endif

/* bbport (Windows): process_vm_readv on the own process, the diagnostics' read that cannot fault
 * (ReadProcessMemory). Implemented in src/compat/win32/compat.c. */
#ifndef BB_COMPAT_SYS_UIO_H
#define BB_COMPAT_SYS_UIO_H
#include <stddef.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
struct iovec { void *iov_base; size_t iov_len; };
ssize_t process_vm_readv(int pid, const struct iovec *local, unsigned long local_count,
                         const struct iovec *remote, unsigned long remote_count, unsigned long flags);
#ifdef __cplusplus
}
#endif
#endif

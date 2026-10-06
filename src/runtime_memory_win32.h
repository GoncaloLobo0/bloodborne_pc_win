/* bbport (Windows): host side of guest memory (runtime_memory_win32.c). */
#ifndef RUNTIME_MEMORY_WIN32_H
#define RUNTIME_MEMORY_WIN32_H
#include <stdint.h>
/* Reserves [start, end) as placeholders and creates the section of `size` bytes, viewed by the
 * host at backing_address. */
int vm_init(uintptr_t start, uintptr_t end, uint64_t size, uintptr_t backing_address);
unsigned char *vm_backing(void);
/* Commits section pages (allocation of direct or flexible memory). */
int vm_commit(uint64_t phys, uint64_t size);
/* Maps section bytes [phys, phys+size) at address, replacing what was there. */
int vm_map(uintptr_t address, uint64_t size, int prot, uint64_t phys);
/* Unmaps [address, address+size): inaccessible placeholders again. */
int vm_release(uintptr_t address, uint64_t size);
/* Private committed memory at address (host-owned memory the guest sees). */
void *vm_alloc_private(uintptr_t address, uint64_t size, int prot);
/* Protection (PROT_* bits) of mapped memory in [address, address+size). */
int vm_protect(uintptr_t address, uint64_t size, int prot);
/* Fault handler: 1 when the access can be retried (the range was being mapped again). */
int runtime_memory_fault_retry(uintptr_t address, int write);
#endif

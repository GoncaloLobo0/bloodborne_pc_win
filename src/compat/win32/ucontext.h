/* bbport (Windows): the Linux x86-64 ucontext layout. The loader's exception handler (probe.c)
 * fills one from the Windows CONTEXT and passes it to the GPU library's fault handlers, so their
 * uc_mcontext.gregs[REG_*] reads work unchanged; changed registers are copied back. */
#ifndef BB_COMPAT_UCONTEXT_H
#define BB_COMPAT_UCONTEXT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* glibc's order (sys/ucontext.h). */
enum {
    REG_R8 = 0, REG_R9, REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15,
    REG_RDI, REG_RSI, REG_RBP, REG_RBX, REG_RDX, REG_RAX, REG_RCX, REG_RSP,
    REG_RIP, REG_EFL, REG_CSGSFS, REG_ERR, REG_TRAPNO, REG_OLDMASK, REG_CR2
};
#define NGREG 23
typedef long long greg_t;
typedef greg_t gregset_t[NGREG];
typedef struct { gregset_t gregs; void *fpregs; unsigned long long reserved[8]; } mcontext_t;
typedef struct ucontext_t {
    unsigned long uc_flags;
    struct ucontext_t *uc_link;
    mcontext_t uc_mcontext;
    void *native; /* EXCEPTION_POINTERS of the fault */
} ucontext_t;
#ifdef __cplusplus
}
#endif
#endif

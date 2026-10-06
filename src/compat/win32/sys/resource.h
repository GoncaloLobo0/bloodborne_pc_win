/* bbport (Windows): getrusage for the frame statistics (CPU times; the remaining counters are
 * zero), and setpriority as a no-op. Implemented in src/compat/win32/compat.c. */
#ifndef BB_COMPAT_SYS_RESOURCE_H
#define BB_COMPAT_SYS_RESOURCE_H
#include <sys/time.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RUSAGE_SELF 0
#define RUSAGE_THREAD 1
#define PRIO_PROCESS 0
typedef unsigned int id_t;
struct rusage {
    struct timeval ru_utime, ru_stime;
    long ru_maxrss, ru_ixrss, ru_idrss, ru_isrss, ru_minflt, ru_majflt, ru_nswap, ru_inblock,
        ru_oublock, ru_msgsnd, ru_msgrcv, ru_nsignals, ru_nvcsw, ru_nivcsw;
};
int getrusage(int who, struct rusage *usage);
int setpriority(int which, id_t who, int prio);
#ifdef __cplusplus
}
#endif
#endif

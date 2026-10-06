/* bbport (Windows): nftw for the save data runtime (deleting save directories): FTW_DEPTH order,
 * no symbolic links. Implemented in src/compat/win32/compat.c. */
#ifndef BB_COMPAT_FTW_H
#define BB_COMPAT_FTW_H
#include <sys/stat.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { FTW_F, FTW_D, FTW_DNR, FTW_DP, FTW_NS, FTW_SL, FTW_SLN };
#define FTW_PHYS 1
#define FTW_MOUNT 2
#define FTW_CHDIR 4
#define FTW_DEPTH 8
struct FTW { int base, level; };
int nftw(const char *path, int (*callback)(const char *, const struct stat *, int, struct FTW *), int descriptors, int flags);
#ifdef __cplusplus
}
#endif
#endif

/* bbport (Windows): the C library's sys/stat.h; mkdir takes the POSIX mode argument (ignored:
 * Windows directories have no mode bits). */
#ifndef BB_COMPAT_SYS_STAT_H
#define BB_COMPAT_SYS_STAT_H
#include_next <sys/stat.h>
#include <io.h>
#include <direct.h>
#define mkdir(path, mode) ((void)(mode), _mkdir(path))
#endif

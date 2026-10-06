/* bbport (Windows): the POSIX/Linux calls of the headers in src/compat/win32 and of
 * bb_platform.h. */
#include <windows.h>
#include <psapi.h>
#include <io.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <execinfo.h>
#include <dirent.h>
#include <ftw.h>
#include <sys/stat.h>
#include "bb_platform.h"

typedef int (*FtwCallback)(const char *, const struct stat *, int, struct FTW *);
static int walk(const char *path, FtwCallback callback, int flags, int level) {
    struct stat st;
    struct FTW where = {(int)(strrchr(path, '/') ? strrchr(path, '/') - path + 1 : 0), level};
    if (stat(path, &st)) return callback(path, &st, FTW_NS, &where);
    if (!S_ISDIR(st.st_mode)) return callback(path, &st, FTW_F, &where);
    if (!(flags & FTW_DEPTH)) { int r = callback(path, &st, FTW_D, &where); if (r) return r; }
    DIR *dir = opendir(path);
    if (!dir) return callback(path, &st, FTW_DNR, &where);
    for (struct dirent *e; (e = readdir(dir));) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char child[4096];
        if ((size_t)snprintf(child, sizeof(child), "%s/%s", path, e->d_name) >= sizeof(child)) continue;
        int r = walk(child, callback, flags, level + 1);
        if (r) { closedir(dir); return r; }
    }
    closedir(dir);
    return (flags & FTW_DEPTH) ? callback(path, &st, FTW_DP, &where) : 0;
}
int nftw(const char *path, FtwCallback callback, int descriptors, int flags) {
    (void)descriptors;
    return walk(path, callback, flags, 0);
}

char *strcasestr(const char *haystack, const char *needle) {
    const size_t n = strlen(needle);
    for (; *haystack; ++haystack)
        if (!_strnicmp(haystack, needle, n)) return (char *)haystack;
    return n ? NULL : (char *)haystack;
}
long bb_utc_offset(long long when_seconds) {
    const time_t when = (time_t)when_seconds;
    struct tm local, utc;
    if (localtime_s(&local, &when) || gmtime_s(&utc, &when)) return 0;
    /* Seconds east of UTC: the local broken-down time read as if it were UTC, minus the time. */
    local.tm_isdst = 0; utc.tm_isdst = 0;
    return (long)(_mkgmtime(&local) - _mkgmtime(&utc));
}

/* bb_setjmp/bb_longjmp (bb_platform.h): rbx rbp rdi rsi r12-r15, rsp after the return, the return
 * address, xmm6-15, MXCSR and the x87 control word; no unwinding. */
__asm__(".text\n"
        ".globl bb_setjmp\n"
        "bb_setjmp:\n"
        "    mov %rbx, 0(%rcx)\n"
        "    mov %rbp, 8(%rcx)\n"
        "    mov %rdi, 16(%rcx)\n"
        "    mov %rsi, 24(%rcx)\n"
        "    mov %r12, 32(%rcx)\n"
        "    mov %r13, 40(%rcx)\n"
        "    mov %r14, 48(%rcx)\n"
        "    mov %r15, 56(%rcx)\n"
        "    lea 8(%rsp), %rdx\n"
        "    mov %rdx, 64(%rcx)\n"
        "    mov (%rsp), %rdx\n"
        "    mov %rdx, 72(%rcx)\n"
        "    movdqu %xmm6, 80(%rcx)\n"
        "    movdqu %xmm7, 96(%rcx)\n"
        "    movdqu %xmm8, 112(%rcx)\n"
        "    movdqu %xmm9, 128(%rcx)\n"
        "    movdqu %xmm10, 144(%rcx)\n"
        "    movdqu %xmm11, 160(%rcx)\n"
        "    movdqu %xmm12, 176(%rcx)\n"
        "    movdqu %xmm13, 192(%rcx)\n"
        "    movdqu %xmm14, 208(%rcx)\n"
        "    movdqu %xmm15, 224(%rcx)\n"
        "    stmxcsr 240(%rcx)\n"
        "    fnstcw 244(%rcx)\n"
        "    xor %eax, %eax\n"
        "    ret\n"
        ".globl bb_longjmp\n"
        "bb_longjmp:\n"
        "    mov 0(%rcx), %rbx\n"
        "    mov 8(%rcx), %rbp\n"
        "    mov 16(%rcx), %rdi\n"
        "    mov 24(%rcx), %rsi\n"
        "    mov 32(%rcx), %r12\n"
        "    mov 40(%rcx), %r13\n"
        "    mov 48(%rcx), %r14\n"
        "    mov 56(%rcx), %r15\n"
        "    movdqu 80(%rcx), %xmm6\n"
        "    movdqu 96(%rcx), %xmm7\n"
        "    movdqu 112(%rcx), %xmm8\n"
        "    movdqu 128(%rcx), %xmm9\n"
        "    movdqu 144(%rcx), %xmm10\n"
        "    movdqu 160(%rcx), %xmm11\n"
        "    movdqu 176(%rcx), %xmm12\n"
        "    movdqu 192(%rcx), %xmm13\n"
        "    movdqu 208(%rcx), %xmm14\n"
        "    movdqu 224(%rcx), %xmm15\n"
        "    ldmxcsr 240(%rcx)\n"
        "    fldcw 244(%rcx)\n"
        "    mov 64(%rcx), %rsp\n"
        "    mov $1, %eax\n"
        "    jmp *72(%rcx)\n");

int gettid(void) { return (int)GetCurrentThreadId(); }
/* _putenv_s updates the C runtime's copy and the process environment (child processes). */
int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !*name || strchr(name, '=')) { errno = EINVAL; return -1; }
    if (!overwrite && getenv(name)) return 0;
    return _putenv_s(name, value ? value : "") ? -1 : 0;
}
int unsetenv(const char *name) { return _putenv_s(name, "") ? -1 : 0; }
int bb_gettid(void) { return (int)GetCurrentThreadId(); }

static void filetime_to_timeval(const FILETIME *t, struct timeval *out) {
    const unsigned long long units = ((unsigned long long)t->dwHighDateTime << 32) | t->dwLowDateTime; /* 100 ns */
    out->tv_sec = (long)(units / 10000000ull);
    out->tv_usec = (long)((units % 10000000ull) / 10);
}
int getrusage(int who, struct rusage *usage) {
    FILETIME created, exited, kernel, user;
    BOOL ok = who == RUSAGE_THREAD ? GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)
                                   : GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    if (!usage || !ok) { errno = EINVAL; return -1; }
    memset(usage, 0, sizeof(*usage));
    filetime_to_timeval(&user, &usage->ru_utime);
    filetime_to_timeval(&kernel, &usage->ru_stime);
    if (who == RUSAGE_SELF) {
        PROCESS_MEMORY_COUNTERS counters;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
            usage->ru_maxrss = (long)(counters.PeakWorkingSetSize / 1024);
            usage->ru_minflt = (long)counters.PageFaultCount;
        }
    }
    return 0;
}
int setpriority(int which, id_t who, int prio) { (void)which; (void)who; (void)prio; return 0; }

int backtrace(void **frames, int size) {
    if (size <= 0) return 0;
    return (int)RtlCaptureStackBackTrace(1, (DWORD)size, frames, NULL);
}
void backtrace_symbols_fd(void *const *frames, int size, int fd) {
    for (int i = 0; i < size; ++i) {
        char line[512];
        Dl_info info;
        memset(&info, 0, sizeof(info));
        if (dladdr(frames[i], &info) && info.dli_fname) {
            const char *name = strrchr(info.dli_fname, '\\');
            name = name ? name + 1 : info.dli_fname;
            if (info.dli_sname)
                snprintf(line, sizeof(line), "%s(%s+%#llx) [%p]\n", name, info.dli_sname,
                         (unsigned long long)((char *)frames[i] - (char *)info.dli_saddr), frames[i]);
            else
                snprintf(line, sizeof(line), "%s(+%#llx) [%p]\n", name,
                         (unsigned long long)((char *)frames[i] - (char *)info.dli_fbase), frames[i]);
        } else {
            snprintf(line, sizeof(line), "[%p]\n", frames[i]);
        }
        if (_write(fd, line, (unsigned)strlen(line)) < 0) return;
    }
}

ssize_t process_vm_readv(int pid, const struct iovec *local, unsigned long local_count,
                         const struct iovec *remote, unsigned long remote_count, unsigned long flags) {
    (void)pid; (void)flags;
    /* The diagnostics pass one local and one remote vector of the same size. */
    if (local_count != 1 || remote_count != 1) { errno = EINVAL; return -1; }
    SIZE_T done = 0;
    const size_t size = local->iov_len < remote->iov_len ? local->iov_len : remote->iov_len;
    if (!ReadProcessMemory(GetCurrentProcess(), remote->iov_base, local->iov_base, size, &done) && !done) {
        errno = EFAULT;
        return -1;
    }
    return (ssize_t)done;
}

/* Positional I/O on a C runtime descriptor; the file position is kept as POSIX requires. */
static ssize_t positional(int fd, void *buffer, size_t size, off_t offset, int write) {
    HANDLE handle = (HANDLE)_get_osfhandle(fd);
    if (handle == INVALID_HANDLE_VALUE) { errno = EBADF; return -1; }
    LARGE_INTEGER zero = {0}, position;
    if (!SetFilePointerEx(handle, zero, &position, FILE_CURRENT)) { errno = EIO; return -1; }
    OVERLAPPED at;
    memset(&at, 0, sizeof(at));
    at.Offset = (DWORD)((unsigned long long)offset & 0xffffffffu);
    at.OffsetHigh = (DWORD)((unsigned long long)offset >> 32);
    DWORD done = 0;
    const DWORD n = size > 0x7fffffffu ? 0x7fffffffu : (DWORD)size;
    BOOL ok = write ? WriteFile(handle, buffer, n, &done, &at) : ReadFile(handle, buffer, n, &done, &at);
    SetFilePointerEx(handle, position, NULL, FILE_BEGIN);
    if (!ok && GetLastError() != ERROR_HANDLE_EOF) { errno = EIO; return -1; }
    return (ssize_t)done;
}
ssize_t pread(int fd, void *buffer, size_t size, off_t offset) { return positional(fd, buffer, size, offset, 0); }
ssize_t pwrite(int fd, const void *buffer, size_t size, off_t offset) {
    return positional(fd, (void *)buffer, size, offset, 1);
}

long sysconf(int name) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    if (name == _SC_PAGESIZE) return (long)info.dwPageSize;
    if (name == _SC_NPROCESSORS_ONLN) return (long)info.dwNumberOfProcessors;
    errno = EINVAL;
    return -1;
}

/* Thread names: SetThreadDescription (Windows 10 1607+), looked up at run time. */
typedef HRESULT (WINAPI *SetDescription)(HANDLE, PCWSTR);
typedef HRESULT (WINAPI *GetDescription)(HANDLE, PWSTR *);
void bb_set_thread_name(const char *name) {
    static SetDescription set;
    static int looked_up;
    if (!looked_up) {
        set = (SetDescription)(void (*)(void))GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "SetThreadDescription");
        looked_up = 1;
    }
    if (!set || !name) return;
    wchar_t wide[64];
    if (MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 64) > 0) set(GetCurrentThread(), wide);
}
int bb_get_thread_name(char *out, size_t size) {
    static GetDescription get;
    static int looked_up;
    if (!looked_up) {
        get = (GetDescription)(void (*)(void))GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "GetThreadDescription");
        looked_up = 1;
    }
    if (!size) return -1;
    out[0] = 0;
    PWSTR wide = NULL;
    if (!get || FAILED(get(GetCurrentThread(), &wide)) || !wide) return -1;
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)size, NULL, NULL);
    out[size - 1] = 0;
    LocalFree(wide);
    return 0;
}
void bb_thread_stack(uintptr_t *low, uintptr_t *high) {
    ULONG_PTR lo = 0, hi = 0;
    GetCurrentThreadStackLimits(&lo, &hi);
    *low = (uintptr_t)lo;
    *high = (uintptr_t)hi;
}

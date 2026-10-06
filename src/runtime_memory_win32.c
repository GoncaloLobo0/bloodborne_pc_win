/* bbport (Windows): the host side of guest memory (runtime_memory.c) with placeholder APIs.
 *
 * Linux backs direct and flexible memory with one memfd and maps any range of it at any guest
 * address with mmap(MAP_FIXED). Here one pagefile-backed section (SEC_RESERVE: pages are
 * committed when the game allocates them) plays the memfd, and the PS4 user range below 1 TiB is
 * reserved at startup as placeholders: a guest mapping is a view of the section placed into a
 * placeholder carved to its exact size (MapViewOfFile3 + MEM_REPLACE_PLACEHOLDER). Views cannot
 * be cut, so a partial unmap or a mapping over part of a view maps the remaining pieces again
 * with their page protections; a thread touching them meanwhile waits in the fault handler
 * (runtime_memory_fault_retry). Callers serialize changes (runtime_memory.c's write lock). */
#ifdef _WIN32
#include "runtime_memory_win32.h"
#include <windows.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef PVOID (WINAPI *VirtualAlloc2Fn)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
typedef PVOID (WINAPI *MapViewOfFile3Fn)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER *, ULONG);
typedef BOOL (WINAPI *UnmapViewOfFile2Fn)(HANDLE, PVOID, ULONG);
static VirtualAlloc2Fn virtual_alloc2;
static MapViewOfFile3Fn map_view3;
static UnmapViewOfFile2Fn unmap_view2;

enum { R_PLACEHOLDER, R_VIEW, R_PRIVATE };
typedef struct { uintptr_t start, end; int kind; uint64_t phys; } Region;
static Region *regions;
static size_t region_count, region_capacity;
static HANDLE section;
static uint64_t section_size;
static unsigned char *backing;
static uintptr_t range_start, range_end;
/* A view being mapped again in pieces: its range, while the flag is set. */
static _Atomic int transition_active;
static _Atomic uintptr_t transition_start, transition_end;

static DWORD win_prot(int prot) {
    const int r = prot & 1, w = prot & 2, x = prot & 4;
    if (x) return w ? PAGE_EXECUTE_READWRITE : r ? PAGE_EXECUTE_READ : PAGE_EXECUTE;
    if (w) return PAGE_READWRITE;
    return r ? PAGE_READONLY : PAGE_NOACCESS;
}
static void report(const char *what, uintptr_t address, uint64_t size) {
    static _Atomic int reported;
    if (atomic_fetch_add(&reported, 1) < 16)
        fprintf(stderr, "Runtime: %s %#llx+%#llx failed (Windows error %lu)\n", what,
                (unsigned long long)address, (unsigned long long)size, GetLastError());
}

/* First region with end > address. */
static size_t find(uintptr_t address) {
    size_t lo = 0, hi = region_count;
    while (lo < hi) { size_t mid = (lo + hi) / 2; if (regions[mid].end <= address) lo = mid + 1; else hi = mid; }
    return lo;
}
static int insert(size_t at, Region r) {
    if (region_count == region_capacity) {
        size_t capacity = region_capacity ? region_capacity * 2 : 1024;
        Region *next = realloc(regions, capacity * sizeof(*regions));
        if (!next) return -1;
        regions = next; region_capacity = capacity;
    }
    memmove(regions + at + 1, regions + at, (region_count - at) * sizeof(*regions));
    regions[at] = r; ++region_count;
    return 0;
}
static void erase(size_t at, size_t n) {
    memmove(regions + at, regions + at + n, (region_count - at - n) * sizeof(*regions));
    region_count -= n;
}

int vm_init(uintptr_t start, uintptr_t end, uint64_t size, uintptr_t backing_address) {
    if (section) return 0;
    HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
    virtual_alloc2 = (VirtualAlloc2Fn)(void (*)(void))GetProcAddress(kernelbase, "VirtualAlloc2");
    map_view3 = (MapViewOfFile3Fn)(void (*)(void))GetProcAddress(kernelbase, "MapViewOfFile3");
    unmap_view2 = (UnmapViewOfFile2Fn)(void (*)(void))GetProcAddress(kernelbase, "UnmapViewOfFile2");
    if (!virtual_alloc2 || !map_view3 || !unmap_view2) {
        fputs("STOP: Windows 10 version 1803 or newer is required (placeholder memory APIs)\n", stderr);
        exit(21);
    }
    HANDLE process = GetCurrentProcess();
    if (!virtual_alloc2(process, (void *)start, end - start, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, NULL, 0)) {
        fprintf(stderr, "STOP: cannot reserve the guest address range %#llx-%#llx (Windows error %lu)\n",
                (unsigned long long)start, (unsigned long long)end, GetLastError());
        exit(21);
    }
    range_start = start; range_end = end;
    if (insert(0, (Region){start, end, R_PLACEHOLDER, 0})) return -1;
    section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_EXECUTE_READWRITE | SEC_RESERVE,
                                 (DWORD)(size >> 32), (DWORD)size, NULL);
    if (!section) { report("creating the guest memory section", 0, size); return -1; }
    section_size = size;
    /* The host's own view (writes that bypass guest and GPU page protection), above the PS4 range
     * and away from the low addresses the host heap grows into. */
    if (!virtual_alloc2(process, (void *)backing_address, size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, NULL, 0) ||
        !(backing = map_view3(section, process, (void *)backing_address, 0, size, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, NULL, 0))) {
        report("mapping the host view of guest memory", backing_address, size);
        return -1;
    }
    return 0;
}
unsigned char *vm_backing(void) { return backing; }
int vm_commit(uint64_t phys, uint64_t size) {
    if (!size) return 0;
    if (phys + size > section_size) return -1;
    if (!VirtualAlloc(backing + phys, size, MEM_COMMIT, PAGE_READWRITE)) { report("committing guest memory", phys, size); return -1; }
    return 0;
}

/* Protection runs of [a, b) as VirtualQuery reports them, for mapping a view again. */
typedef struct { uintptr_t start, end; DWORD protect; } Run;
static Run *runs;
static size_t run_capacity;
static size_t query_runs(uintptr_t a, uintptr_t b) {
    size_t n = 0;
    for (uintptr_t at = a; at < b;) {
        MEMORY_BASIC_INFORMATION info;
        if (!VirtualQuery((void *)at, &info, sizeof(info))) break;
        uintptr_t end = (uintptr_t)info.BaseAddress + info.RegionSize;
        if (end > b) end = b;
        if (n == run_capacity) {
            size_t capacity = run_capacity ? run_capacity * 2 : 4096;
            Run *next = realloc(runs, capacity * sizeof(*runs));
            if (!next) { fputs("STOP: out of memory mapping a view again\n", stderr); exit(21); }
            runs = next; run_capacity = capacity;
        }
        runs[n++] = (Run){at, end, info.Protect};
        at = end;
    }
    return n;
}
static int map_view(uintptr_t a, uintptr_t b, uint64_t phys) {
    return map_view3(section, GetCurrentProcess(), (void *)a, phys, b - a, MEM_REPLACE_PLACEHOLDER,
                     PAGE_EXECUTE_READWRITE, NULL, 0) ? 0 : -1;
}
/* A view gets a boundary at `at`: unmapped, its placeholder split, both pieces mapped again with
 * the protection they had page by page. */
static int split_view(size_t i, uintptr_t at) {
    const Region r = regions[i];
    const size_t n = query_runs(r.start, r.end);
    atomic_store(&transition_start, r.start);
    atomic_store(&transition_end, r.end);
    atomic_store(&transition_active, 1);
    int error = 0;
    if (!unmap_view2(GetCurrentProcess(), (void *)r.start, MEM_PRESERVE_PLACEHOLDER)) { report("unmapping a view", r.start, r.end - r.start); error = -1; }
    if (!error && !VirtualFree((void *)r.start, at - r.start, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) { report("splitting a placeholder", r.start, at - r.start); error = -1; }
    if (!error && (map_view(r.start, at, r.phys) || map_view(at, r.end, r.phys + (at - r.start)))) { report("mapping a view again", r.start, r.end - r.start); error = -1; }
    if (!error) {
        DWORD old;
        for (size_t k = 0; k < n; ++k)
            if (runs[k].protect != PAGE_EXECUTE_READWRITE)
                VirtualProtect((void *)runs[k].start, runs[k].end - runs[k].start, runs[k].protect, &old);
    }
    atomic_store(&transition_active, 0);
    if (error) return -1;
    regions[i].end = at;
    return insert(i + 1, (Region){at, r.end, R_VIEW, r.phys + (at - r.start)});
}
/* A region boundary at `at`. */
static int split_at(uintptr_t at) {
    size_t i = find(at);
    if (i == region_count || regions[i].start >= at) return 0;
    Region *r = &regions[i];
    if (r->kind == R_VIEW) return split_view(i, at);
    if (r->kind == R_PRIVATE) { fprintf(stderr, "Runtime: host allocation %#llx split\n", (unsigned long long)at); return -1; }
    if (!VirtualFree((void *)r->start, at - r->start, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) {
        report("splitting a placeholder", r->start, at - r->start);
        return -1;
    }
    const uintptr_t end = r->end;
    r->end = at;
    return insert(i + 1, (Region){at, end, R_PLACEHOLDER, 0});
}
/* [a, b) becomes one placeholder; returns its index or -1. */
static long release(uintptr_t a, uintptr_t b) {
    if (a < range_start || b > range_end || a >= b) return -1;
    if (split_at(a) || split_at(b)) return -1;
    size_t first = find(a), last = first;
    for (size_t i = first; i < region_count && regions[i].start < b; ++i) {
        Region *r = &regions[i];
        if (r->kind == R_VIEW && !unmap_view2(GetCurrentProcess(), (void *)r->start, MEM_PRESERVE_PLACEHOLDER)) {
            report("unmapping a view", r->start, r->end - r->start);
            return -1;
        }
        if (r->kind == R_PRIVATE && !VirtualFree((void *)r->start, r->end - r->start, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) {
            report("releasing a host allocation", r->start, r->end - r->start);
            return -1;
        }
        r->kind = R_PLACEHOLDER; r->phys = 0;
        last = i;
    }
    if (last > first) {
        if (!VirtualFree((void *)a, b - a, MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS)) {
            report("joining placeholders", a, b - a);
            return -1;
        }
        regions[first].end = b;
        erase(first + 1, last - first);
    }
    return (long)first;
}

int vm_map(uintptr_t address, uint64_t size, int prot, uint64_t phys) {
    if (phys + size > section_size) return -1;
    long i = release(address, address + size);
    if (i < 0) return -1;
    if (map_view(address, address + size, phys)) { report("mapping guest memory", address, size); return -1; }
    regions[i].kind = R_VIEW; regions[i].phys = phys;
    DWORD old, want = win_prot(prot);
    if (want != PAGE_EXECUTE_READWRITE && !VirtualProtect((void *)address, size, want, &old)) {
        report("protecting guest memory", address, size);
        return -1;
    }
    return 0;
}
int vm_release(uintptr_t address, uint64_t size) { return release(address, address + size) < 0 ? -1 : 0; }
void *vm_alloc_private(uintptr_t address, uint64_t size, int prot) {
    long i = release(address, address + size);
    if (i < 0) return NULL;
    void *p = virtual_alloc2(GetCurrentProcess(), (void *)address, size, MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER,
                             win_prot(prot), NULL, 0);
    if (!p) { report("allocating host memory", address, size); return NULL; }
    regions[i].kind = R_PRIVATE;
    return p;
}
int vm_protect(uintptr_t address, uint64_t size, int prot) {
    const uintptr_t end = address + size;
    const DWORD want = win_prot(prot);
    int result = 0;
    for (size_t i = find(address); i < region_count && regions[i].start < end; ++i) {
        const Region *r = &regions[i];
        if (r->kind == R_PLACEHOLDER) { result = -1; continue; }
        const uintptr_t a = r->start > address ? r->start : address, b = r->end < end ? r->end : end;
        DWORD old;
        if (!VirtualProtect((void *)a, b - a, want, &old)) { report("protecting guest memory", a, b - a); result = -1; }
    }
    return result;
}

int runtime_memory_fault_retry(uintptr_t address, int write) {
    if (atomic_load(&transition_active)) {
        if (address >= atomic_load(&transition_start) && address < atomic_load(&transition_end)) {
            while (atomic_load(&transition_active)) SwitchToThread();
            return 1;
        }
    }
    /* The page changed under the fault (mapped again, unprotected by another thread): retry. */
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery((void *)address, &info, sizeof(info)) || info.State != MEM_COMMIT) return 0;
    const DWORD p = info.Protect & 0xff;
    const int readable = p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE ||
                         p == PAGE_WRITECOPY || p == PAGE_EXECUTE_WRITECOPY;
    const int writable = p == PAGE_READWRITE || p == PAGE_EXECUTE_READWRITE || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_WRITECOPY;
    if (info.Protect & PAGE_GUARD) return 0;
    return write ? writable : readable;
}
#endif

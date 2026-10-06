/* Guest memory under random fixed mappings, partial unmaps and protection changes, checked page by
 * page against a model: what the guest API reports, the data seen through every alias of the same
 * direct memory, and (Windows) the host protection, including GPU write protection kept across
 * the remapping of split views. */
#define _GNU_SOURCE
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

void runtime_restart(void) { abort(); }
void runtime_memory_gpu_protect(uintptr_t address, uint64_t size, int read, int write);

typedef int32_t (ABI *Allocate)(int64_t, int64_t, uint64_t, uint64_t, int, int64_t *);
typedef int32_t (ABI *Map)(void **, uint64_t, int, int, int64_t, uint64_t);
typedef int32_t (ABI *Unmap)(void *, uint64_t);
typedef int32_t (ABI *Protect)(const void *, uint64_t, int);
typedef int32_t (ABI *Query)(void *, void **, void **, uint32_t *);
#define GET(type, name) ((type)runtime_resolve(name, 0))

enum { PAGE = 16384, PAGES = 64, MAP_AT = 0x10 /* MAP_FIXED */ };
#define BASE UINT64_C(0x2000000000)
typedef struct { int mapped, prot, phys; } Page;
static Page pages[PAGES];       /* guest page -> direct memory page */
static uint64_t content[PAGES]; /* first qword of each direct memory page */
static int gpu_readonly[PAGES]; /* GPU write tracking on the page */
static uint64_t seed = 0x9E3779B97F4A7C15ull;
static unsigned next(unsigned n) { seed = seed * 6364136223846793005ull + 1442695040888963407ull; return (unsigned)(seed >> 33) % n; }
static void *at(int page) { return (void *)(uintptr_t)(BASE + (uint64_t)page * PAGE); }

static void check(Query query) {
    for (int i = 0; i < PAGES; ++i) {
        void *start = NULL, *end = NULL;
        uint32_t prot = 0;
        const int32_t r = query(at(i), &start, &end, &prot);
        if (!pages[i].mapped) {
            assert((uint32_t)r == 0x8002000d);
            continue;
        }
        assert(r == 0 && (int)prot == pages[i].prot);
        assert((uintptr_t)start <= (uintptr_t)at(i) && (uintptr_t)at(i) < (uintptr_t)end);
        if (pages[i].prot & 1) assert(*(volatile uint64_t *)at(i) == content[pages[i].phys]);
#ifdef _WIN32
        MEMORY_BASIC_INFORMATION info;
        assert(VirtualQuery(at(i), &info, sizeof(info)) == sizeof(info) && info.State == MEM_COMMIT);
        const int writable = (pages[i].prot & 2) && !gpu_readonly[i];
        const DWORD want = writable ? PAGE_READWRITE : (pages[i].prot & 1) ? PAGE_READONLY : PAGE_NOACCESS;
        if (info.Protect != want) {
            fprintf(stderr, "page %d: protection %#lx, expected %#lx\n", i, info.Protect, want);
            abort();
        }
#endif
    }
}

#ifdef _WIN32
#include "runtime_memory_win32.h"
/* A view cut in two is unmapped and mapped again in pieces: a thread reading its untouched part
 * meanwhile faults, and the loader's exception handler retries (runtime_memory_fault_retry). */
static volatile LONG reader_stop, retried;
static LONG CALLBACK retry_handler(EXCEPTION_POINTERS *ep) {
    const EXCEPTION_RECORD *r = ep->ExceptionRecord;
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2 &&
        runtime_memory_fault_retry((uintptr_t)r->ExceptionInformation[1], r->ExceptionInformation[0] == 1)) {
        InterlockedIncrement(&retried);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static DWORD WINAPI reader(void *expected) {
    unsigned long long reads = 0;
    while (!reader_stop) {
        if (*(volatile uint64_t *)at(3) != *(const uint64_t *)expected) { fputs("reader: wrong data\n", stderr); abort(); }
        ++reads;
    }
    return (DWORD)(reads != 0);
}
static void concurrent_splits(Map map, Unmap unmap, int64_t phys) {
    AddVectoredExceptionHandler(1, retry_handler);
    void *address = at(0);
    assert(unmap(at(0), (uint64_t)PAGES * PAGE) == 0);
    assert(map(&address, 8 * PAGE, 3, MAP_AT, phys, 0) == 0);
    static uint64_t expected = UINT64_C(0x5eed5eed5eed5eed);
    *(volatile uint64_t *)at(3) = expected;
    HANDLE thread = CreateThread(NULL, 0, reader, &expected, 0, NULL);
    for (int i = 0; i < 2000; ++i) {
        /* One view [0, 8), then page 7 cut off it: [0, 7) is mapped again with the reader on it. */
        address = at(0);
        assert(map(&address, 8 * PAGE, 3, MAP_AT, phys, 0) == 0);
        assert(unmap(at(7), PAGE) == 0);
    }
    reader_stop = 1;
    DWORD read_some = 0;
    WaitForSingleObject(thread, INFINITE);
    GetExitCodeThread(thread, &read_some);
    CloseHandle(thread);
    assert(read_some);
    printf("PASS: concurrent view splits (%ld faults retried)\n", (long)retried);
}
#endif

int main(void) {
#ifdef _WIN32
    runtime_memory_init();
#endif
    runtime_start(1);
    if (getenv("BB_TEST_SEED")) seed = strtoull(getenv("BB_TEST_SEED"), NULL, 0);
    Allocate allocate = GET(Allocate, "rTXw65xmLIA#p#J");
    Map map = GET(Map, "L-Q3LEjIbgA#p#J");
    Unmap unmap = GET(Unmap, "cQke9UuBQOk#p#J");
    Protect protect = GET(Protect, "vSMAm3cxYTY#p#J");
    Query query = GET(Query, "WFcfL2lzido#p#J");
    assert(allocate && map && unmap && protect && query);
    int64_t phys = -1;
    assert(allocate(0, INT64_C(1) << 32, (uint64_t)PAGES * PAGE, 0, 0, &phys) == 0);
    unsigned maps = 0, unmaps = 0, protects = 0, writes = 0, tracked = 0;
    for (int step = 0; step < 4000; ++step) {
        const int first = (int)next(PAGES), count = 1 + (int)next((unsigned)(PAGES - first < 8 ? PAGES - first : 8));
        const unsigned op = next(5);
        if (getenv("BB_TEST_TRACE")) fprintf(stderr, "step %d: op %u pages %d+%d\n", step, op, first, count);
        switch (op) {
        case 0: { /* map (over whatever is there) */
            const int source = (int)next((unsigned)(PAGES - count + 1));
            const int prot = next(3) ? 3 : 1;
            void *address = at(first);
            assert(map(&address, (uint64_t)count * PAGE, prot, MAP_AT, phys + (int64_t)source * PAGE, 0) == 0);
            assert(address == at(first));
            for (int i = 0; i < count; ++i) pages[first + i] = (Page){1, prot, source + i}, gpu_readonly[first + i] = 0;
            ++maps;
            break;
        }
        case 1: /* unmap part of what is there */
            assert(unmap(at(first), (uint64_t)count * PAGE) == 0);
            for (int i = 0; i < count; ++i) pages[first + i].mapped = 0, gpu_readonly[first + i] = 0;
            ++unmaps;
            break;
        case 2: { /* guest protection over mapped pages only */
            int covered = 1;
            for (int i = 0; i < count; ++i) covered &= pages[first + i].mapped;
            const int prot = next(2) ? 3 : 1;
            const int32_t r = protect(at(first), (uint64_t)count * PAGE, prot);
            if (!covered) { assert((uint32_t)r == 0x80020016); break; }
            assert(r == 0);
            for (int i = 0; i < count; ++i) pages[first + i].prot = prot, gpu_readonly[first + i] = 0;
            ++protects;
            break;
        }
        case 3: { /* GPU write tracking: the next map or unmap elsewhere in the view must keep it */
            int covered = 1;
            for (int i = 0; i < count; ++i) covered &= pages[first + i].mapped && (pages[first + i].prot & 2);
            if (!covered) break;
            const int on = (int)next(2);
            runtime_memory_gpu_protect((uintptr_t)at(first), (uint64_t)count * PAGE, 1, !on);
            for (int i = 0; i < count; ++i) gpu_readonly[first + i] = on;
            ++tracked;
            break;
        }
        default: /* a write through one alias */
            for (int i = first; i < first + count; ++i) {
                if (!pages[i].mapped || !(pages[i].prot & 2) || gpu_readonly[i]) continue;
                const uint64_t value = ((uint64_t)step << 16) | (uint64_t)i;
                *(volatile uint64_t *)at(i) = value;
                content[pages[i].phys] = value;
                ++writes;
            }
        }
        check(query);
    }
    printf("PASS: guest memory model (%u maps, %u unmaps, %u protects, %u tracking changes, %u writes)\n",
           maps, unmaps, protects, tracked, writes);
#ifdef _WIN32
    concurrent_splits(map, unmap, phys);
#endif
    return 0;
}

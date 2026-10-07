/* Diagnostic (Windows): BB_CAMSCAN=<file> finds the game's camera angles in its memory, for mouse
 * look that sets them directly. The renderer publishes the camera's yaw and pitch (from the view
 * matrix, bbgpu_camera_angles). Writing "scan" into the file collects every float in the game's
 * memory (its image and the PS4 address range) that matches them under a few conventions (sign,
 * quarter-turn offsets, any multiple of a full turn); "refine", after the camera has moved, keeps
 * the candidates that followed it. The file is emptied when a step is done; candidates are printed
 * once few are left. */
#define _GNU_SOURCE
#include "runtime.h"
#include "gpu/bbgpu.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#ifdef _WIN32
#include <windows.h>

/* Set by probe.c: a hardware write breakpoint on every thread ("watchc"). */
void (*runtime_watch_hook)(uintptr_t address);

typedef struct { uintptr_t address; uint8_t kind, transform; } Candidate; /* kind 0 yaw, 1 pitch */
static Candidate *candidates;
static size_t candidate_count, candidate_capacity;
static const unsigned char *image_base;
static size_t image_size;

#define PI 3.14159265358979f
#define TOLERANCE 0.003f
/* transform t: value = sign * angle + offset (mod 2 pi) */
static const float signs[8] = {1, 1, 1, 1, -1, -1, -1, -1};
static const float offsets[8] = {0, PI / 2, PI, -PI / 2, 0, PI / 2, PI, -PI / 2};

static float wrap(float a) {
    a = fmodf(a, 2 * PI);
    if (a > PI) a -= 2 * PI;
    if (a < -PI) a += 2 * PI;
    return a;
}
static int matches(float value, float angle, int t) {
    if (!isfinite(value) || value == 0.0f || fabsf(value) > 1000.0f) return 0;
    return fabsf(wrap(value - (signs[t] * angle + offsets[t]))) < TOLERANCE;
}
static int read_float(uintptr_t address, float *value) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (const void *)address, value, 4, &got) && got == 4;
}
static void add(uintptr_t address, int kind, int t) {
    if (candidate_count == candidate_capacity) {
        candidate_capacity = candidate_capacity ? candidate_capacity * 2 : 1 << 16;
        candidates = realloc(candidates, candidate_capacity * sizeof(*candidates));
        if (!candidates) { candidate_count = candidate_capacity = 0; return; }
    }
    candidates[candidate_count++] = (Candidate){address, (uint8_t)kind, (uint8_t)t};
}
static void scan_range(uintptr_t start, uintptr_t end, float yaw, float pitch) {
    for (uintptr_t a = start; a < end;) {
        MEMORY_BASIC_INFORMATION info;
        if (!VirtualQuery((void *)a, &info, sizeof(info))) break;
        const uintptr_t region_end = (uintptr_t)info.BaseAddress + info.RegionSize;
        const DWORD p = info.Protect & 0xff;
        const int readable = info.State == MEM_COMMIT && !(info.Protect & PAGE_GUARD) &&
                             (p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
                              p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE);
        if (readable) {
            const uintptr_t from = (a > (uintptr_t)info.BaseAddress ? a : (uintptr_t)info.BaseAddress + 3) & ~(uintptr_t)3;
            const uintptr_t to = region_end < end ? region_end : end;
            /* The 16 targets, wrapped once; each value is wrapped once (most need no fmodf). */
            float targets[16];
            for (int t = 0; t < 8; ++t) {
                targets[t] = wrap(signs[t] * yaw + offsets[t]);
                targets[8 + t] = wrap(signs[t] * pitch + offsets[t]);
            }
            /* Copied out in chunks: a page the game or the GPU's tracking changes meanwhile makes
             * the copy fail instead of faulting in this thread. */
            static float chunk[1 << 18];
            for (uintptr_t c = from; c < to; c += sizeof(chunk)) {
                const size_t bytes = to - c < sizeof(chunk) ? (size_t)(to - c) & ~(size_t)3 : sizeof(chunk);
                SIZE_T got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), (const void *)c, chunk, bytes, &got)) continue;
                for (size_t i = 0; i < got / 4; ++i) {
                    float v = chunk[i];
                    if (!(fabsf(v) <= 1000.0f) || v == 0.0f) continue; /* also drops NaN */
                    if (v > PI || v < -PI) v = wrap(v);
                    for (int t = 0; t < 16; ++t) {
                        float d = v - targets[t];
                        if (d > PI) d -= 2 * PI;
                        else if (d < -PI) d += 2 * PI;
                        if (d < TOLERANCE && d > -TOLERANCE) add(c + i * 4, t >> 3, t & 7);
                    }
                }
            }
        }
        a = region_end;
    }
}
static void print_candidates(void) {
    for (size_t i = 0; i < candidate_count; ++i) {
        const Candidate *c = &candidates[i];
        const int in_image = (const unsigned char *)c->address >= image_base &&
                             (const unsigned char *)c->address < image_base + image_size;
        float value = 0.0f;
        read_float(c->address, &value);
        printf("Camscan: %s at %p%s (image offset 0x%llx), value %.5f, sign %+.0f offset %.4f\n",
               c->kind ? "pitch" : "yaw  ", (void *)c->address, in_image ? " in image" : "",
               in_image ? (unsigned long long)((const unsigned char *)c->address - image_base) : 0ull,
               value, signs[c->transform], offsets[c->transform]);
    }
}
/* Calls found(location, value) for every 8-byte value in [start, end) that is in [low, high]. */
typedef void (*PointerFound)(uintptr_t location, uintptr_t value, void *context);
static void find_pointers(uintptr_t start, uintptr_t end, uintptr_t low, uintptr_t high,
                          PointerFound found, void *context) {
    for (uintptr_t a = start; a < end;) {
        MEMORY_BASIC_INFORMATION info;
        if (!VirtualQuery((void *)a, &info, sizeof(info))) break;
        const uintptr_t region_end = (uintptr_t)info.BaseAddress + info.RegionSize;
        const DWORD p = info.Protect & 0xff;
        if (info.State == MEM_COMMIT && !(info.Protect & PAGE_GUARD) &&
            (p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
             p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE)) {
            static uint64_t chunk[1 << 17];
            const uintptr_t to = region_end < end ? region_end : end;
            for (uintptr_t c = (a > (uintptr_t)info.BaseAddress ? a : (uintptr_t)info.BaseAddress); c < to;
                 c += sizeof(chunk)) {
                const size_t bytes = to - c < sizeof(chunk) ? (size_t)(to - c) & ~(size_t)7 : sizeof(chunk);
                SIZE_T got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), (const void *)c, chunk, bytes, &got)) continue;
                for (size_t i = 0; i < got / 8; ++i)
                    if (chunk[i] >= low && chunk[i] <= high) found(c + i * 8, (uintptr_t)chunk[i], context);
            }
        }
        a = region_end;
    }
}
typedef struct { uintptr_t target; int depth; } ChainSearch;
static void print_static(uintptr_t location, uintptr_t value, void *context) {
    const ChainSearch *s = context;
    printf("Camscan:   image+0x%llx -> %p (+0x%llx to the %s)\n",
           (unsigned long long)(location - (uintptr_t)image_base), (void *)value,
           (unsigned long long)(s->target - value), s->depth ? "heap pointer" : "angle");
}
static int heap_hits;
static void search_level2(uintptr_t location, uintptr_t value, void *context) {
    (void)context;
    if (++heap_hits > 40) return;
    printf("Camscan:  heap %p -> %p\n", (void *)location, (void *)value);
    ChainSearch s = {location, 1};
    find_pointers((uintptr_t)image_base, (uintptr_t)image_base + image_size, location - 0x400, location,
                  print_static, &s);
}
/* Static pointers to the object holding `address` (within 16 KiB below it), directly or through
 * one heap object (within 1 KiB). */
static void pointer_scan(uintptr_t address) {
    printf("Camscan: pointers to %p:\n", (void *)address);
    ChainSearch s = {address, 0};
    find_pointers((uintptr_t)image_base, (uintptr_t)image_base + image_size, address - 0x4000, address,
                  print_static, &s);
    heap_hits = 0;
    find_pointers(0x800000000ull, 0xfc00000000ull, address - 0x4000, address, search_level2, NULL);
}

static void *camscan_thread(void *path) {
    for (;;) {
        Sleep(200);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char command[32] = {0};
        unsigned long long poke_address = 0;
        float poke_delta = 0.0f;
        if (fscanf(f, "%31s", command) != 1) command[0] = 0;
        if (!strcmp(command, "poke") && fscanf(f, "%llx %f", &poke_address, &poke_delta) != 2) command[0] = 0;
        if (!strcmp(command, "spin")) { /* "spin <radians per 5 ms>": does writing the yaw turn the camera? */
            float delta = 0.0f;
            unsigned index = 0;
            for (; index < candidate_count; ++index)
                if (candidates[index].kind == 0 && candidates[index].transform == 0) break;
            if (fscanf(f, "%f", &delta) == 1 && index < candidate_count) {
                fclose(f);
                float yaw0, pitch0, yaw1, pitch1, value = 0.0f;
                bbgpu_camera_angles(&yaw0, &pitch0);
                volatile float *yaw_state = (volatile float *)candidates[index].address;
                for (int i = 0; i < 400; ++i) {
                    *yaw_state = *yaw_state + delta;
                    Sleep(5);
                }
                bbgpu_camera_angles(&yaw1, &pitch1);
                read_float(candidates[index].address, &value);
                printf("Camscan: spin %p by %.4f x 400: state now %.4f, camera yaw %.4f -> %.4f\n",
                       (void *)candidates[index].address, delta, value, yaw0, yaw1);
                fflush(stdout);
                f = fopen(path, "w");
                if (f) fclose(f);
                continue;
            }
            command[0] = 0;
        }
        if (!strcmp(command, "dumpcode")) { /* "dumpcode <hex image offset> <hex size>" */
            unsigned long long offset = 0, bytes = 0;
            if (fscanf(f, "%llx %llx", &offset, &bytes) == 2 && offset + bytes <= image_size) {
                fclose(f);
                char out_path[128];
                snprintf(out_path, sizeof(out_path), "out/code_%llx.bin", offset);
                FILE *o = fopen(out_path, "wb");
                if (o) {
                    /* Page by page: the gaps between the image's segments are not readable. */
                    static unsigned char page[4096];
                    for (unsigned long long done = 0; done < bytes; done += sizeof(page)) {
                        const size_t n = bytes - done < sizeof(page) ? (size_t)(bytes - done) : sizeof(page);
                        SIZE_T got = 0;
                        if (!ReadProcessMemory(GetCurrentProcess(), image_base + offset + done, page, n, &got))
                            memset(page, 0, n);
                        fwrite(page, 1, n, o);
                    }
                    fclose(o);
                    printf("Camscan: wrote %s\n", out_path);
                }
                f = fopen(path, "w");
                if (f) fclose(f);
                continue;
            }
            command[0] = 0;
        }
        if (!strcmp(command, "watchc")) { /* "watchc <candidate index>": who writes it */
            unsigned index = 0;
            const int parsed = fscanf(f, "%u", &index) == 1;
            if (parsed && index == 999) { /* the first yaw equal to the camera's (sign +1, offset 0) */
                for (index = 0; index < candidate_count; ++index)
                    if (candidates[index].kind == 0 && candidates[index].transform == 0) break;
            }
            if (parsed && index < candidate_count && runtime_watch_hook) {
                fclose(f);
                runtime_watch_hook(candidates[index].address);
                f = fopen(path, "w");
                if (f) fclose(f);
                continue;
            }
            command[0] = 0;
        }
        if (!strcmp(command, "ptrscan")) { /* "ptrscan <candidate index>" */
            unsigned index = 0;
            if (fscanf(f, "%u", &index) == 1 && index < candidate_count) {
                fclose(f);
                pointer_scan(candidates[index].address);
                fflush(stdout);
                f = fopen(path, "w");
                if (f) fclose(f);
                continue;
            }
            command[0] = 0;
        }
        if (!strcmp(command, "pokec")) { /* "pokec <candidate index> <delta>" */
            unsigned index = 0;
            if (fscanf(f, "%u %f", &index, &poke_delta) == 2 && index < candidate_count) {
                poke_address = candidates[index].address;
                strcpy(command, "poke");
            } else {
                command[0] = 0;
            }
        }
        fclose(f);
        if (!command[0]) continue;
        float yaw, pitch;
        if (!bbgpu_camera_angles(&yaw, &pitch)) { puts("Camscan: no camera yet"); }
        else if (!strcmp(command, "poke")) {
            /* "poke <hex address> <delta>": adds delta to the float there, then reports what the
             * camera did over the next half second (does writing it move the camera?). */
            float before = 0.0f;
            if (read_float((uintptr_t)poke_address, &before)) {
                *(volatile float *)(uintptr_t)poke_address = before + poke_delta;
                Sleep(500);
                float yaw2, pitch2, after = 0.0f;
                bbgpu_camera_angles(&yaw2, &pitch2);
                read_float((uintptr_t)poke_address, &after);
                printf("Camscan: poke %llx by %.3f: value %.4f -> %.4f (now %.4f); camera yaw %.4f -> %.4f, "
                       "pitch %.4f -> %.4f\n", poke_address, poke_delta, before, before + poke_delta,
                       after, yaw, yaw2, pitch, pitch2);
            }
        }
        else if (!strcmp(command, "scan")) {
            candidate_count = 0;
            const ULONGLONG started = GetTickCount64();
            scan_range((uintptr_t)image_base, (uintptr_t)image_base + image_size, yaw, pitch);
            scan_range(0x800000000ull, 0xfc00000000ull, yaw, pitch);
            printf("Camscan: scan at yaw %.4f pitch %.4f: %zu candidates (%.1f s)\n", yaw, pitch,
                   candidate_count, (GetTickCount64() - started) / 1000.0);
        } else if (!strcmp(command, "refine")) {
            size_t kept = 0;
            for (size_t i = 0; i < candidate_count; ++i) {
                const Candidate c = candidates[i];
                float value;
                if (read_float(c.address, &value) && matches(value, c.kind ? pitch : yaw, c.transform))
                    candidates[kept++] = c;
            }
            candidate_count = kept;
            printf("Camscan: refine at yaw %.4f pitch %.4f: %zu candidates\n", yaw, pitch, candidate_count);
        }
        if (candidate_count && candidate_count <= 64) print_candidates();
        fflush(stdout);
        f = fopen(path, "w");
        if (f) fclose(f);
    }
    return NULL;
}
void runtime_camscan_start(const void *image, size_t size) {
    const char *path = getenv("BB_CAMSCAN");
    if (!path || !*path) return;
    image_base = image; image_size = size;
    pthread_t thread;
    if (pthread_create(&thread, NULL, camscan_thread, (void *)path) == 0) pthread_detach(thread);
}
#else
void runtime_camscan_start(const void *image, size_t size) { (void)image; (void)size; }
#endif

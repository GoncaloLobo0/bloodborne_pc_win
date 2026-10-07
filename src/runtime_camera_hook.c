/* Mouse look that turns the camera directly (Bloodborne 1.09). The game's camera update adds
 * each frame's turn to the camera's pitch and yaw at image offset 0x143cdf0:
 *     vaddps xmm0, xmm0, xmm1        ; xmm0 = (pitch, yaw), xmm1 = this frame's change
 *     vmulps xmm1, xmm0, [0x48f25f0] ; start of the wrap to +-pi
 * These two instructions are replaced by a jump to a stub that first adds the mouse's pending
 * turn (runtime_camera_turn) to xmm1, so the camera follows the mouse one to one, without the
 * stick's dead zone, acceleration or top speed; the game's own pitch limits still apply.
 * Installed only when the bytes there are the expected ones. xmm2 is free at that point (the
 * next instruction overwrites it without reading it). */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define HOOK_OFFSET 0x143cdf0u
#define HOOK_RESUME 0x143cdfcu
#define WRAP_CONSTANT 0x48f25f0u

static const unsigned char expected[12] = {
    0xc5, 0xf8, 0x58, 0xc1,                         /* vaddps xmm0, xmm0, xmm1 */
    0xc5, 0xf8, 0x59, 0x0d, 0xf4, 0x57, 0x4b, 0x03, /* vmulps xmm1, xmm0, [rip+0x34b57f4] */
};

/* The stub's data: the pending turn (pitch, yaw in radians, as the game's xmm1 lanes) and how
 * many camera updates went through it. */
typedef struct { float pitch, yaw; uint64_t hits; uint32_t active; } HookData;
static volatile HookData *hook_data;

#ifdef _WIN32
static void *alloc_near(const unsigned char *target, size_t size) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const uintptr_t granularity = info.dwAllocationGranularity;
    const uintptr_t base = (uintptr_t)target & ~(granularity - 1);
    for (uintptr_t step = 1; step < 0x7000; ++step) { /* within +-1.75 GiB */
        for (int sign = -1; sign <= 1; sign += 2) {
            const uintptr_t at = base + (uintptr_t)(sign * (intptr_t)(step * granularity));
            void *p = VirtualAlloc((void *)at, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (p) return p;
        }
    }
    return NULL;
}
static void put32(unsigned char *at, int64_t value) { int32_t v = (int32_t)value; memcpy(at, &v, 4); }

int runtime_camera_hook_install(unsigned char *image, size_t image_size) {
    const char *env = getenv("BB_MOUSE_HOOK");
    if (env && env[0] == '0') return 0;
    if (!image || image_size < HOOK_RESUME || memcmp(image + HOOK_OFFSET, expected, sizeof(expected))) {
        puts("Mouse look: camera hook not installed (unexpected game code; stick emulation instead)");
        return 0;
    }
    unsigned char *stub = alloc_near(image + HOOK_OFFSET, 4096);
    if (!stub) { puts("Mouse look: no memory near the game for the camera hook"); return 0; }
    HookData *data = (HookData *)(stub + 2048);
    memset(data, 0, sizeof(*data));
    unsigned char *p = stub;
    /* The frame's "camera input" flag (byte [rbp-0x3c4], read at 0x143ce67) is raised while the
     * mouse turns: otherwise the game eases the pitch back to its last target every frame, as
     * when the stick is released. flag = max(flag, active) without a general register:
     * vmovd xmm2, [rip+active]; vpmaxub xmm2, xmm2, [rbp-0x3c4]; vpextrb [rbp-0x3c4], xmm2, 0 */
    memcpy(p, "\xc5\xf9\x6e\x15", 4); put32(p + 4, (unsigned char *)&data->active - (p + 8)); p += 8;
    memcpy(p, "\xc5\xe9\xde\x95\x3c\xfc\xff\xff", 8); p += 8;
    memcpy(p, "\xc4\xe3\x79\x14\x95\x3c\xfc\xff\xff\x00", 10); p += 10;
    /* vxorps xmm2, xmm2, xmm2; vmovd [rip+active], xmm2 */
    memcpy(p, "\xc5\xe8\x57\xd2", 4); p += 4;
    memcpy(p, "\xc5\xf9\x7e\x15", 4); put32(p + 4, (unsigned char *)&data->active - (p + 8)); p += 8;
    /* vmovq xmm2, [rip+pending] */
    memcpy(p, "\xc5\xfa\x7e\x15", 4); put32(p + 4, (unsigned char *)&data->pitch - (p + 8)); p += 8;
    /* vaddps xmm1, xmm1, xmm2 */
    memcpy(p, "\xc5\xf0\x58\xca", 4); p += 4;
    /* vxorps xmm2, xmm2, xmm2 */
    memcpy(p, "\xc5\xe8\x57\xd2", 4); p += 4;
    /* vmovq [rip+pending], xmm2 */
    memcpy(p, "\xc5\xf9\xd6\x15", 4); put32(p + 4, (unsigned char *)&data->pitch - (p + 8)); p += 8;
    /* lock inc qword [rip+hits] */
    memcpy(p, "\xf0\x48\xff\x05", 4); put32(p + 4, (unsigned char *)&data->hits - (p + 8)); p += 8;
    /* the replaced instructions: vaddps xmm0, xmm0, xmm1; vmulps xmm1, xmm0, [wrap constant] */
    memcpy(p, "\xc5\xf8\x58\xc1", 4); p += 4;
    memcpy(p, "\xc5\xf8\x59\x0d", 4); put32(p + 4, (image + WRAP_CONSTANT) - (p + 8)); p += 8;
    /* jmp back */
    *p = 0xe9; put32(p + 1, (image + HOOK_RESUME) - (p + 5)); p += 5;
    FlushInstructionCache(GetCurrentProcess(), stub, (SIZE_T)(p - stub));

    /* jmp stub; 7-byte nop */
    unsigned char patch[12] = {0xe9, 0, 0, 0, 0, 0x0f, 0x1f, 0x80, 0, 0, 0, 0};
    put32(patch + 1, stub - (image + HOOK_OFFSET + 5));
    DWORD old;
    if (!VirtualProtect(image + HOOK_OFFSET, sizeof(patch), PAGE_EXECUTE_READWRITE, &old)) {
        puts("Mouse look: cannot patch the camera code");
        return 0;
    }
    memcpy(image + HOOK_OFFSET, patch, sizeof(patch));
    VirtualProtect(image + HOOK_OFFSET, sizeof(patch), old, &old);
    FlushInstructionCache(GetCurrentProcess(), image + HOOK_OFFSET, sizeof(patch));
    hook_data = data;
    puts("Mouse look: camera hook installed (BB_MOUSE_HOOK=0 turns it off)");
    return 1;
}
#else
int runtime_camera_hook_install(unsigned char *image, size_t image_size) {
    (void)image; (void)image_size;
    return 0;
}
#endif

int runtime_camera_hook_active(void) { return hook_data != NULL; }

/* Adds a turn (radians) for the next camera update. A turn added while the stub takes the
 * previous one may be lost (a few pixels of mouse motion); no lock is needed. */
void runtime_camera_turn(float pitch, float yaw) {
    if (!hook_data || (pitch == 0.0f && yaw == 0.0f)) return;
    /* While the camera is not updated (menus, loading screens) the turn would pile up and the
     * camera would jump when play resumes: it is dropped after 100 ms without an update. */
    static uint64_t seen_hits, seen_at;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t now = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    if (hook_data->hits != seen_hits) {
        seen_hits = hook_data->hits;
        seen_at = now;
    } else if (now - seen_at > 100000000ull) {
        hook_data->pitch = hook_data->yaw = 0.0f;
        return;
    }
    hook_data->pitch += pitch;
    hook_data->yaw += yaw;
    hook_data->active = 1;
}

uint64_t runtime_camera_hook_hits(void) { return hook_data ? hook_data->hits : 0; }

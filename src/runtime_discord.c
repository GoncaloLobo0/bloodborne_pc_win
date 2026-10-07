/* Discord Rich Presence: "Playing Bloodborne" with the game's cover and the session time, through
 * the local Discord client's IPC (a named pipe on Windows, a Unix socket elsewhere). Discord's own
 * Bloodborne application id gives the name; it has no artwork, so the cover is an image URL.
 * Nothing happens when Discord is not running; BB_DISCORD=0 turns it off. */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#define DISCORD_APP_ID "1440139778141196421" /* Discord's "Bloodborne" (detectable games list) */
#define DISCORD_COVER "https://store.playstation.com/store/api/chihiro/00_09_000/titlecontainer/US/en/999/CUSA03173_00/image"

#ifdef _WIN32
typedef HANDLE Connection;
#define NO_CONNECTION INVALID_HANDLE_VALUE
static Connection connect_discord(void) {
    for (int i = 0; i < 10; ++i) {
        char name[64];
        snprintf(name, sizeof(name), "\\\\.\\pipe\\discord-ipc-%d", i);
        HANDLE pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (pipe != INVALID_HANDLE_VALUE) return pipe;
    }
    return NO_CONNECTION;
}
static int write_all(Connection c, const void *data, size_t size) {
    DWORD done = 0;
    return WriteFile(c, data, (DWORD)size, &done, NULL) && done == size;
}
static int read_all(Connection c, void *data, size_t size) {
    char *p = data;
    while (size) {
        DWORD done = 0;
        if (!ReadFile(c, p, (DWORD)size, &done, NULL) || !done) return 0;
        p += done; size -= done;
    }
    return 1;
}
static void close_discord(Connection c) { CloseHandle(c); }
static long process_id(void) { return (long)GetCurrentProcessId(); }
static void pause_seconds(unsigned s) { Sleep(s * 1000u); }
#else
typedef int Connection;
#define NO_CONNECTION (-1)
static Connection connect_discord(void) {
    const char *dirs[] = {getenv("XDG_RUNTIME_DIR"), getenv("TMPDIR"), "/tmp"};
    for (size_t d = 0; d < sizeof(dirs) / sizeof(*dirs); ++d) {
        if (!dirs[d]) continue;
        for (int i = 0; i < 10; ++i) {
            struct sockaddr_un addr = {.sun_family = AF_UNIX};
            snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/discord-ipc-%d", dirs[d], i);
            int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd < 0) return NO_CONNECTION;
            if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) return fd;
            close(fd);
        }
    }
    return NO_CONNECTION;
}
static int write_all(Connection c, const void *data, size_t size) {
    const char *p = data;
    while (size) {
        ssize_t done = write(c, p, size);
        if (done <= 0) return 0;
        p += done; size -= (size_t)done;
    }
    return 1;
}
static int read_all(Connection c, void *data, size_t size) {
    char *p = data;
    while (size) {
        ssize_t done = read(c, p, size);
        if (done <= 0) return 0;
        p += done; size -= (size_t)done;
    }
    return 1;
}
static void close_discord(Connection c) { close(c); }
static long process_id(void) { return (long)getpid(); }
static void pause_seconds(unsigned s) { sleep(s); }
#endif

/* A frame: opcode and length (little endian), then JSON. */
static int send_frame(Connection c, uint32_t opcode, const char *json) {
    const uint32_t header[2] = {opcode, (uint32_t)strlen(json)};
    return write_all(c, header, sizeof(header)) && write_all(c, json, header[1]);
}
/* Reads one frame; the start of its body goes to `text` (may be NULL). */
static int receive_frame(Connection c, char *text, size_t size) {
    uint32_t header[2];
    if (!read_all(c, header, sizeof(header)) || header[1] > (1u << 20)) return 0;
    char chunk[512];
    size_t kept = 0;
    for (uint32_t left = header[1]; left;) {
        const uint32_t n = left < sizeof(chunk) ? left : (uint32_t)sizeof(chunk);
        if (!read_all(c, chunk, n)) return 0;
        if (text && kept + 1 < size) {
            const size_t copy = n < size - 1 - kept ? n : size - 1 - kept;
            memcpy(text + kept, chunk, copy);
            kept += copy;
        }
        left -= n;
    }
    if (text && size) text[kept] = 0;
    return 1;
}

static void *presence_thread(void *arg) {
    (void)arg;
    const long long start = (long long)time(NULL);
    int reported = 0;
    for (;;) {
        Connection c = connect_discord();
        if (c != NO_CONNECTION) {
            char json[1024];
            snprintf(json, sizeof(json), "{\"v\":1,\"client_id\":\"%s\"}", DISCORD_APP_ID);
            int ok = send_frame(c, 0, json) && receive_frame(c, NULL, 0);
            snprintf(json, sizeof(json),
                     "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"1\",\"args\":{\"pid\":%ld,\"activity\":{"
                     "\"timestamps\":{\"start\":%lld},\"assets\":{\"large_image\":\"%s\","
                     "\"large_text\":\"Bloodborne\"}}}}",
                     process_id(), start, DISCORD_COVER);
            char reply[512];
            ok = ok && send_frame(c, 1, json) && receive_frame(c, reply, sizeof(reply));
            if (ok && !reported) {
                if (strstr(reply, "\"ERROR\"")) printf("Runtime: Discord presence refused: %s\n", reply);
                else puts("Runtime: Discord presence set (BB_DISCORD=0 turns it off)");
                reported = 1;
            }
            /* The presence lasts while the connection is open: wait until Discord closes it. */
            while (ok && receive_frame(c, NULL, 0)) {}
            close_discord(c);
        }
        pause_seconds(30); /* Discord not running (yet), or restarted */
    }
    return NULL;
}

void runtime_discord_start(void) {
    const char *env = getenv("BB_DISCORD");
    if (env && env[0] == '0') return;
    pthread_t thread;
    if (pthread_create(&thread, NULL, presence_thread, NULL) == 0) pthread_detach(thread);
}

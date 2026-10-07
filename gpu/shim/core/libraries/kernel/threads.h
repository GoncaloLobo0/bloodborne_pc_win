// bbport: host threads that may call guest code (AvPlayer allocator callbacks).
// Each thread gets a guest TCB (GS base, TLS) from the C runtime before running.
#pragma once
#include <atomic>
#include <cstdio>
#include <functional>
#include <mutex>
#include <system_error>
#include <stop_token>
#include <thread>
#include "common/types.h"

extern "C" void runtime_thread_attach_host(const char* name);

namespace Libraries::Kernel {
class Thread {
public:
    Thread() = default;
    ~Thread() {
        if (Own()) {
            // Destroyed by its own thread: nobody else can be joining it.
            thread.request_stop();
            thread.detach();
            return;
        }
        Stop();
    }
    void Run(std::function<void(std::stop_token)>&& func) {
        std::scoped_lock lock{mutex};
        thread = std::jthread([func = std::move(func)](std::stop_token stop) {
            runtime_thread_attach_host("bb:hle");
            func(stop);
        });
        owner = thread.get_id();
    }
    // bbport: AvPlayer stops a thread from the game's thread and from the thread itself (at the
    // end of a movie) at the same time. The thread does not detach itself then: that closes the
    // handle the other is waiting on (Windows: join fails, "invalid handle"). Joins are serialized;
    // the thread's own stop only asks it to stop, and whoever stops it next (or destroys it) joins.
    void Join() {
        if (Own()) return;
        std::scoped_lock lock{mutex};
        if (!thread.joinable()) return;
        try {
            thread.join();
        } catch (const std::system_error& e) {
            std::fprintf(stderr, "HLE thread: join failed (%s); detached\n", e.what());
            if (thread.joinable()) thread.detach();
        }
    }
    bool Joinable() const { return thread.joinable(); }
    void Stop() {
        thread.request_stop();
        Join();
    }

private:
    bool Own() const { return owner.load() == std::this_thread::get_id(); }
    std::jthread thread;
    std::atomic<std::thread::id> owner{};
    std::mutex mutex;
};
} // namespace Libraries::Kernel

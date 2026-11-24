// Portable threading primitives.
//
// On toolchains with a working C++11 thread library (MSVC, MinGW-w64 posix,
// GCC/Clang on Linux/macOS) these wrap std::thread / std::mutex /
// std::condition_variable. The legacy MinGW.org GCC (win32 thread model) ships
// without <thread>/<mutex> support, so there we fall back to the native Win32
// kernel primitives (threads, CRITICAL_SECTION, semaphore objects). Either way
// the demos use REAL operating-system threads and REAL locks.
#pragma once

#include <functional>
#include <string>

#if defined(_WIN32) && !defined(_MSC_VER) && !defined(_GLIBCXX_HAS_GTHREADS)
#define DC_USE_WIN32_THREADS 1
#else
#define DC_USE_STD_THREADS 1
#endif

#if defined(DC_USE_STD_THREADS)
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

namespace dc {
namespace sync {

/// Human-readable name of the threading backend compiled in.
std::string backendName();

class Mutex {
public:
    Mutex();
    ~Mutex();
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    void lock();
    void unlock();
    bool tryLock();
private:
#if defined(DC_USE_STD_THREADS)
    std::mutex m_;
#else
    void* cs_; // CRITICAL_SECTION*
#endif
};

class LockGuard {
public:
    explicit LockGuard(Mutex& m) : m_(m) { m_.lock(); }
    ~LockGuard() { m_.unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
private:
    Mutex& m_;
};

/// Counting semaphore (Dijkstra P/V).
class Semaphore {
public:
    Semaphore(int initial, int maximum);
    ~Semaphore();
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    void acquire();   // P / wait
    bool tryAcquire();
    void release();   // V / signal
private:
#if defined(DC_USE_STD_THREADS)
    std::mutex m_;
    std::condition_variable cv_;
    int count_;
    int max_;
#else
    void* handle_; // HANDLE
#endif
};

class Thread {
public:
    explicit Thread(std::function<void()> fn);
    ~Thread();
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    void join();
private:
#if defined(DC_USE_STD_THREADS)
    std::thread t_;
#else
    void* handle_;
    std::function<void()>* fn_;
#endif
    bool joined_ = false;
};

void yieldNow();
void sleepMs(int ms);

} // namespace sync
} // namespace dc

#include "synchronization/Threading.h"

#include <stdexcept>

#if defined(DC_USE_WIN32_THREADS)
#include <windows.h>
#include <process.h>
#else
#include <chrono>
#endif

namespace dc {
namespace sync {

#if defined(DC_USE_STD_THREADS)

std::string backendName() { return "C++ standard library (std::thread / std::mutex / std::condition_variable)"; }

Mutex::Mutex() {}
Mutex::~Mutex() {}
void Mutex::lock() { m_.lock(); }
void Mutex::unlock() { m_.unlock(); }
bool Mutex::tryLock() { return m_.try_lock(); }

Semaphore::Semaphore(int initial, int maximum) : count_(initial), max_(maximum) {}
Semaphore::~Semaphore() {}
void Semaphore::acquire() {
    std::unique_lock<std::mutex> lk(m_);
    cv_.wait(lk, [this] { return count_ > 0; });
    --count_;
}
bool Semaphore::tryAcquire() {
    std::lock_guard<std::mutex> lk(m_);
    if (count_ <= 0) return false;
    --count_;
    return true;
}
void Semaphore::release() {
    {
        std::lock_guard<std::mutex> lk(m_);
        if (count_ < max_) ++count_;
    }
    cv_.notify_one();
}

Thread::Thread(std::function<void()> fn) : t_(std::move(fn)) {}
Thread::~Thread() { if (!joined_ && t_.joinable()) t_.join(); }
void Thread::join() { if (!joined_ && t_.joinable()) t_.join(); joined_ = true; }

void yieldNow() { std::this_thread::yield(); }
void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

#else // ------------------------------------------------------------ Win32

std::string backendName() { return "Win32 kernel primitives (_beginthreadex / CRITICAL_SECTION / Semaphore objects)"; }

Mutex::Mutex() {
    CRITICAL_SECTION* cs = new CRITICAL_SECTION;
    InitializeCriticalSection(cs);
    cs_ = cs;
}
Mutex::~Mutex() {
    CRITICAL_SECTION* cs = static_cast<CRITICAL_SECTION*>(cs_);
    DeleteCriticalSection(cs);
    delete cs;
}
void Mutex::lock() { EnterCriticalSection(static_cast<CRITICAL_SECTION*>(cs_)); }
void Mutex::unlock() { LeaveCriticalSection(static_cast<CRITICAL_SECTION*>(cs_)); }
bool Mutex::tryLock() { return TryEnterCriticalSection(static_cast<CRITICAL_SECTION*>(cs_)) != 0; }

Semaphore::Semaphore(int initial, int maximum) {
    handle_ = CreateSemaphoreA(NULL, initial, maximum, NULL);
    if (!handle_) throw std::runtime_error("CreateSemaphore failed");
}
Semaphore::~Semaphore() { CloseHandle(static_cast<HANDLE>(handle_)); }
void Semaphore::acquire() { WaitForSingleObject(static_cast<HANDLE>(handle_), INFINITE); }
bool Semaphore::tryAcquire() { return WaitForSingleObject(static_cast<HANDLE>(handle_), 0) == WAIT_OBJECT_0; }
void Semaphore::release() { ReleaseSemaphore(static_cast<HANDLE>(handle_), 1, NULL); }

namespace {
unsigned __stdcall threadTrampoline(void* arg) {
    std::function<void()>* fn = static_cast<std::function<void()>*>(arg);
    (*fn)();
    return 0;
}
} // namespace

Thread::Thread(std::function<void()> fn) : handle_(nullptr), fn_(new std::function<void()>(std::move(fn))) {
    uintptr_t h = _beginthreadex(NULL, 0, &threadTrampoline, fn_, 0, NULL);
    if (h == 0) {
        delete fn_;
        fn_ = nullptr;
        throw std::runtime_error("_beginthreadex failed");
    }
    handle_ = reinterpret_cast<void*>(h);
}
Thread::~Thread() {
    join();
}
void Thread::join() {
    if (joined_) return;
    if (handle_) {
        WaitForSingleObject(static_cast<HANDLE>(handle_), INFINITE);
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
    delete fn_;
    fn_ = nullptr;
    joined_ = true;
}

void yieldNow() { Sleep(0); }
void sleepMs(int ms) { Sleep(static_cast<DWORD>(ms)); }

#endif

} // namespace sync
} // namespace dc

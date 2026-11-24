#include "synchronization/ConcurrencyDemo.h"

#include "core/Types.h"
#include "synchronization/Threading.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

namespace dc {

namespace {

using Clock = std::chrono::steady_clock;

struct TraceEvent {
    long seq = 0;
    double tUs = 0;
    int thread = 0;
    const char* action = "";
    long value = 0;
    int active = 0;
};

/// Lock-free bounded trace recorder (does not serialise the threads it watches).
class TraceRecorder {
public:
    explicit TraceRecorder(int limit) : events_(static_cast<std::size_t>(std::max(0, limit))), next_(0), start_(Clock::now()) {}
    void record(int thread, const char* action, long value, int active = 0) {
        int idx = next_.fetch_add(1);
        if (idx < 0 || idx >= static_cast<int>(events_.size())) return;
        TraceEvent& e = events_[idx];
        e.seq = idx;
        e.tUs = std::chrono::duration<double, std::micro>(Clock::now() - start_).count();
        e.thread = thread;
        e.action = action;
        e.value = value;
        e.active = active;
    }
    Json toJson() const {
        Json arr = Json::array();
        int n = std::min(next_.load(), static_cast<int>(events_.size()));
        for (int i = 0; i < n; ++i) {
            const auto& e = events_[i];
            Json x = Json::object();
            x["seq"] = e.seq;
            x["tUs"] = util::round2(e.tUs);
            x["thread"] = e.thread;
            x["action"] = e.action;
            x["value"] = e.value;
            x["active"] = e.active;
            arr.push(x);
        }
        return arr;
    }
    int totalEvents() const { return next_.load(); }
private:
    std::vector<TraceEvent> events_;
    std::atomic<int> next_;
    Clock::time_point start_;
};

double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

void clampCommon(SyncDemoConfig& c) {
    c.threads = std::max(2, std::min(16, c.threads));
    c.iterations = std::max(1, std::min(20000, c.iterations));
    c.traceLimit = std::max(0, std::min(5000, c.traceLimit));
}

Json baseResult(const SyncDemoConfig& c, const std::string& mode, const TraceRecorder& tr, double elapsed) {
    Json r = Json::object();
    r["mode"] = mode;
    r["threads"] = c.threads;
    r["iterations"] = c.iterations;
    r["backend"] = sync::backendName();
    r["elapsedMs"] = util::round2(elapsed);
    r["trace"] = tr.toJson();
    r["traceTotalEvents"] = tr.totalEvents();
    return r;
}

} // namespace

Json ConcurrencyDemo::run(const SyncDemoConfig& cfg) {
    std::string m = util::toUpper(cfg.mode);
    if (m == "RACE" || m == "UNSYNCHRONIZED") return runRace(cfg);
    if (m == "MUTEX") return runMutex(cfg);
    if (m == "SEMAPHORE") return runSemaphore(cfg);
    if (m == "PRODCONS" || m == "PRODUCER_CONSUMER") return runProducerConsumer(cfg);
    throw SimError("Unknown synchronization demo mode '" + cfg.mode + "'");
}

// --------------------------------------------------------------- race condition
Json ConcurrencyDemo::runRace(const SyncDemoConfig& cfgIn) {
    SyncDemoConfig c = cfgIn;
    clampCommon(c);
    TraceRecorder tr(c.traceLimit);
    volatile long counter = 0; // shared, deliberately UNPROTECTED
    std::vector<long> perThread(c.threads, 0);
    auto t0 = Clock::now();
    {
        std::vector<std::unique_ptr<sync::Thread>> ts;
        for (int id = 0; id < c.threads; ++id) {
            ts.emplace_back(new sync::Thread([&, id]() {
                for (int i = 0; i < c.iterations; ++i) {
                    long v = counter;            // READ
                    tr.record(id, "READ", v);
                    sync::yieldNow();            // widen the race window
                    counter = v + 1;             // WRITE (may overwrite another thread's update)
                    tr.record(id, "WRITE", v + 1);
                    perThread[id]++;
                }
            }));
        }
        for (auto& t : ts) t->join();
    }
    double elapsed = msSince(t0);
    long expected = static_cast<long>(c.threads) * c.iterations;
    long actual = counter;
    Json r = baseResult(c, "race", tr, elapsed);
    r["synchronized"] = false;
    r["expected"] = expected;
    r["actual"] = actual;
    r["lostUpdates"] = expected - actual;
    r["correct"] = expected == actual;
    r["explanation"] = "Each thread performs read -> yield -> write on a shared counter without a lock. "
                       "When two threads read the same value before either writes, one increment is lost.";
    return r;
}

// --------------------------------------------------------------- mutex
Json ConcurrencyDemo::runMutex(const SyncDemoConfig& cfgIn) {
    SyncDemoConfig c = cfgIn;
    clampCommon(c);
    TraceRecorder tr(c.traceLimit);
    sync::Mutex mtx;
    long counter = 0;
    std::atomic<int> inside(0), maxInside(0);
    std::atomic<long> contentions(0);
    std::vector<double> waitMs(c.threads, 0.0), maxWait(c.threads, 0.0);
    std::vector<long> acquisitions(c.threads, 0);
    auto t0 = Clock::now();
    {
        std::vector<std::unique_ptr<sync::Thread>> ts;
        for (int id = 0; id < c.threads; ++id) {
            ts.emplace_back(new sync::Thread([&, id]() {
                for (int i = 0; i < c.iterations; ++i) {
                    tr.record(id, "REQUEST", 0);
                    if (!mtx.tryLock()) {
                        contentions++;
                        tr.record(id, "BLOCKED", 0);
                        auto w0 = Clock::now();
                        mtx.lock();
                        double w = msSince(w0);
                        waitMs[id] += w;
                        maxWait[id] = std::max(maxWait[id], w);
                    }
                    int now = ++inside;
                    int prev = maxInside.load();
                    while (now > prev && !maxInside.compare_exchange_weak(prev, now)) {}
                    tr.record(id, "ACQUIRED", counter, now);
                    long v = counter;          // critical section
                    sync::yieldNow();
                    counter = v + 1;
                    tr.record(id, "WRITE", counter, now);
                    acquisitions[id]++;
                    --inside;
                    mtx.unlock();
                    tr.record(id, "RELEASED", 0);
                }
            }));
        }
        for (auto& t : ts) t->join();
    }
    double elapsed = msSince(t0);
    long expected = static_cast<long>(c.threads) * c.iterations;
    Json r = baseResult(c, "mutex", tr, elapsed);
    r["synchronized"] = true;
    r["expected"] = expected;
    r["actual"] = counter;
    r["lostUpdates"] = expected - counter;
    r["correct"] = expected == counter;
    r["maxConcurrentInCriticalSection"] = maxInside.load();
    r["contentions"] = contentions.load();
    double totalWait = 0, mw = 0;
    Json per = Json::array();
    for (int i = 0; i < c.threads; ++i) {
        totalWait += waitMs[i];
        mw = std::max(mw, maxWait[i]);
        Json p = Json::object();
        p["thread"] = i; p["acquisitions"] = acquisitions[i]; p["waitMs"] = util::round2(waitMs[i]);
        per.push(p);
    }
    r["perThread"] = per;
    r["totalWaitMs"] = util::round2(totalWait);
    r["maxWaitMs"] = util::round2(mw);
    r["avgWaitPerAcquisitionMs"] = util::round2(expected ? totalWait / expected : 0);
    r["explanation"] = "The read-modify-write is wrapped in lock()/unlock(). Only one thread can be inside the "
                       "critical section, so no update is lost (mutual exclusion).";
    return r;
}

// --------------------------------------------------------------- semaphore
Json ConcurrencyDemo::runSemaphore(const SyncDemoConfig& cfgIn) {
    SyncDemoConfig c = cfgIn;
    clampCommon(c);
    c.permits = std::max(1, std::min(c.threads, c.permits));
    TraceRecorder tr(c.traceLimit);
    sync::Semaphore sem(c.permits, c.permits);
    sync::Mutex counterLock;
    long counter = 0;
    std::atomic<int> inside(0), maxInside(0);
    std::atomic<long> blocked(0);
    std::vector<double> waitMs(c.threads, 0.0);
    auto t0 = Clock::now();
    {
        std::vector<std::unique_ptr<sync::Thread>> ts;
        for (int id = 0; id < c.threads; ++id) {
            ts.emplace_back(new sync::Thread([&, id]() {
                for (int i = 0; i < c.iterations; ++i) {
                    tr.record(id, "WAIT(P)", 0, inside.load());
                    if (!sem.tryAcquire()) {
                        blocked++;
                        tr.record(id, "BLOCKED", 0, inside.load());
                        auto w0 = Clock::now();
                        sem.acquire();
                        waitMs[id] += msSince(w0);
                    }
                    int now = ++inside;
                    int prev = maxInside.load();
                    while (now > prev && !maxInside.compare_exchange_weak(prev, now)) {}
                    tr.record(id, "ENTER", 0, now);
                    for (int k = 0; k < 3; ++k) sync::yieldNow(); // use the pooled resource
                    {
                        sync::LockGuard g(counterLock);
                        ++counter;
                    }
                    int after = --inside;
                    tr.record(id, "SIGNAL(V)", 0, after);
                    sem.release();
                }
            }));
        }
        for (auto& t : ts) t->join();
    }
    double elapsed = msSince(t0);
    long expected = static_cast<long>(c.threads) * c.iterations;
    Json r = baseResult(c, "semaphore", tr, elapsed);
    r["synchronized"] = true;
    r["permits"] = c.permits;
    r["expected"] = expected;
    r["actual"] = counter;
    r["lostUpdates"] = expected - counter;
    r["maxConcurrent"] = maxInside.load();
    r["limitRespected"] = maxInside.load() <= c.permits;
    r["correct"] = expected == counter && maxInside.load() <= c.permits;
    r["blockedWaits"] = blocked.load();
    double tw = 0;
    for (double w : waitMs) tw += w;
    r["totalWaitMs"] = util::round2(tw);
    r["explanation"] = "A counting semaphore initialised to " + std::to_string(c.permits) +
                       " lets at most that many threads use the pooled resource at once; others block in P() until a V().";
    return r;
}

// --------------------------------------------------------------- producer / consumer
Json ConcurrencyDemo::runProducerConsumer(const SyncDemoConfig& cfgIn) {
    SyncDemoConfig c = cfgIn;
    c.producers = std::max(1, std::min(8, c.producers));
    c.consumers = std::max(1, std::min(8, c.consumers));
    c.bufferSize = std::max(1, std::min(64, c.bufferSize));
    c.iterations = std::max(1, std::min(5000, c.iterations));
    c.threads = c.producers + c.consumers;
    c.traceLimit = std::max(0, std::min(5000, c.traceLimit));
    TraceRecorder tr(c.traceLimit);

    sync::Semaphore empty(c.bufferSize, c.bufferSize), full(0, c.bufferSize);
    sync::Mutex mtx;
    std::vector<long> buffer(c.bufferSize, 0);
    int in = 0, out = 0, count = 0, maxCount = 0;
    long producedSum = 0, consumedSum = 0, producedItems = 0, consumedItems = 0;
    bool overflow = false, underflow = false;
    const long totalItems = static_cast<long>(c.producers) * c.iterations;

    auto t0 = Clock::now();
    {
        std::vector<std::unique_ptr<sync::Thread>> ts;
        for (int p = 0; p < c.producers; ++p) {
            ts.emplace_back(new sync::Thread([&, p]() {
                for (int i = 0; i < c.iterations; ++i) {
                    long item = static_cast<long>(p) * 100000 + i + 1;
                    empty.acquire();
                    {
                        sync::LockGuard g(mtx);
                        buffer[in] = item;
                        in = (in + 1) % c.bufferSize;
                        ++count;
                        if (count > c.bufferSize) overflow = true;
                        maxCount = std::max(maxCount, count);
                        producedSum += item;
                        ++producedItems;
                        tr.record(p, "PRODUCE", item, count);
                    }
                    full.release();
                }
            }));
        }
        for (int k = 0; k < c.consumers; ++k) {
            long share = totalItems / c.consumers + (k < totalItems % c.consumers ? 1 : 0);
            int id = c.producers + k;
            ts.emplace_back(new sync::Thread([&, share, id]() {
                for (long i = 0; i < share; ++i) {
                    full.acquire();
                    {
                        sync::LockGuard g(mtx);
                        long item = buffer[out];
                        out = (out + 1) % c.bufferSize;
                        --count;
                        if (count < 0) underflow = true;
                        consumedSum += item;
                        ++consumedItems;
                        tr.record(id, "CONSUME", item, count);
                    }
                    empty.release();
                }
            }));
        }
        for (auto& t : ts) t->join();
    }
    double elapsed = msSince(t0);
    Json r = baseResult(c, "prodcons", tr, elapsed);
    r["synchronized"] = true;
    r["producers"] = c.producers;
    r["consumers"] = c.consumers;
    r["bufferSize"] = c.bufferSize;
    r["producedItems"] = producedItems;
    r["consumedItems"] = consumedItems;
    r["checksumMatch"] = producedSum == consumedSum;
    r["maxBufferOccupancy"] = maxCount;
    r["overflow"] = overflow;
    r["underflow"] = underflow;
    r["expected"] = totalItems;
    r["actual"] = consumedItems;
    r["correct"] = producedItems == consumedItems && producedSum == consumedSum && !overflow && !underflow;
    r["explanation"] = "Semaphores 'empty' (initially N) and 'full' (initially 0) count free/used slots while a mutex "
                       "protects the buffer indices: producers never overflow and consumers never underflow.";
    return r;
}

} // namespace dc

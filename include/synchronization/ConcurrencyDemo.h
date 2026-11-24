// Real multi-threaded synchronization demonstrations (race condition, mutex,
// counting semaphore, bounded-buffer producer/consumer). Each run spawns real
// OS threads and records an interleaving trace for the frontend to replay.
#pragma once

#include "core/Json.h"

#include <string>
#include <vector>

namespace dc {

struct SyncDemoConfig {
    std::string mode = "race"; // race | mutex | semaphore | prodcons
    int threads = 4;
    int iterations = 1000;
    int permits = 2;      // semaphore mode
    int bufferSize = 4;   // prodcons
    int producers = 2;
    int consumers = 2;
    int traceLimit = 400;
};

class ConcurrencyDemo {
public:
    static Json run(const SyncDemoConfig& cfg);
    static Json runRace(const SyncDemoConfig& cfg);
    static Json runMutex(const SyncDemoConfig& cfg);
    static Json runSemaphore(const SyncDemoConfig& cfg);
    static Json runProducerConsumer(const SyncDemoConfig& cfg);
};

} // namespace dc

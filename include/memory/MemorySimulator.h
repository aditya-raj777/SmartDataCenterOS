// Time-driven memory allocation lab: jobs arrive, request contiguous memory,
// hold it for `duration` time units, then release it.
#pragma once

#include "core/Json.h"
#include "memory/MemoryManager.h"

#include <string>
#include <vector>

namespace dc {

struct MemoryJob {
    int pid = 0;
    std::string name;
    int size = 0;
    int arrival = 0;
    int duration = 0; // <= 0 : never released
};

struct MemoryLabConfig {
    int total = 1024;
    MemoryMode mode = MemoryMode::Dynamic;
    std::vector<int> partitions;
    std::string strategy = "FIRST_FIT";
};

class MemorySimulator {
public:
    /// Returns {strategy, steps[], jobs[], summary{...}}
    static Json run(const MemoryLabConfig& cfg, const std::vector<MemoryJob>& jobs);
    static Json compare(const MemoryLabConfig& cfg, const std::vector<MemoryJob>& jobs);
    static std::vector<MemoryJob> jobsFromJson(const Json& arr);
};

} // namespace dc

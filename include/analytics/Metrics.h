// Performance analytics: per-process outcomes, summaries and comparison ranking.
#pragma once

#include "core/Json.h"

#include <string>
#include <utility>
#include <vector>

namespace dc {

struct ProcessOutcome {
    int pid = 0;
    std::string name;
    int arrival = 0;
    int burst = 0;    // total CPU demand
    int priority = 0;
    int start = -1;
    int completion = -1;
    int turnaround = 0;
    int waiting = 0;  // time spent in READY queue
    int response = 0;
    bool completed = false;
    Json toJson() const;
};

struct PerformanceSummary {
    int processCount = 0;
    int completed = 0;
    int failed = 0;
    double avgWaiting = 0;
    double avgTurnaround = 0;
    double avgResponse = 0;
    double throughput = 0;     // completed per time unit
    double cpuUtilization = 0; // percent
    int busy = 0;
    int idle = 0;
    int makespan = 0;
    int contextSwitches = 0;
    Json toJson() const;
};

class PerformanceAnalyzer {
public:
    static PerformanceSummary summarize(const std::vector<ProcessOutcome>& outcomes, int busy, int idle,
                                        int makespan, int contextSwitches);

    /// For each (metric, lowerIsBetter) pair, find the best value among `rows`
    /// (array of objects) and list every row (by `keyField`) that achieves it.
    static Json bestOf(const Json& rows, const std::vector<std::pair<std::string, bool>>& metrics,
                       const std::string& keyField);
};

} // namespace dc

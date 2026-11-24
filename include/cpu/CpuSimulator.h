// Stand-alone CPU scheduling simulator (CPU bursts only). Used by the CPU lab,
// the comparison lab and the interactive algorithm walkthrough.
#pragma once

#include "analytics/Metrics.h"
#include "core/Json.h"
#include "cpu/CpuScheduler.h"

#include <string>
#include <vector>

namespace dc {

struct CpuJob {
    int pid = 0;
    std::string name;
    int arrival = 0;
    int burst = 1;
    int priority = 0;
};

struct GanttSlice {
    int pid = -1; // -1 => idle
    std::string name;
    int start = 0;
    int end = 0;
};

/// One decision point of the walkthrough (a time at which something happened).
struct CpuStep {
    int time = 0;
    std::vector<std::string> events;
    struct QEntry { int pid; std::string name; int remaining; int priority; };
    std::vector<QEntry> readyQueue; // in FIFO entry order
    int runningPid = -1;
    std::string runningName;
    int runningRemaining = 0;
    int sliceUsed = 0;
    int contextSwitches = 0;
};

struct CpuResult {
    std::string algorithm;
    std::string algorithmName;
    int quantum = 0;
    bool preemptive = false;
    std::vector<GanttSlice> gantt;
    std::vector<ProcessOutcome> outcomes;
    PerformanceSummary summary;
    std::vector<CpuStep> steps;
    Json toJson(bool includeSteps = true) const;
};

class CpuSimulator {
public:
    static CpuResult run(const std::vector<CpuJob>& jobs, const CpuScheduler& scheduler);
    static void appendGantt(std::vector<GanttSlice>& g, int pid, const std::string& name, int t);
};

} // namespace dc

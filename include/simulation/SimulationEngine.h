// Integrated Smart Data Center OS Simulation Engine.
// Coordinates process lifecycle, memory allocation, CPU scheduling, disk I/O,
// and shared-resource synchronization in a single coherent discrete simulation.
#pragma once

#include "analytics/Metrics.h"
#include "core/Json.h"
#include "core/Types.h"
#include "cpu/CpuScheduler.h"
#include "cpu/CpuSimulator.h"
#include "disk/DiskScheduler.h"
#include "memory/MemoryManager.h"
#include "process/Process.h"
#include "process/ProcessControlBlock.h"
#include "synchronization/ResourceManager.h"

#include <string>
#include <vector>

namespace dc {

struct ResourceDef {
    std::string name;
    std::string kind = "mutex"; // mutex | semaphore
    int capacity = 1;
};

struct SimConfig {
    // CPU
    std::string cpuAlgorithm = "RR";
    int quantum = 2;
    bool preemptive = false;
    PriorityOrder priorityOrder = PriorityOrder::LowerIsHigher;

    // Memory
    int memoryTotal = 1024;
    MemoryMode memoryMode = MemoryMode::Dynamic;
    std::vector<int> memoryPartitions;
    std::string memoryStrategy = "FIRST_FIT";

    // Disk
    std::string diskAlgorithm = "SCAN";
    int diskCylinders = 200;
    int diskHead = 53;
    Direction diskDirection = Direction::Up;
    bool diskIncludeReturnSeek = true;

    // Synchronization
    std::vector<ResourceDef> resources;

    // Safety guard
    int maxTicks = 5000;
    bool includeTimeline = true;

    static SimConfig fromJson(const Json& j);
    Json toJson() const;
};

struct SimEvent {
    int time = 0;
    std::string type;
    int pid = -1;
    std::string processName;
    std::string message;
    Json toJson() const;
};

class SimulationEngine {
public:
    /// Run a complete integrated simulation and return a full JSON report:
    /// {config, summary, processes, gantt, disk, memory, resources, events, timeline, metricsHistory}
    static Json run(const SimConfig& cfg, const std::vector<ProcessSpec>& specs);

    /// Built-in realistic sample workloads (small, cpu, memory, disk, sync, mixed).
    static Json sampleWorkloads();
};

} // namespace dc

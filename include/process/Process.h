// Process (job) specification, workload parsing/validation and generation.
#pragma once

#include "core/Json.h"
#include "core/Types.h"

#include <string>
#include <vector>

namespace dc {

enum class BurstType { CPU, IO };

/// One phase of a job: a CPU burst (optionally a critical section that needs
/// shared resources) or a disk I/O burst targeting a cylinder.
struct Burst {
    BurstType type = BurstType::CPU;
    int length = 1;
    int cylinder = -1;                  // IO only
    std::vector<std::string> resources; // CPU only: resources held during this burst

    Json toJson() const;
    std::string toSpec() const; // e.g. "C4", "C2[DB]", "I3@98"
};

/// Immutable description of a job submitted by the user.
struct ProcessSpec {
    int pid = 0;
    std::string name;
    int arrival = 0;
    int priority = 0;
    int memory = 0; // KB
    std::vector<Burst> bursts;

    int totalCpu() const;
    int totalIo() const;
    std::vector<int> ioCylinders() const;
    std::vector<std::string> allResources() const;
    std::string burstSpec() const;
    Json toJson() const;
};

/// Converts frontend JSON into validated ProcessSpecs.
class WorkloadParser {
public:
    /// Accepted per-job fields: pid, name, arrival, priority, memory, and either
    /// "bursts" (array or spec string) or cpuBurst + ioRequests + resources.
    static std::vector<ProcessSpec> fromJson(const Json& arr, int defaultIoTime = 2);

    /// Parses "C4 I3@98 C2[DB,LOG]" style burst sequences.
    static std::vector<Burst> parseBurstSpec(const std::string& spec);

    /// Builds a burst sequence from simple fields: total CPU time split around
    /// I/O requests; resources become a final critical-section CPU burst.
    static std::vector<Burst> buildBursts(int cpuBurst, const std::vector<int>& ioCylinders,
                                          const std::vector<std::string>& resources, int ioTime);

    static Json toJson(const std::vector<ProcessSpec>& procs);
};

/// Deterministic random workload generator (seeded).
class WorkloadGenerator {
public:
    /// profile: "mixed" | "cpu" | "memory" | "disk" | "sync" | "small"
    static std::vector<ProcessSpec> generate(const std::string& profile, int count, unsigned seed,
                                             int cylinders = 200, int maxMemory = 256);
};

} // namespace dc

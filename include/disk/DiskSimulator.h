// Stand-alone disk scheduling lab: services a static request queue.
#pragma once

#include "core/Json.h"
#include "disk/DiskScheduler.h"

#include <string>
#include <vector>

namespace dc {

struct DiskLabConfig {
    int cylinders = 200;
    int head = 53;
    Direction direction = Direction::Up;
    bool includeReturnSeek = true; // count the C-SCAN/C-LOOK return jump
    std::string algorithm = "FCFS";
};

class DiskSimulator {
public:
    /// Returns {algorithm, order[], moves[], path[], totalMovement, averageSeek, ...}
    static Json run(const DiskLabConfig& cfg, const std::vector<int>& requests);
    static Json compare(const DiskLabConfig& cfg, const std::vector<int>& requests);
};

} // namespace dc

// Process Control Block: the runtime record the integrated engine keeps per job.
#pragma once

#include "core/Types.h"
#include "process/Process.h"

#include <set>
#include <string>

namespace dc {

struct ProcessControlBlock {
    ProcessSpec spec;
    ProcessState state = ProcessState::New;
    WaitReason waitReason = WaitReason::None;
    bool arrived = false;

    std::size_t burstIndex = 0; // current burst
    int burstRemaining = 0;     // time left in current burst
    long readySeq = 0;          // FIFO order stamp when (re)entering READY
    int sliceUsed = 0;          // ticks used in current RR quantum

    bool memoryAllocated = false;
    int lastAdmitVersion = -1; // memory release version at last failed admission
    bool memFailLogged = false;
    std::set<std::string> held;     // resources currently held
    std::string blockedOn;          // resource we wait for

    int startTime = -1;      // first time on CPU
    int completionTime = -1;
    int readyWait = 0;       // ticks spent in READY
    int memoryWait = 0;      // ticks spent in NEW after arrival (no memory)
    int ioWait = 0;          // ticks spent waiting for / doing disk I/O
    int resourceWait = 0;    // ticks spent blocked on a shared resource
    int cpuExecuted = 0;
    int ioOperations = 0;
    int preemptions = 0;
    bool failed = false;
    std::string failReason;

    const Burst* current() const {
        return burstIndex < spec.bursts.size() ? &spec.bursts[burstIndex] : nullptr;
    }
    int remainingCpu() const {
        int r = 0;
        for (std::size_t i = burstIndex; i < spec.bursts.size(); ++i)
            if (spec.bursts[i].type == BurstType::CPU) r += (i == burstIndex ? burstRemaining : spec.bursts[i].length);
        return r;
    }
};

} // namespace dc

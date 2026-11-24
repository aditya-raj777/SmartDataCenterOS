// CPU scheduling policies (Strategy pattern). Each policy defines an ordering
// key; ties are always broken FIFO by ready-queue entry order, then by PID.
#pragma once

#include "core/Types.h"

#include <memory>
#include <string>
#include <vector>

namespace dc {

/// What a scheduler may look at when choosing among READY processes.
struct SchedCandidate {
    int pid = 0;
    int priority = 0;
    int remaining = 0;   // remaining time of the current CPU burst
    int burstLength = 0; // full length of the current CPU burst
    long readySeq = 0;   // order of entry into the ready queue
    int arrival = 0;
};

class CpuScheduler {
public:
    virtual ~CpuScheduler() = default;
    virtual std::string code() const = 0;
    virtual std::string name() const = 0;
    virtual bool isPreemptive() const { return false; }
    virtual int timeQuantum() const { return 0; }

    /// <0 if a is strictly preferred over b by this policy's key, 0 if equal.
    virtual int compareKey(const SchedCandidate& a, const SchedCandidate& b) const = 0;

    /// Index of the candidate to dispatch (-1 if empty).
    int selectIndex(const std::vector<SchedCandidate>& ready) const;

    /// True if `best` should preempt `running`.
    bool shouldPreempt(const SchedCandidate& running, const SchedCandidate& best) const {
        return isPreemptive() && compareKey(best, running) < 0;
    }
};

class FcfsScheduler : public CpuScheduler {
public:
    std::string code() const override { return "FCFS"; }
    std::string name() const override { return "First Come First Serve"; }
    int compareKey(const SchedCandidate&, const SchedCandidate&) const override { return 0; }
};

class SjfScheduler : public CpuScheduler {
public:
    std::string code() const override { return "SJF"; }
    std::string name() const override { return "Shortest Job First (non-preemptive)"; }
    int compareKey(const SchedCandidate& a, const SchedCandidate& b) const override { return a.remaining - b.remaining; }
};

class SrtfScheduler : public CpuScheduler {
public:
    std::string code() const override { return "SRTF"; }
    std::string name() const override { return "Shortest Remaining Time First"; }
    bool isPreemptive() const override { return true; }
    int compareKey(const SchedCandidate& a, const SchedCandidate& b) const override { return a.remaining - b.remaining; }
};

class PriorityScheduler : public CpuScheduler {
public:
    PriorityScheduler(bool preemptive, PriorityOrder order) : preemptive_(preemptive), order_(order) {}
    std::string code() const override { return preemptive_ ? "PRIORITY_P" : "PRIORITY"; }
    std::string name() const override { return preemptive_ ? "Preemptive Priority" : "Priority (non-preemptive)"; }
    bool isPreemptive() const override { return preemptive_; }
    int compareKey(const SchedCandidate& a, const SchedCandidate& b) const override {
        return order_ == PriorityOrder::LowerIsHigher ? a.priority - b.priority : b.priority - a.priority;
    }
private:
    bool preemptive_;
    PriorityOrder order_;
};

class RoundRobinScheduler : public CpuScheduler {
public:
    explicit RoundRobinScheduler(int quantum);
    std::string code() const override { return "RR"; }
    std::string name() const override { return "Round Robin (q=" + std::to_string(quantum_) + ")"; }
    int timeQuantum() const override { return quantum_; }
    int compareKey(const SchedCandidate&, const SchedCandidate&) const override { return 0; }
private:
    int quantum_;
};

/// Factory. algo: FCFS | SJF | SRTF | PRIORITY | PRIORITY_P | RR.
/// `preemptive` upgrades SJF->SRTF and PRIORITY->PRIORITY_P.
std::unique_ptr<CpuScheduler> makeScheduler(const std::string& algo, int quantum = 2, bool preemptive = false,
                                            PriorityOrder order = PriorityOrder::LowerIsHigher);

std::vector<std::string> allCpuAlgorithms();
PriorityOrder parsePriorityOrder(const std::string& s);

} // namespace dc

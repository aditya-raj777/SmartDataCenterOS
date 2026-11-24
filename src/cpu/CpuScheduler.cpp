#include "cpu/CpuScheduler.h"

namespace dc {

int CpuScheduler::selectIndex(const std::vector<SchedCandidate>& ready) const {
    int best = -1;
    for (int i = 0; i < static_cast<int>(ready.size()); ++i) {
        if (best < 0) { best = i; continue; }
        const auto& a = ready[i];
        const auto& b = ready[best];
        int k = compareKey(a, b);
        if (k < 0 || (k == 0 && (a.readySeq < b.readySeq || (a.readySeq == b.readySeq && a.pid < b.pid)))) best = i;
    }
    return best;
}

RoundRobinScheduler::RoundRobinScheduler(int quantum) : quantum_(quantum) {
    if (quantum_ < 1) throw SimError("Round Robin time quantum must be >= 1");
}

std::unique_ptr<CpuScheduler> makeScheduler(const std::string& algoIn, int quantum, bool preemptive, PriorityOrder order) {
    std::string a = util::toUpper(util::trim(algoIn));
    if (a == "FCFS" || a == "FIFO") return std::unique_ptr<CpuScheduler>(new FcfsScheduler());
    if (a == "SJF") {
        if (preemptive) return std::unique_ptr<CpuScheduler>(new SrtfScheduler());
        return std::unique_ptr<CpuScheduler>(new SjfScheduler());
    }
    if (a == "SRTF" || a == "SRJF") return std::unique_ptr<CpuScheduler>(new SrtfScheduler());
    if (a == "PRIORITY") return std::unique_ptr<CpuScheduler>(new PriorityScheduler(preemptive, order));
    if (a == "PRIORITY_P" || a == "PPRIORITY") return std::unique_ptr<CpuScheduler>(new PriorityScheduler(true, order));
    if (a == "RR" || a == "ROUND_ROBIN") return std::unique_ptr<CpuScheduler>(new RoundRobinScheduler(quantum));
    throw SimError("Unknown CPU scheduling algorithm '" + algoIn + "'");
}

std::vector<std::string> allCpuAlgorithms() { return {"FCFS", "SJF", "SRTF", "PRIORITY", "PRIORITY_P", "RR"}; }

PriorityOrder parsePriorityOrder(const std::string& s) {
    std::string u = util::toUpper(s);
    if (u == "HIGH" || u == "HIGHER" || u == "HIGHER_IS_HIGHER" || u == "DESC") return PriorityOrder::HigherIsHigher;
    return PriorityOrder::LowerIsHigher;
}

} // namespace dc

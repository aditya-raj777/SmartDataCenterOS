#include "analytics/Metrics.h"

#include "core/Types.h"

#include <cmath>

namespace dc {

Json ProcessOutcome::toJson() const {
    Json j = Json::object();
    j["pid"] = pid;
    j["name"] = name;
    j["arrival"] = arrival;
    j["burst"] = burst;
    j["priority"] = priority;
    j["start"] = start;
    j["completion"] = completion;
    j["turnaround"] = turnaround;
    j["waiting"] = waiting;
    j["response"] = response;
    j["completed"] = completed;
    return j;
}

Json PerformanceSummary::toJson() const {
    Json j = Json::object();
    j["processCount"] = processCount;
    j["completed"] = completed;
    j["failed"] = failed;
    j["avgWaiting"] = util::round2(avgWaiting);
    j["avgTurnaround"] = util::round2(avgTurnaround);
    j["avgResponse"] = util::round2(avgResponse);
    j["throughput"] = std::round(throughput * 10000.0) / 10000.0;
    j["cpuUtilization"] = util::round2(cpuUtilization);
    j["busy"] = busy;
    j["idle"] = idle;
    j["makespan"] = makespan;
    j["contextSwitches"] = contextSwitches;
    return j;
}

PerformanceSummary PerformanceAnalyzer::summarize(const std::vector<ProcessOutcome>& outcomes, int busy, int idle,
                                                  int makespan, int contextSwitches) {
    PerformanceSummary s;
    s.processCount = static_cast<int>(outcomes.size());
    s.busy = busy;
    s.idle = idle;
    s.makespan = makespan;
    s.contextSwitches = contextSwitches;
    double w = 0, t = 0, r = 0;
    for (const auto& o : outcomes) {
        if (!o.completed) { s.failed++; continue; }
        s.completed++;
        w += o.waiting;
        t += o.turnaround;
        r += o.response;
    }
    if (s.completed > 0) {
        s.avgWaiting = w / s.completed;
        s.avgTurnaround = t / s.completed;
        s.avgResponse = r / s.completed;
    }
    if (makespan > 0) {
        s.throughput = static_cast<double>(s.completed) / makespan;
        s.cpuUtilization = 100.0 * busy / makespan;
    }
    return s;
}

Json PerformanceAnalyzer::bestOf(const Json& rows, const std::vector<std::pair<std::string, bool>>& metrics,
                                 const std::string& keyField) {
    Json out = Json::object();
    for (const auto& m : metrics) {
        bool found = false;
        double best = 0;
        for (const auto& r : rows.items()) {
            if (!r.has(m.first) || r[m.first].isNull()) continue;
            double v = r.getNum(m.first);
            if (!found || (m.second ? v < best - 1e-9 : v > best + 1e-9)) { best = v; found = true; }
        }
        Json entry = Json::object();
        entry["lowerIsBetter"] = m.second;
        Json winners = Json::array();
        if (found) {
            entry["value"] = best;
            for (const auto& r : rows.items())
                if (r.has(m.first) && std::fabs(r.getNum(m.first) - best) < 1e-9) winners.push(r[keyField]);
        }
        entry["winners"] = winners;
        out[m.first] = entry;
    }
    return out;
}

} // namespace dc

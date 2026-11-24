#include "memory/MemorySimulator.h"

#include "analytics/Metrics.h"

#include <algorithm>
#include <deque>
#include <map>
#include <set>

namespace dc {

std::vector<MemoryJob> MemorySimulator::jobsFromJson(const Json& arr) {
    std::vector<MemoryJob> out;
    std::set<int> pids;
    int next = 1;
    for (const auto& j : arr.items()) {
        MemoryJob m;
        m.pid = j.has("pid") ? j.getInt("pid") : next;
        next = std::max(next, m.pid + 1);
        if (!pids.insert(m.pid).second) throw SimError("Duplicate PID " + std::to_string(m.pid) + " in memory workload");
        m.name = j.getStr("name", "P" + std::to_string(m.pid));
        m.size = j.getInt("size", j.getInt("memory", 0));
        m.arrival = j.getInt("arrival", 0);
        m.duration = j.getInt("duration", j.getInt("cpuBurst", 0));
        if (m.size <= 0) throw SimError(m.name + ": memory size must be positive");
        if (m.arrival < 0) throw SimError(m.name + ": arrival cannot be negative");
        out.push_back(m);
    }
    return out;
}

Json MemorySimulator::run(const MemoryLabConfig& cfg, const std::vector<MemoryJob>& jobsIn) {
    MemoryManager mm(cfg.total, cfg.mode, cfg.partitions, makePlacementStrategy(cfg.strategy));
    std::vector<MemoryJob> jobs = jobsIn;
    std::stable_sort(jobs.begin(), jobs.end(), [](const MemoryJob& a, const MemoryJob& b) {
        return a.arrival != b.arrival ? a.arrival < b.arrival : a.pid < b.pid;
    });

    struct JobState { int allocAt = -1; int releaseAt = -1; int start = -1; int blockSize = 0; bool rejected = false; int attempts = 0; std::string lastReason; };
    std::map<int, JobState> st;
    for (const auto& j : jobs) st[j.pid] = JobState();

    Json steps = Json::array();
    auto snapshot = [&](int t, const std::string& action, int pid, const std::string& msg, bool ok) {
        Json s = Json::object();
        s["time"] = t;
        s["action"] = action;
        s["pid"] = pid;
        s["message"] = msg;
        s["ok"] = ok;
        s["memory"] = mm.toJson();
        steps.push(s);
    };
    snapshot(0, "INIT", -1, "Memory initialised: " + std::to_string(mm.total()) + " KB, strategy " + mm.strategy().name(), true);

    std::deque<int> pending; // indices into jobs
    std::size_t nextArr = 0;
    std::multimap<int, int> releases; // time -> job idx
    double utilArea = 0;
    int lastT = 0;
    double peakUtil = 0;
    int peakExtFrag = 0, extFragFailures = 0, rejected = 0, allocated = 0;
    int t = 0;
    const int guard = 1000000;

    while (nextArr < jobs.size() || !releases.empty() || !pending.empty()) {
        // next event time
        int nt = guard;
        if (nextArr < jobs.size()) nt = std::min(nt, jobs[nextArr].arrival);
        if (!releases.empty()) nt = std::min(nt, releases.begin()->first);
        if (nt == guard) {
            // pending jobs remain but nothing will ever be released -> they never fit
            for (int idx : pending) {
                st[jobs[idx].pid].rejected = true;
                ++rejected;
                snapshot(t, "BLOCKED", jobs[idx].pid, jobs[idx].name + " can never be allocated: no memory will be released (" + st[jobs[idx].pid].lastReason + ")", false);
            }
            pending.clear();
            break;
        }
        utilArea += mm.utilization() * (nt - lastT);
        lastT = nt;
        t = nt;
        // releases first
        bool released = false;
        while (!releases.empty() && releases.begin()->first == t) {
            int idx = releases.begin()->second;
            releases.erase(releases.begin());
            mm.release(jobs[idx].pid);
            st[jobs[idx].pid].releaseAt = t;
            released = true;
            snapshot(t, "FREE", jobs[idx].pid, jobs[idx].name + " finished and released " + std::to_string(st[jobs[idx].pid].blockSize) + " KB", true);
        }
        // arrivals
        std::set<int> fresh;
        while (nextArr < jobs.size() && jobs[nextArr].arrival == t) {
            const auto& j = jobs[nextArr];
            if (!mm.canEverFit(j.size)) {
                st[j.pid].rejected = true;
                ++rejected;
                snapshot(t, "REJECT", j.pid, j.name + " (" + std::to_string(j.size) + " KB) rejected: larger than " +
                         (cfg.mode == MemoryMode::Fixed ? "any partition" : "total memory"), false);
            } else {
                pending.push_back(static_cast<int>(nextArr));
                fresh.insert(static_cast<int>(nextArr));
            }
            ++nextArr;
        }
        // try allocate pending (FIFO, later jobs may bypass blocked ones).
        // Jobs that already failed are only retried after some memory was released.
        for (auto it = pending.begin(); it != pending.end();) {
            if (!released && !fresh.count(*it)) { ++it; continue; }
            const auto& j = jobs[*it];
            AllocResult r = mm.allocate(j.pid, j.size);
            auto& s = st[j.pid];
            s.attempts++;
            if (r.ok) {
                s.allocAt = t;
                s.start = r.start;
                s.blockSize = r.blockSize;
                ++allocated;
                if (j.duration > 0) releases.insert(std::make_pair(t + j.duration, *it));
                snapshot(t, "ALLOC", j.pid, j.name + " allocated " + std::to_string(j.size) + " KB at address " +
                         std::to_string(r.start) + " (block " + std::to_string(r.blockSize) + " KB)", true);
                it = pending.erase(it);
            } else {
                if (r.externalFragmentation) ++extFragFailures;
                s.lastReason = r.reason;
                snapshot(t, "FAIL", j.pid, j.name + " (" + std::to_string(j.size) + " KB) must wait: " + r.reason, false);
                ++it;
            }
            peakUtil = std::max(peakUtil, mm.utilization());
            peakExtFrag = std::max(peakExtFrag, mm.externalFragmentation());
        }
        if (nextArr >= jobs.size() && releases.empty()) {
            // nothing more can change; any remaining pending are blocked forever
            for (int idx : pending) {
                st[jobs[idx].pid].rejected = true;
                ++rejected;
                snapshot(t, "BLOCKED", jobs[idx].pid, jobs[idx].name + " can never be allocated: " + st[jobs[idx].pid].lastReason, false);
            }
            pending.clear();
        }
    }

    Json jobsOut = Json::array();
    double waitSum = 0;
    int waitCount = 0;
    for (const auto& j : jobs) {
        const auto& s = st[j.pid];
        Json x = Json::object();
        x["pid"] = j.pid;
        x["name"] = j.name;
        x["size"] = j.size;
        x["arrival"] = j.arrival;
        x["duration"] = j.duration;
        x["allocatedAt"] = s.allocAt;
        x["releasedAt"] = s.releaseAt;
        x["address"] = s.start;
        x["blockSize"] = s.blockSize;
        x["status"] = s.rejected ? "REJECTED" : (s.allocAt >= 0 ? "ALLOCATED" : "WAITING");
        x["memoryWait"] = s.allocAt >= 0 ? s.allocAt - j.arrival : -1;
        x["failedAttempts"] = s.allocAt >= 0 ? s.attempts - 1 : s.attempts;
        if (s.allocAt >= 0) { waitSum += s.allocAt - j.arrival; ++waitCount; }
        jobsOut.push(x);
    }
    Json summary = Json::object();
    summary["strategy"] = mm.strategy().code();
    summary["strategyName"] = mm.strategy().name();
    summary["totalMemory"] = mm.total();
    summary["jobs"] = static_cast<int>(jobs.size());
    summary["allocated"] = allocated;
    summary["rejected"] = rejected;
    summary["allocationFailures"] = mm.failures();
    summary["externalFragmentationFailures"] = extFragFailures;
    summary["peakUtilization"] = util::round2(peakUtil);
    summary["avgUtilization"] = lastT > 0 ? util::round2(utilArea / lastT) : util::round2(mm.utilization());
    summary["finalUtilization"] = util::round2(mm.utilization());
    summary["peakExternalFragmentation"] = peakExtFrag;
    summary["finalExternalFragmentation"] = mm.externalFragmentation();
    summary["internalFragmentation"] = mm.internalFragmentation();
    summary["avgMemoryWait"] = waitCount ? util::round2(waitSum / waitCount) : 0.0;
    summary["freeBlocks"] = mm.freeBlockCount();
    summary["endTime"] = t;

    Json out = Json::object();
    out["strategy"] = mm.strategy().code();
    out["strategyName"] = mm.strategy().name();
    out["steps"] = steps;
    out["jobs"] = jobsOut;
    out["summary"] = summary;
    out["final"] = mm.toJson();
    return out;
}

Json MemorySimulator::compare(const MemoryLabConfig& cfgIn, const std::vector<MemoryJob>& jobs) {
    Json results = Json::array();
    Json rows = Json::array();
    for (const auto& s : allMemoryStrategies()) {
        MemoryLabConfig cfg = cfgIn;
        cfg.strategy = s;
        Json r = run(cfg, jobs);
        rows.push(r["summary"]);
        results.push(r);
    }
    Json out = Json::object();
    out["results"] = results;
    out["rows"] = rows;
    out["best"] = PerformanceAnalyzer::bestOf(rows, {{"allocated", false}, {"allocationFailures", true}, {"avgUtilization", false},
                                                     {"peakExternalFragmentation", true}, {"avgMemoryWait", true}, {"internalFragmentation", true}},
                                              "strategy");
    return out;
}

} // namespace dc

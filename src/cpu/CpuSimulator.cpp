#include "cpu/CpuSimulator.h"

#include <algorithm>
#include <set>

namespace dc {

void CpuSimulator::appendGantt(std::vector<GanttSlice>& g, int pid, const std::string& name, int t) {
    if (!g.empty() && g.back().pid == pid && g.back().end == t) {
        g.back().end = t + 1;
        return;
    }
    GanttSlice s;
    s.pid = pid;
    s.name = pid < 0 ? "IDLE" : name;
    s.start = t;
    s.end = t + 1;
    g.push_back(s);
}

CpuResult CpuSimulator::run(const std::vector<CpuJob>& jobsIn, const CpuScheduler& sched) {
    CpuResult res;
    res.algorithm = sched.code();
    res.algorithmName = sched.name();
    res.quantum = sched.timeQuantum();
    res.preemptive = sched.isPreemptive();

    std::vector<CpuJob> jobs = jobsIn;
    {
        std::set<int> pids;
        for (const auto& j : jobs) {
            if (j.burst < 1) throw SimError("Process " + j.name + " has CPU burst < 1");
            if (j.arrival < 0) throw SimError("Process " + j.name + " has negative arrival time");
            if (!pids.insert(j.pid).second) throw SimError("Duplicate PID " + std::to_string(j.pid));
        }
    }
    std::stable_sort(jobs.begin(), jobs.end(), [](const CpuJob& a, const CpuJob& b) {
        return a.arrival != b.arrival ? a.arrival < b.arrival : a.pid < b.pid;
    });

    const int n = static_cast<int>(jobs.size());
    std::vector<int> remaining(n), start(n, -1), completion(n, -1), waiting(n, 0);
    std::vector<long> seq(n, 0);
    for (int i = 0; i < n; ++i) remaining[i] = jobs[i].burst;

    std::vector<int> ready; // indices
    long seqCounter = 0;
    int running = -1, slice = 0, lastPid = -1, cs = 0, busy = 0, idle = 0;
    int nextArr = 0, done = 0, t = 0;
    const int quantum = sched.timeQuantum();

    auto candidate = [&](int i) {
        SchedCandidate c;
        c.pid = jobs[i].pid;
        c.priority = jobs[i].priority;
        c.remaining = remaining[i];
        c.burstLength = jobs[i].burst;
        c.readySeq = seq[i];
        c.arrival = jobs[i].arrival;
        return c;
    };
    auto enqueue = [&](int i) { seq[i] = seqCounter++; ready.push_back(i); };
    auto pickBest = [&]() {
        std::vector<SchedCandidate> cands;
        for (int i : ready) cands.push_back(candidate(i));
        return sched.selectIndex(cands);
    };

    const int guard = 1000000;
    while (done < n) {
        if (t > guard) throw SimError("CPU simulation exceeded time guard");
        std::vector<std::string> ev;

        // 1. arrivals
        while (nextArr < n && jobs[nextArr].arrival <= t) {
            enqueue(nextArr);
            ev.push_back(jobs[nextArr].name + " arrived (burst " + std::to_string(jobs[nextArr].burst) + ") -> ready queue");
            ++nextArr;
        }
        // 2. running process: completion / quantum expiry
        if (running >= 0) {
            if (remaining[running] == 0) {
                completion[running] = t;
                ev.push_back(jobs[running].name + " completed at t=" + std::to_string(t));
                running = -1;
                ++done;
            } else if (quantum > 0 && slice >= quantum) {
                if (!ready.empty()) {
                    ev.push_back("Quantum expired: " + jobs[running].name + " (remaining " + std::to_string(remaining[running]) + ") -> tail of ready queue");
                    enqueue(running);
                    running = -1;
                } else {
                    ev.push_back("Quantum expired for " + jobs[running].name + " but ready queue is empty: it continues");
                    slice = 0;
                }
            }
        }
        if (done == n) {
            CpuStep st; st.time = t; st.events = ev; st.contextSwitches = cs;
            res.steps.push_back(st);
            break;
        }
        // 3. preemption
        if (running >= 0 && sched.isPreemptive() && !ready.empty()) {
            int bi = pickBest();
            int b = ready[bi];
            if (sched.shouldPreempt(candidate(running), candidate(b))) {
                ev.push_back(jobs[b].name + " preempts " + jobs[running].name + " (remaining " + std::to_string(remaining[running]) + ")");
                enqueue(running);
                running = -1;
            }
        }
        // 4. dispatch
        if (running < 0 && !ready.empty()) {
            int bi = pickBest();
            running = ready[bi];
            ready.erase(ready.begin() + bi);
            slice = 0;
            bool switched = lastPid != -1 && lastPid != jobs[running].pid;
            if (switched) ++cs;
            lastPid = jobs[running].pid;
            if (start[running] < 0) start[running] = t;
            ev.push_back("CPU -> " + jobs[running].name + (switched ? " (context switch #" + std::to_string(cs) + ")" : ""));
        }
        // record decision step before executing this tick
        if (!ev.empty()) {
            CpuStep st;
            st.time = t;
            st.events = ev;
            std::vector<int> q = ready;
            std::sort(q.begin(), q.end(), [&](int a, int b) { return seq[a] < seq[b]; });
            for (int i : q) st.readyQueue.push_back({jobs[i].pid, jobs[i].name, remaining[i], jobs[i].priority});
            if (running >= 0) {
                st.runningPid = jobs[running].pid;
                st.runningName = jobs[running].name;
                st.runningRemaining = remaining[running];
                st.sliceUsed = slice;
            }
            st.contextSwitches = cs;
            res.steps.push_back(st);
        }
        // 5. execute one time unit
        if (running >= 0) {
            remaining[running]--;
            slice++;
            busy++;
            appendGantt(res.gantt, jobs[running].pid, jobs[running].name, t);
        } else {
            idle++;
            appendGantt(res.gantt, -1, "IDLE", t);
        }
        for (int i : ready) waiting[i]++;
        ++t;
    }

    for (int i = 0; i < n; ++i) {
        ProcessOutcome o;
        o.pid = jobs[i].pid;
        o.name = jobs[i].name;
        o.arrival = jobs[i].arrival;
        o.burst = jobs[i].burst;
        o.priority = jobs[i].priority;
        o.start = start[i];
        o.completion = completion[i];
        o.turnaround = completion[i] - jobs[i].arrival;
        o.waiting = waiting[i];
        o.response = start[i] - jobs[i].arrival;
        o.completed = true;
        res.outcomes.push_back(o);
    }
    std::sort(res.outcomes.begin(), res.outcomes.end(), [](const ProcessOutcome& a, const ProcessOutcome& b) { return a.pid < b.pid; });
    res.summary = PerformanceAnalyzer::summarize(res.outcomes, busy, idle, n ? t : 0, cs);
    return res;
}

Json CpuResult::toJson(bool includeSteps) const {
    Json j = Json::object();
    j["algorithm"] = algorithm;
    j["algorithmName"] = algorithmName;
    j["quantum"] = quantum;
    j["preemptive"] = preemptive;
    Json g = Json::array();
    for (const auto& s : gantt) {
        Json x = Json::object();
        x["pid"] = s.pid;
        x["name"] = s.name;
        x["start"] = s.start;
        x["end"] = s.end;
        g.push(x);
    }
    j["gantt"] = g;
    Json p = Json::array();
    for (const auto& o : outcomes) p.push(o.toJson());
    j["processes"] = p;
    j["summary"] = summary.toJson();
    if (includeSteps) {
        Json st = Json::array();
        for (const auto& s : steps) {
            Json x = Json::object();
            x["time"] = s.time;
            Json ev = Json::array();
            for (const auto& e : s.events) ev.push(e);
            x["events"] = ev;
            Json q = Json::array();
            for (const auto& e : s.readyQueue) {
                Json qe = Json::object();
                qe["pid"] = e.pid; qe["name"] = e.name; qe["remaining"] = e.remaining; qe["priority"] = e.priority;
                q.push(qe);
            }
            x["readyQueue"] = q;
            x["runningPid"] = s.runningPid;
            x["runningName"] = s.runningName;
            x["runningRemaining"] = s.runningRemaining;
            x["sliceUsed"] = s.sliceUsed;
            x["contextSwitches"] = s.contextSwitches;
            st.push(x);
        }
        j["steps"] = st;
    }
    return j;
}

} // namespace dc

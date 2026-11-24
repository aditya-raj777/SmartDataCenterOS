#include "simulation/SimulationEngine.h"

#include <algorithm>
#include <map>

namespace dc {

// ------------------------------------------------------------------ SimConfig
SimConfig SimConfig::fromJson(const Json& j) {
    SimConfig c;
    if (!j.isObject()) return c;
    if (j.has("cpuAlgorithm")) c.cpuAlgorithm = j.getStr("cpuAlgorithm", c.cpuAlgorithm);
    if (j.has("quantum")) c.quantum = j.getInt("quantum", c.quantum);
    if (j.has("preemptive")) c.preemptive = j.getBool("preemptive", c.preemptive);
    if (j.has("priorityOrder")) c.priorityOrder = parsePriorityOrder(j.getStr("priorityOrder", "LOWER"));

    if (j.has("memoryTotal")) c.memoryTotal = j.getInt("memoryTotal", c.memoryTotal);
    if (j.has("memoryMode")) c.memoryMode = parseMemoryMode(j.getStr("memoryMode", "DYNAMIC"));
    if (j.has("memoryStrategy")) c.memoryStrategy = j.getStr("memoryStrategy", c.memoryStrategy);
    if (j.has("memoryPartitions") && j["memoryPartitions"].isArray()) {
        for (const auto& v : j["memoryPartitions"].items()) c.memoryPartitions.push_back(v.asInt());
    }

    if (j.has("diskAlgorithm")) c.diskAlgorithm = j.getStr("diskAlgorithm", c.diskAlgorithm);
    if (j.has("diskCylinders")) c.diskCylinders = j.getInt("diskCylinders", c.diskCylinders);
    if (j.has("diskHead")) c.diskHead = j.getInt("diskHead", c.diskHead);
    if (j.has("diskDirection")) c.diskDirection = parseDirection(j.getStr("diskDirection", "UP"));
    if (j.has("diskIncludeReturnSeek")) c.diskIncludeReturnSeek = j.getBool("diskIncludeReturnSeek", true);

    if (j.has("maxTicks")) c.maxTicks = std::max(100, std::min(50000, j.getInt("maxTicks", c.maxTicks)));
    if (j.has("includeTimeline")) c.includeTimeline = j.getBool("includeTimeline", true);

    if (j.has("resources") && j["resources"].isArray()) {
        for (const auto& rj : j["resources"].items()) {
            ResourceDef d;
            d.name = util::trim(rj.getStr("name", ""));
            d.kind = rj.getStr("kind", "mutex");
            d.capacity = rj.getInt("capacity", 1);
            if (!d.name.empty()) c.resources.push_back(d);
        }
    }
    return c;
}

Json SimConfig::toJson() const {
    Json j = Json::object();
    j["cpuAlgorithm"] = cpuAlgorithm;
    j["quantum"] = quantum;
    j["preemptive"] = preemptive;
    j["priorityOrder"] = priorityOrder == PriorityOrder::LowerIsHigher ? "LOWER" : "HIGHER";
    j["memoryTotal"] = memoryTotal;
    j["memoryMode"] = memoryMode == MemoryMode::Fixed ? "FIXED" : "DYNAMIC";
    j["memoryStrategy"] = memoryStrategy;
    j["diskAlgorithm"] = diskAlgorithm;
    j["diskCylinders"] = diskCylinders;
    j["diskHead"] = diskHead;
    j["diskDirection"] = toString(diskDirection);
    return j;
}

Json SimEvent::toJson() const {
    Json j = Json::object();
    j["time"] = time;
    j["type"] = type;
    j["pid"] = pid;
    j["processName"] = processName;
    j["message"] = message;
    return j;
}

// ------------------------------------------------------------------ Engine
Json SimulationEngine::run(const SimConfig& cfg, const std::vector<ProcessSpec>& specs) {
    if (cfg.diskCylinders < 2) throw SimError("Disk cylinders must be >= 2");
    if (cfg.diskHead < 0 || cfg.diskHead >= cfg.diskCylinders)
        throw SimError("Initial disk head must be within [0, diskCylinders-1]");

    auto cpuSched = makeScheduler(cfg.cpuAlgorithm, cfg.quantum, cfg.preemptive, cfg.priorityOrder);
    auto diskSched = makeDiskScheduler(cfg.diskAlgorithm);
    MemoryManager mm(cfg.memoryTotal, cfg.memoryMode, cfg.memoryPartitions, makePlacementStrategy(cfg.memoryStrategy));
    ResourceManager rm;

    // Default shared data-center resources
    rm.define("DB_LOCK", "mutex", 1);
    rm.define("LOG_FILE", "mutex", 1);
    rm.define("NET_PORT", "semaphore", 2);
    for (const auto& rd : cfg.resources) rm.define(rd.name, rd.kind, rd.capacity);

    // Initialize PCBs
    std::vector<ProcessControlBlock> pcbs;
    pcbs.reserve(specs.size());
    std::map<int, int> pidToIdx;
    for (const auto& s : specs) {
        for (int c : s.ioCylinders()) {
            if (c < 0 || c >= cfg.diskCylinders)
                throw SimError(s.name + ": disk request cylinder " + std::to_string(c) +
                               " is outside [0, " + std::to_string(cfg.diskCylinders - 1) + "]");
        }
        ProcessControlBlock pcb;
        pcb.spec = s;
        pcb.state = ProcessState::New;
        pcb.burstIndex = 0;
        pcb.burstRemaining = s.bursts.empty() ? 0 : s.bursts[0].length;
        pidToIdx[s.pid] = static_cast<int>(pcbs.size());
        pcbs.push_back(pcb);
    }

    const int n = static_cast<int>(pcbs.size());
    std::vector<int> ready; // indices into pcbs
    long readySeqCounter = 0;

    int runningIdx = -1;
    int lastCpuPid = -1;
    int contextSwitches = 0;
    int cpuBusy = 0, cpuIdle = 0;
    std::vector<GanttSlice> gantt;

    // Disk state
    int diskHead = cfg.diskHead;
    Direction diskDir = cfg.diskDirection;
    std::vector<DiskRequest> diskQueue;
    long diskSeqCounter = 0;
    int diskActiveIdx = -1;
    int diskActiveCylinder = -1;
    int diskRemaining = 0;
    int diskBusy = 0;
    int diskCompletedOps = 0;
    int diskTotalMovement = 0;
    std::vector<SeekMove> diskAllMoves;
    Json diskOrder = Json::array();
    Json diskPath = Json::array();
    diskPath.push(diskHead);

    // Events & timeline
    std::vector<SimEvent> events;
    Json timeline = Json::array();
    Json metricsHistory = Json::array();
    double peakMemUtil = 0;

    int t = 0;
    auto emit = [&](const std::string& type, int idx, const std::string& msg) {
        SimEvent e;
        e.time = t;
        e.type = type;
        e.pid = idx >= 0 ? pcbs[idx].spec.pid : -1;
        e.processName = idx >= 0 ? pcbs[idx].spec.name : "SYSTEM";
        e.message = msg;
        events.push_back(e);
    };

    auto enqueueReady = [&](int idx) {
        pcbs[idx].state = ProcessState::Ready;
        pcbs[idx].waitReason = WaitReason::None;
        pcbs[idx].blockedOn.clear();
        pcbs[idx].readySeq = readySeqCounter++;
        ready.push_back(idx);
    };

    auto enqueueDisk = [&](int idx, int cyl) {
        pcbs[idx].state = ProcessState::Waiting;
        pcbs[idx].waitReason = WaitReason::IO;
        DiskRequest req;
        req.id = static_cast<int>(diskSeqCounter);
        req.cylinder = cyl;
        req.pid = pcbs[idx].spec.pid;
        req.seq = diskSeqCounter++;
        diskQueue.push_back(req);
        emit("DISK_REQUEST", idx, pcbs[idx].spec.name + " requested disk I/O at cylinder " + std::to_string(cyl));
    };

    // Release all resources held by idx, waking any waiters whose needed resources become satisfied
    auto releaseResources = [&](int idx) {
        if (pcbs[idx].held.empty()) return;
        std::vector<std::string> toRelease(pcbs[idx].held.begin(), pcbs[idx].held.end());
        pcbs[idx].held.clear();
        for (const auto& rname : toRelease) {
            int grantedPid = rm.release(rname, pcbs[idx].spec.pid);
            emit("RESOURCE_RELEASED", idx, pcbs[idx].spec.name + " released " + rname);
            if (grantedPid >= 0 && pidToIdx.count(grantedPid)) {
                int wIdx = pidToIdx[grantedPid];
                pcbs[wIdx].held.insert(rname);
                emit("RESOURCE_ACQUIRED", wIdx, pcbs[wIdx].spec.name + " acquired " + rname + " (woke from wait queue)");
                // Check if wIdx still needs any other resources for its current burst
                const Burst* wb = pcbs[wIdx].current();
                bool stillBlocked = false;
                if (wb && wb->type == BurstType::CPU) {
                    for (const auto& need : wb->resources) {
                        if (pcbs[wIdx].held.count(need)) continue;
                        if (rm.acquire(need, pcbs[wIdx].spec.pid)) {
                            pcbs[wIdx].held.insert(need);
                            emit("RESOURCE_ACQUIRED", wIdx, pcbs[wIdx].spec.name + " acquired " + need);
                        } else {
                            pcbs[wIdx].blockedOn = need;
                            stillBlocked = true;
                            emit("RESOURCE_BLOCKED", wIdx, pcbs[wIdx].spec.name + " blocked waiting for " + need);
                            break;
                        }
                    }
                }
                if (!stillBlocked && pcbs[wIdx].state == ProcessState::Waiting &&
                    pcbs[wIdx].waitReason == WaitReason::Resource) {
                    enqueueReady(wIdx);
                    emit("ENTER_READY", wIdx, pcbs[wIdx].spec.name + " unblocked -> READY");
                }
            }
        }
    };

    auto terminateProcess = [&](int idx, bool failed, const std::string& reason) {
        auto& p = pcbs[idx];
        releaseResources(idx);
        rm.removeWaiter(p.spec.pid);
        if (p.memoryAllocated) {
            mm.release(p.spec.pid);
            p.memoryAllocated = false;
            emit("MEMORY_RELEASED", idx, p.spec.name + " released " + std::to_string(p.spec.memory) + " KB");
        }
        p.state = ProcessState::Terminated;
        p.waitReason = WaitReason::None;
        p.failed = failed;
        p.failReason = reason;
        if (!failed) {
            p.completionTime = t;
            emit("JOB_COMPLETED", idx, p.spec.name + " completed at t=" + std::to_string(t));
        } else {
            emit("JOB_FAILED", idx, p.spec.name + " failed: " + reason);
        }
    };

    auto advanceBurst = [&](int idx) {
        auto& p = pcbs[idx];
        p.burstIndex++;
        if (p.burstIndex >= p.spec.bursts.size()) {
            terminateProcess(idx, false, "");
            return;
        }
        const Burst& next = p.spec.bursts[p.burstIndex];
        p.burstRemaining = next.length;
        if (next.type == BurstType::IO) {
            enqueueDisk(idx, next.cylinder);
        } else {
            enqueueReady(idx);
            emit("ENTER_READY", idx, p.spec.name + " entered READY queue");
        }
    };

    auto makeCandidate = [&](int idx) {
        SchedCandidate c;
        c.pid = pcbs[idx].spec.pid;
        c.priority = pcbs[idx].spec.priority;
        c.remaining = pcbs[idx].burstRemaining;
        c.burstLength = pcbs[idx].current() ? pcbs[idx].current()->length : 0;
        c.readySeq = pcbs[idx].readySeq;
        c.arrival = pcbs[idx].spec.arrival;
        return c;
    };

    auto pickBestReady = [&]() {
        std::vector<SchedCandidate> cands;
        cands.reserve(ready.size());
        for (int idx : ready) cands.push_back(makeCandidate(idx));
        return cpuSched->selectIndex(cands);
    };

    auto recordSnapshot = [&](std::size_t eventStart) {
        int cNew = 0, cReady = 0, cRun = 0, cWait = 0, cTerm = 0;
        Json pArr = Json::array();
        for (const auto& p : pcbs) {
            switch (p.state) {
            case ProcessState::New: cNew++; break;
            case ProcessState::Ready: cReady++; break;
            case ProcessState::Running: cRun++; break;
            case ProcessState::Waiting: cWait++; break;
            case ProcessState::Terminated: cTerm++; break;
            }
            Json pj = Json::object();
            pj["pid"] = p.spec.pid;
            pj["name"] = p.spec.name;
            pj["state"] = toString(p.state);
            pj["waitReason"] = toString(p.waitReason);
            pj["remainingCpu"] = p.remainingCpu();
            pj["burstRemaining"] = p.burstRemaining;
            pj["memoryAllocated"] = p.memoryAllocated;
            pj["blockedOn"] = p.blockedOn;
            pj["failed"] = p.failed;
            Json h = Json::array();
            for (const auto& r : p.held) h.push(r);
            pj["heldResources"] = h;
            pArr.push(pj);
        }

        double cpuUtilNow = (cpuBusy + cpuIdle) > 0 ? util::round2(100.0 * cpuBusy / (cpuBusy + cpuIdle)) : 0.0;
        double memUtilNow = util::round2(mm.utilization());
        peakMemUtil = std::max(peakMemUtil, memUtilNow);

        Json mh = Json::object();
        mh["time"] = t;
        mh["cpuUtilization"] = cpuUtilNow;
        mh["memoryUtilization"] = memUtilNow;
        mh["readyCount"] = cReady;
        mh["waitingCount"] = cWait;
        mh["diskHead"] = diskHead;
        mh["diskQueueSize"] = static_cast<int>(diskQueue.size());
        mh["contextSwitches"] = contextSwitches;
        mh["resourceConflicts"] = static_cast<long long>(rm.totalConflicts());
        metricsHistory.push(mh);

        if (!cfg.includeTimeline) return;
        Json snap = Json::object();
        snap["time"] = t;
        snap["runningPid"] = runningIdx >= 0 ? pcbs[runningIdx].spec.pid : -1;
        snap["runningName"] = runningIdx >= 0 ? pcbs[runningIdx].spec.name : "IDLE";
        snap["counts"] = Json::object();
        snap["counts"]["new"] = cNew;
        snap["counts"]["ready"] = cReady;
        snap["counts"]["running"] = cRun;
        snap["counts"]["waiting"] = cWait;
        snap["counts"]["terminated"] = cTerm;
        snap["cpuUtilization"] = cpuUtilNow;
        snap["contextSwitches"] = contextSwitches;
        snap["resourceConflicts"] = static_cast<long long>(rm.totalConflicts());
        snap["processes"] = pArr;
        snap["memory"] = mm.toJson();
        Json dj = Json::object();
        dj["head"] = diskHead;
        dj["direction"] = toString(diskDir);
        dj["activePid"] = diskActiveIdx >= 0 ? pcbs[diskActiveIdx].spec.pid : -1;
        dj["activeCylinder"] = diskActiveCylinder;
        dj["totalMovement"] = diskTotalMovement;
        dj["completedOps"] = diskCompletedOps;
        Json dq = Json::array();
        for (const auto& r : diskQueue) {
            Json rq = Json::object();
            rq["pid"] = r.pid;
            rq["cylinder"] = r.cylinder;
            dq.push(rq);
        }
        dj["queue"] = dq;
        snap["disk"] = dj;
        snap["resources"] = rm.toJson();

        Json tickEvents = Json::array();
        for (std::size_t k = eventStart; k < events.size(); ++k) tickEvents.push(events[k].toJson());
        snap["events"] = tickEvents;
        timeline.push(snap);
    };

    auto allTerminated = [&]() {
        for (const auto& p : pcbs) if (p.state != ProcessState::Terminated) return false;
        return true;
    };

    const int quantum = cpuSched->timeQuantum();

    while (n > 0 && t <= cfg.maxTicks) {
        std::size_t evStart = events.size();

        // 1. Arrivals at time t
        for (int i = 0; i < n; ++i) {
            if (!pcbs[i].arrived && pcbs[i].spec.arrival <= t) {
                pcbs[i].arrived = true;
                emit("JOB_ARRIVED", i, pcbs[i].spec.name + " arrived (CPU=" + std::to_string(pcbs[i].spec.totalCpu()) +
                     ", Mem=" + std::to_string(pcbs[i].spec.memory) + " KB, Prio=" + std::to_string(pcbs[i].spec.priority) + ")");
                if (!mm.canEverFit(pcbs[i].spec.memory)) {
                    terminateProcess(i, true, "Memory request (" + std::to_string(pcbs[i].spec.memory) +
                                     " KB) exceeds system capacity (" + std::to_string(mm.total()) + " KB)");
                }
            }
        }

        // 2. Disk I/O completion
        if (diskActiveIdx >= 0 && diskRemaining <= 0) {
            int dIdx = diskActiveIdx;
            int cyl = diskActiveCylinder;
            diskActiveIdx = -1;
            diskActiveCylinder = -1;
            pcbs[dIdx].ioOperations++;
            diskCompletedOps++;
            emit("DISK_IO_DONE", dIdx, pcbs[dIdx].spec.name + " completed disk I/O at cylinder " + std::to_string(cyl));
            advanceBurst(dIdx);
        }

        // 3. Running CPU burst completion or Round Robin quantum expiry
        if (runningIdx >= 0) {
            if (pcbs[runningIdx].burstRemaining <= 0) {
                int rIdx = runningIdx;
                runningIdx = -1;
                releaseResources(rIdx);
                advanceBurst(rIdx);
            } else if (quantum > 0 && pcbs[runningIdx].sliceUsed >= quantum) {
                if (!ready.empty()) {
                    int rIdx = runningIdx;
                    runningIdx = -1;
                    pcbs[rIdx].preemptions++;
                    emit("CPU_PREEMPTED", rIdx, "Time quantum expired for " + pcbs[rIdx].spec.name +
                         " (remaining=" + std::to_string(pcbs[rIdx].burstRemaining) + ") -> READY");
                    enqueueReady(rIdx);
                } else {
                    pcbs[runningIdx].sliceUsed = 0;
                }
            }
        }

        // 4. Memory Admission (NEW -> READY or WAITING(IO))
        for (int i = 0; i < n; ++i) {
            auto& p = pcbs[i];
            if (!p.arrived || p.state != ProcessState::New) continue;
            if (p.lastAdmitVersion == mm.releaseVersion()) continue;
            AllocResult ar = mm.allocate(p.spec.pid, p.spec.memory);
            if (ar.ok) {
                p.memoryAllocated = true;
                emit("MEMORY_ALLOCATED", i, p.spec.name + " allocated " + std::to_string(p.spec.memory) +
                     " KB at addr " + std::to_string(ar.start));
                const Burst* b = p.current();
                if (b && b->type == BurstType::IO) {
                    enqueueDisk(i, b->cylinder);
                } else {
                    enqueueReady(i);
                    emit("ENTER_READY", i, p.spec.name + " entered READY queue");
                }
            } else {
                p.lastAdmitVersion = mm.releaseVersion();
                if (!p.memFailLogged) {
                    p.memFailLogged = true;
                    emit("MEMORY_FAILED", i, p.spec.name + " waiting for memory: " + ar.reason);
                }
            }
        }

        // 5. Disk Dispatch (if idle and requests pending)
        if (diskActiveIdx < 0 && !diskQueue.empty()) {
            std::vector<SeekMove> moves;
            int qIdx = diskSched->selectNext(diskQueue, diskHead, diskDir, cfg.diskCylinders, moves);
            if (qIdx >= 0) {
                DiskRequest req = diskQueue[qIdx];
                diskQueue.erase(diskQueue.begin() + qIdx);
                int dist = totalSeek(moves, cfg.diskIncludeReturnSeek);
                diskTotalMovement += dist;
                for (const auto& m : moves) {
                    diskAllMoves.push_back(m);
                    diskPath.push(m.to);
                }
                diskOrder.push(req.cylinder);
                diskActiveIdx = pidToIdx[req.pid];
                diskActiveCylinder = req.cylinder;
                diskRemaining = pcbs[diskActiveIdx].burstRemaining;
                emit("DISK_IO_START", diskActiveIdx, pcbs[diskActiveIdx].spec.name +
                     " started disk I/O at cylinder " + std::to_string(req.cylinder) +
                     " (seek=" + std::to_string(dist) + ")");
            }
        }

        // 6. CPU Preemption check (SRTF / Preemptive Priority)
        if (runningIdx >= 0 && cpuSched->isPreemptive() && !ready.empty()) {
            int bestPos = pickBestReady();
            int bestIdx = ready[bestPos];
            if (cpuSched->shouldPreempt(makeCandidate(runningIdx), makeCandidate(bestIdx))) {
                int rIdx = runningIdx;
                runningIdx = -1;
                pcbs[rIdx].preemptions++;
                emit("CPU_PREEMPTED", rIdx, pcbs[bestIdx].spec.name + " preempted " + pcbs[rIdx].spec.name);
                enqueueReady(rIdx);
            }
        }

        // 7. CPU Dispatch (checking shared-resource locks before granting CPU)
        while (runningIdx < 0 && !ready.empty()) {
            int bestPos = pickBestReady();
            int candIdx = ready[bestPos];
            ready.erase(ready.begin() + bestPos);

            const Burst* b = pcbs[candIdx].current();
            bool blocked = false;
            if (b && b->type == BurstType::CPU && !b->resources.empty()) {
                for (const auto& rname : b->resources) {
                    if (pcbs[candIdx].held.count(rname)) continue;
                    if (rm.acquire(rname, pcbs[candIdx].spec.pid)) {
                        pcbs[candIdx].held.insert(rname);
                        emit("RESOURCE_ACQUIRED", candIdx, pcbs[candIdx].spec.name + " acquired " + rname);
                    } else {
                        pcbs[candIdx].state = ProcessState::Waiting;
                        pcbs[candIdx].waitReason = WaitReason::Resource;
                        pcbs[candIdx].blockedOn = rname;
                        emit("RESOURCE_BLOCKED", candIdx, pcbs[candIdx].spec.name + " blocked on " + rname);
                        blocked = true;
                        break;
                    }
                }
            }
            if (blocked) continue;

            runningIdx = candIdx;
            pcbs[runningIdx].state = ProcessState::Running;
            pcbs[runningIdx].waitReason = WaitReason::None;
            pcbs[runningIdx].sliceUsed = 0;
            bool switched = (lastCpuPid != -1 && lastCpuPid != pcbs[runningIdx].spec.pid);
            if (switched) contextSwitches++;
            lastCpuPid = pcbs[runningIdx].spec.pid;
            if (pcbs[runningIdx].startTime < 0) pcbs[runningIdx].startTime = t;
            emit("CPU_DISPATCH", runningIdx, "CPU assigned to " + pcbs[runningIdx].spec.name +
                 (switched ? " (context switch #" + std::to_string(contextSwitches) + ")" : ""));
        }

        // Check completion
        if (allTerminated()) {
            recordSnapshot(evStart);
            break;
        }

        // Check stall (no running CPU, no active disk, no future arrivals)
        bool futureArrivals = false;
        for (const auto& p : pcbs) if (!p.arrived) { futureArrivals = true; break; }
        if (runningIdx < 0 && diskActiveIdx < 0 && !futureArrivals) {
            for (int i = 0; i < n; ++i) {
                if (pcbs[i].state != ProcessState::Terminated) {
                    std::string why = pcbs[i].state == ProcessState::New
                                          ? "Insufficient contiguous memory (deadlocked/unsatisfiable)"
                                          : "Blocked on resource deadlock (" + pcbs[i].blockedOn + ")";
                    terminateProcess(i, true, why);
                }
            }
            recordSnapshot(evStart);
            break;
        }

        // Record snapshot before executing tick [t, t+1)
        recordSnapshot(evStart);

        // Execute 1 tick
        if (runningIdx >= 0) {
            pcbs[runningIdx].burstRemaining--;
            pcbs[runningIdx].sliceUsed++;
            pcbs[runningIdx].cpuExecuted++;
            cpuBusy++;
            CpuSimulator::appendGantt(gantt, pcbs[runningIdx].spec.pid, pcbs[runningIdx].spec.name, t);
        } else {
            cpuIdle++;
            CpuSimulator::appendGantt(gantt, -1, "IDLE", t);
        }

        if (diskActiveIdx >= 0) {
            diskRemaining--;
            pcbs[diskActiveIdx].burstRemaining--;
            diskBusy++;
        }

        for (int i = 0; i < n; ++i) {
            auto& p = pcbs[i];
            if (p.arrived && p.state == ProcessState::New) p.memoryWait++;
            else if (p.state == ProcessState::Ready) p.readyWait++;
            else if (p.state == ProcessState::Waiting && p.waitReason == WaitReason::IO) p.ioWait++;
            else if (p.state == ProcessState::Waiting && p.waitReason == WaitReason::Resource) p.resourceWait++;
        }

        t++;
    }

    // Build final outcomes & summary
    std::vector<ProcessOutcome> outcomes;
    Json procTable = Json::array();
    double totalMemWait = 0, totalIoWait = 0, totalResWait = 0;
    int completedCount = 0, failedCount = 0;

    for (const auto& p : pcbs) {
        ProcessOutcome o;
        o.pid = p.spec.pid;
        o.name = p.spec.name;
        o.arrival = p.spec.arrival;
        o.burst = p.spec.totalCpu();
        o.priority = p.spec.priority;
        o.start = p.startTime;
        o.completion = p.completionTime;
        o.completed = (p.state == ProcessState::Terminated && !p.failed);
        if (o.completed) {
            o.turnaround = p.completionTime - p.spec.arrival;
            o.waiting = p.readyWait;
            o.response = p.startTime >= 0 ? p.startTime - p.spec.arrival : 0;
            completedCount++;
            totalMemWait += p.memoryWait;
            totalIoWait += p.ioWait;
            totalResWait += p.resourceWait;
        } else {
            failedCount++;
        }
        outcomes.push_back(o);

        Json pj = p.spec.toJson();
        pj["state"] = toString(p.state);
        pj["start"] = p.startTime;
        pj["completion"] = p.completionTime;
        pj["turnaround"] = o.completed ? o.turnaround : -1;
        pj["waiting"] = p.readyWait;
        pj["response"] = o.completed ? o.response : -1;
        pj["memoryWait"] = p.memoryWait;
        pj["ioWait"] = p.ioWait;
        pj["resourceWait"] = p.resourceWait;
        pj["preemptions"] = p.preemptions;
        pj["ioOperations"] = p.ioOperations;
        pj["failed"] = p.failed;
        pj["failReason"] = p.failReason;
        procTable.push(pj);
    }

    PerformanceSummary perf = PerformanceAnalyzer::summarize(outcomes, cpuBusy, cpuIdle, t, contextSwitches);
    Json summary = perf.toJson();
    summary["peakMemoryUtilization"] = util::round2(peakMemUtil);
    summary["memoryAllocationFailures"] = mm.failures();
    summary["diskTotalMovement"] = diskTotalMovement;
    summary["diskCompletedOps"] = diskCompletedOps;
    summary["diskAverageSeek"] = diskCompletedOps > 0 ? util::round2(static_cast<double>(diskTotalMovement) / diskCompletedOps) : 0.0;
    summary["diskUtilization"] = t > 0 ? util::round2(100.0 * diskBusy / t) : 0.0;
    summary["resourceAcquisitions"] = static_cast<long long>(rm.totalAcquisitions());
    summary["resourceConflicts"] = static_cast<long long>(rm.totalConflicts());
    summary["avgMemoryWait"] = completedCount > 0 ? util::round2(totalMemWait / completedCount) : 0.0;
    summary["avgIoWait"] = completedCount > 0 ? util::round2(totalIoWait / completedCount) : 0.0;
    summary["avgResourceWait"] = completedCount > 0 ? util::round2(totalResWait / completedCount) : 0.0;

    Json ganttJson = Json::array();
    for (const auto& s : gantt) {
        Json g = Json::object();
        g["pid"] = s.pid; g["name"] = s.name; g["start"] = s.start; g["end"] = s.end;
        ganttJson.push(g);
    }

    Json eventsJson = Json::array();
    for (const auto& e : events) eventsJson.push(e.toJson());

    Json diskJson = Json::object();
    diskJson["algorithm"] = diskSched->code();
    diskJson["cylinders"] = cfg.diskCylinders;
    diskJson["initialHead"] = cfg.diskHead;
    diskJson["finalHead"] = diskHead;
    diskJson["direction"] = toString(diskDir);
    diskJson["totalMovement"] = diskTotalMovement;
    diskJson["completedOps"] = diskCompletedOps;
    diskJson["averageSeek"] = diskCompletedOps > 0 ? util::round2(static_cast<double>(diskTotalMovement) / diskCompletedOps) : 0.0;
    diskJson["order"] = diskOrder;
    diskJson["path"] = diskPath;

    Json out = Json::object();
    out["config"] = cfg.toJson();
    out["summary"] = summary;
    out["processes"] = procTable;
    out["gantt"] = ganttJson;
    out["disk"] = diskJson;
    out["memory"] = mm.toJson();
    out["resources"] = rm.toJson();
    out["events"] = eventsJson;
    out["timeline"] = timeline;
    out["metricsHistory"] = metricsHistory;
    return out;
}

// ------------------------------------------------------------------ Samples
Json SimulationEngine::sampleWorkloads() {
    auto makeJob = [](int pid, const std::string& name, int arr, int prio, int mem, const std::string& spec) {
        ProcessSpec p;
        p.pid = pid; p.name = name; p.arrival = arr; p.priority = prio; p.memory = mem;
        p.bursts = WorkloadParser::parseBurstSpec(spec);
        return p.toJson();
    };

    Json samples = Json::array();

    // 1. Small workload (5 processes)
    {
        Json s = Json::object();
        s["id"] = "small";
        s["name"] = "Small Workload (5 Jobs)";
        s["description"] = "Clean 5-process data center workload with staggered arrivals, disk I/O, and a shared DB lock.";
        Json p = Json::array();
        p.push(makeJob(1, "auth-svc", 0, 2, 128, "C2 I2@45 C2"));
        p.push(makeJob(2, "api-gw", 1, 1, 192, "C3 C2[DB_LOCK]"));
        p.push(makeJob(3, "cache-sync", 2, 3, 128, "C2 I2@120 C1"));
        p.push(makeJob(4, "billing", 3, 2, 256, "C2 C3[DB_LOCK]"));
        p.push(makeJob(5, "metrics", 5, 4, 96, "C3 I1@30 C1"));
        s["processes"] = p;
        s["recommendedConfig"] = Json::object();
        s["recommendedConfig"]["cpuAlgorithm"] = "RR";
        s["recommendedConfig"]["quantum"] = 2;
        s["recommendedConfig"]["memoryTotal"] = 1024;
        s["recommendedConfig"]["memoryStrategy"] = "FIRST_FIT";
        s["recommendedConfig"]["diskAlgorithm"] = "SCAN";
        samples.push(s);
    }

    // 2. CPU-intensive workload
    {
        Json s = Json::object();
        s["id"] = "cpu";
        s["name"] = "CPU-Intensive Compute Cluster";
        s["description"] = "Long compute-bound ML and video-encoding jobs ideal for comparing FCFS, SJF, SRTF, Priority, and RR.";
        Json p = Json::array();
        p.push(makeJob(1, "ml-train-1", 0, 3, 192, "C12"));
        p.push(makeJob(2, "infer-fast", 1, 1, 128, "C3"));
        p.push(makeJob(3, "video-enc", 2, 2, 256, "C9"));
        p.push(makeJob(4, "health-chk", 3, 1, 64, "C2"));
        p.push(makeJob(5, "sim-monte", 4, 4, 192, "C7"));
        p.push(makeJob(6, "query-opt", 6, 2, 128, "C4"));
        s["processes"] = p;
        s["recommendedConfig"] = Json::object();
        s["recommendedConfig"]["cpuAlgorithm"] = "SRTF";
        s["recommendedConfig"]["quantum"] = 3;
        s["recommendedConfig"]["memoryTotal"] = 1024;
        s["recommendedConfig"]["memoryStrategy"] = "FIRST_FIT";
        s["recommendedConfig"]["diskAlgorithm"] = "FCFS";
        samples.push(s);
    }

    // 3. Memory-intensive workload
    {
        Json s = Json::object();
        s["id"] = "memory";
        s["name"] = "Memory-Intensive (Fragmentation Lab)";
        s["description"] = "Tight 512 KB RAM with varied footprints and staggered releases to expose external fragmentation and placement differences.";
        Json p = Json::array();
        p.push(makeJob(1, "redis-node", 0, 2, 160, "C4"));
        p.push(makeJob(2, "spark-exec", 0, 1, 140, "C7"));
        p.push(makeJob(3, "elastic-idx", 1, 2, 150, "C5"));
        p.push(makeJob(4, "kafka-brk", 2, 3, 180, "C4"));
        p.push(makeJob(5, "flink-job", 4, 2, 110, "C4"));
        p.push(makeJob(6, "huge-model", 5, 1, 260, "C5"));
        p.push(makeJob(7, "sidecar-ag", 6, 4, 60, "C3"));
        s["processes"] = p;
        s["recommendedConfig"] = Json::object();
        s["recommendedConfig"]["cpuAlgorithm"] = "FCFS";
        s["recommendedConfig"]["quantum"] = 2;
        s["recommendedConfig"]["memoryTotal"] = 512;
        s["recommendedConfig"]["memoryStrategy"] = "BEST_FIT";
        s["recommendedConfig"]["diskAlgorithm"] = "FCFS";
        samples.push(s);
    }

    // 4. Disk-intensive workload
    {
        Json s = Json::object();
        s["id"] = "disk";
        s["name"] = "Disk-Intensive Storage Array";
        s["description"] = "Heavy cylinder-scattered storage requests (classic OS textbook cylinders: 98, 183, 37, 122, 14, 124, 65, 67).";
        Json p = Json::array();
        p.push(makeJob(1, "wal-flush", 0, 2, 128, "C1 I2@98 C1 I2@183 C1"));
        p.push(makeJob(2, "snap-read", 0, 1, 128, "C1 I2@37 C1 I2@122 C1"));
        p.push(makeJob(3, "cold-arch", 1, 3, 128, "C1 I2@14 C1 I2@124 C1"));
        p.push(makeJob(4, "idx-scan", 2, 2, 128, "C1 I2@65 C1 I2@67 C1"));
        s["processes"] = p;
        s["recommendedConfig"] = Json::object();
        s["recommendedConfig"]["cpuAlgorithm"] = "RR";
        s["recommendedConfig"]["quantum"] = 2;
        s["recommendedConfig"]["memoryTotal"] = 1024;
        s["recommendedConfig"]["memoryStrategy"] = "FIRST_FIT";
        s["recommendedConfig"]["diskAlgorithm"] = "SCAN";
        s["recommendedConfig"]["diskHead"] = 53;
        s["recommendedConfig"]["diskCylinders"] = 200;
        samples.push(s);
    }

    // 5. Synchronization workload
    {
        Json s = Json::object();
        s["id"] = "sync";
        s["name"] = "Synchronization & Lock Contention";
        s["description"] = "Multiple database writers and loggers competing simultaneously for DB_LOCK (mutex), LOG_FILE (mutex), and NET_PORT (semaphore=2).";
        Json p = Json::array();
        p.push(makeJob(1, "tx-writer-1", 0, 1, 128, "C1 C3[DB_LOCK] C1"));
        p.push(makeJob(2, "tx-writer-2", 0, 2, 128, "C1 C3[DB_LOCK] C1"));
        p.push(makeJob(3, "audit-log-1", 1, 2, 96, "C1 C2[LOG_FILE,NET_PORT] C1"));
        p.push(makeJob(4, "tx-writer-3", 1, 1, 128, "C1 C2[DB_LOCK,LOG_FILE] C1"));
        p.push(makeJob(5, "stream-tx-1", 2, 3, 96, "C1 C3[NET_PORT] C1"));
        p.push(makeJob(6, "stream-tx-2", 2, 3, 96, "C1 C3[NET_PORT] C1"));
        s["processes"] = p;
        s["recommendedConfig"] = Json::object();
        s["recommendedConfig"]["cpuAlgorithm"] = "RR";
        s["recommendedConfig"]["quantum"] = 2;
        s["recommendedConfig"]["memoryTotal"] = 1024;
        s["recommendedConfig"]["memoryStrategy"] = "FIRST_FIT";
        s["recommendedConfig"]["diskAlgorithm"] = "SSTF";
        samples.push(s);
    }

    // 6. Mixed data center workload
    {
        Json s = Json::object();
        s["id"] = "mixed";
        s["name"] = "Mixed Data Center Production";
        s["description"] = "Realistic blend of compute, I/O-heavy, memory-heavy, and synchronized microservice workloads.";
        Json p = Json::array();
        p.push(makeJob(1, "ingress-lb", 0, 1, 128, "C2 I2@42 C2[NET_PORT] C1"));
        p.push(makeJob(2, "pg-primary", 0, 1, 256, "C2 I2@150 C3[DB_LOCK] C1"));
        p.push(makeJob(3, "ml-batch", 1, 4, 320, "C7 I2@185 C3"));
        p.push(makeJob(4, "checkout", 2, 1, 160, "C1 C3[DB_LOCK,LOG_FILE] C1"));
        p.push(makeJob(5, "backup-d", 3, 5, 192, "C2 I3@18 C1 I2@170 C1"));
        p.push(makeJob(6, "telemetry", 4, 3, 96, "C2 C2[LOG_FILE] C1"));
        p.push(makeJob(7, "search-idx", 5, 2, 224, "C3 I2@88 C2[NET_PORT]"));
        p.push(makeJob(8, "report-gen", 7, 3, 160, "C4 I2@112 C2"));
        s["processes"] = p;
        s["recommendedConfig"] = Json::object();
        s["recommendedConfig"]["cpuAlgorithm"] = "RR";
        s["recommendedConfig"]["quantum"] = 2;
        s["recommendedConfig"]["memoryTotal"] = 1024;
        s["recommendedConfig"]["memoryStrategy"] = "BEST_FIT";
        s["recommendedConfig"]["diskAlgorithm"] = "SCAN";
        samples.push(s);
    }

    return samples;
}

} // namespace dc

// Comprehensive C++ unit & integration test suite for Smart Data Center OS Simulator.
// Verifies CPU scheduling, Memory allocation, Disk scheduling, Synchronization,
// Integrated Simulation Engine, and edge cases against hand-computed answers.
#include "analytics/Metrics.h"
#include "core/Json.h"
#include "core/Types.h"
#include "cpu/CpuScheduler.h"
#include "cpu/CpuSimulator.h"
#include "disk/DiskSimulator.h"
#include "memory/MemoryManager.h"
#include "memory/MemorySimulator.h"
#include "process/Process.h"
#include "simulation/SimulationEngine.h"
#include "synchronization/ConcurrencyDemo.h"
#include "synchronization/ResourceManager.h"

#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int gPassed = 0;
int gFailed = 0;

void check(bool cond, const std::string& label, const std::string& detail = "") {
    if (cond) {
        gPassed++;
    } else {
        gFailed++;
        std::cerr << "[FAIL] " << label;
        if (!detail.empty()) std::cerr << " -- " << detail;
        std::cerr << "\n";
    }
}

bool near(double a, double b, double eps = 0.02) { return std::fabs(a - b) <= eps; }

using namespace dc;

void testCpuScheduling() {
    // Classic textbook workload:
    // P1: arr=0, burst=8, prio=3
    // P2: arr=1, burst=4, prio=1
    // P3: arr=2, burst=9, prio=4
    // P4: arr=3, burst=5, prio=2
    std::vector<CpuJob> jobs = {
        {1, "P1", 0, 8, 3},
        {2, "P2", 1, 4, 1},
        {3, "P3", 2, 9, 4},
        {4, "P4", 3, 5, 2}
    };

    // 1. FCFS: P1[0-8], P2[8-12], P3[12-21], P4[21-26]
    // Wait: P1=0, P2=7, P3=10, P4=18 -> avg = 35/4 = 8.75
    // TAT:  P1=8, P2=11, P3=19, P4=23 -> avg = 61/4 = 15.25
    {
        auto r = CpuSimulator::run(jobs, *makeScheduler("FCFS"));
        check(near(r.summary.avgWaiting, 8.75), "CPU FCFS avgWaiting == 8.75");
        check(near(r.summary.avgTurnaround, 15.25), "CPU FCFS avgTurnaround == 15.25");
        check(r.summary.makespan == 26, "CPU FCFS makespan == 26");
        check(near(r.summary.cpuUtilization, 100.0), "CPU FCFS utilization == 100%");
        check(r.summary.contextSwitches == 3, "CPU FCFS contextSwitches == 3");
    }

    // 2. SJF (non-preemptive): P1[0-8], then P2(4)[8-12], P4(5)[12-17], P3(9)[17-26]
    // Wait: P1=0, P2=7, P4=9, P3=15 -> avg = 31/4 = 7.75
    {
        auto r = CpuSimulator::run(jobs, *makeScheduler("SJF"));
        check(near(r.summary.avgWaiting, 7.75), "CPU SJF avgWaiting == 7.75");
        check(r.outcomes[1].completion == 12, "CPU SJF P2 completes at 12");
        check(r.outcomes[3].completion == 17, "CPU SJF P4 completes at 17");
    }

    // 3. SRTF (preemptive SJF):
    // t=0: P1(8) runs -> t=1: P1(7), P2(4) arrives -> P2 preempts!
    // P2[1-5] completes at 5. Ready: P1(7), P3(9), P4(5).
    // P4[5-10] completes at 10. P1[10-17] completes at 17. P3[17-26] completes at 26.
    // Wait: P1=9, P2=0, P3=15, P4=2 -> avg = 26/4 = 6.50
    {
        auto r = CpuSimulator::run(jobs, *makeScheduler("SRTF"));
        check(near(r.summary.avgWaiting, 6.50), "CPU SRTF avgWaiting == 6.50");
        check(r.outcomes[0].completion == 17, "CPU SRTF P1 completes at 17");
        check(r.outcomes[1].completion == 5, "CPU SRTF P2 completes at 5");
    }

    // 4. Priority (non-preemptive, lower number = higher priority):
    // P1[0-8], then P2(p1)[8-12], P4(p2)[12-17], P3(p4)[17-26]
    {
        auto r = CpuSimulator::run(jobs, *makeScheduler("PRIORITY", 2, false, PriorityOrder::LowerIsHigher));
        check(near(r.summary.avgWaiting, 7.75), "CPU Priority NP avgWaiting == 7.75");
    }

    // 5. Priority (preemptive, lower number = higher priority):
    // P1[0-1], P2(p1)[1-5], P4(p2)[5-10], P1(p3)[10-17], P3(p4)[17-26]
    {
        auto r = CpuSimulator::run(jobs, *makeScheduler("PRIORITY_P", 2, true, PriorityOrder::LowerIsHigher));
        check(near(r.summary.avgWaiting, 6.50), "CPU Priority Preemptive avgWaiting == 6.50");
    }

    // 6. Round Robin (q=4) with all arriving at 0: P1=5, P2=3, P3=1
    // P1[0-4], P2[4-7], P3[7-8], P1[8-9]
    // Wait: P1=4, P2=4, P3=7 -> avg = 5.0
    {
        std::vector<CpuJob> rrJobs = {{1, "P1", 0, 5, 1}, {2, "P2", 0, 3, 1}, {3, "P3", 0, 1, 1}};
        auto r = CpuSimulator::run(rrJobs, *makeScheduler("RR", 4));
        check(near(r.summary.avgWaiting, 5.0), "CPU RR(q=4) avgWaiting == 5.0");
        check(r.summary.contextSwitches == 3, "CPU RR(q=4) contextSwitches == 3");
    }

    // 7. Edge cases: empty workload, single process, idle gap, burst=1, RR quantum=1
    {
        auto emptyR = CpuSimulator::run({}, *makeScheduler("FCFS"));
        check(emptyR.summary.processCount == 0 && emptyR.summary.makespan == 0, "CPU empty workload");

        std::vector<CpuJob> single = {{1, "Solo", 5, 1, 1}};
        auto singleR = CpuSimulator::run(single, *makeScheduler("RR", 1));
        check(singleR.summary.idle == 5 && singleR.summary.busy == 1, "CPU single process with idle gap");
        check(singleR.outcomes[0].completion == 6 && singleR.outcomes[0].waiting == 0, "CPU single process completion & wait");
    }
}

void testMemoryManagement() {
    // Fixed partitions: [100, 500, 200, 300, 600]
    // Requests: 212, 417, 112, 426
    // Classic Silberschatz example:
    // First Fit: 212->500, 417->600, 112->200, 426->FAIL (3 allocated, 1 fail)
    // Best Fit:  212->300, 417->500, 112->200, 426->600 (4 allocated, 0 fail!)
    // Worst Fit: 212->600, 417->500, 112->300, 426->FAIL (3 allocated, 1 fail)
    std::vector<int> parts = {100, 500, 200, 300, 600};
    {
        MemoryManager ff(0, MemoryMode::Fixed, parts, makePlacementStrategy("FIRST_FIT"));
        check(ff.allocate(1, 212).ok, "Mem FF alloc 212");
        check(ff.allocate(2, 417).ok, "Mem FF alloc 417");
        check(ff.allocate(3, 112).ok, "Mem FF alloc 112");
        check(!ff.allocate(4, 426).ok, "Mem FF rejects 426");
        check(ff.failures() == 1, "Mem FF failures == 1");
    }
    {
        MemoryManager bf(0, MemoryMode::Fixed, parts, makePlacementStrategy("BEST_FIT"));
        check(bf.allocate(1, 212).start == 800, "Mem BF places 212 in 300-block (addr 800)");
        check(bf.allocate(2, 417).start == 100, "Mem BF places 417 in 500-block (addr 100)");
        check(bf.allocate(3, 112).start == 600, "Mem BF places 112 in 200-block (addr 600)");
        check(bf.allocate(4, 426).start == 1100, "Mem BF places 426 in 600-block (addr 1100)");
        check(bf.failures() == 0, "Mem BF 0 failures");
    }
    {
        MemoryManager wf(0, MemoryMode::Fixed, parts, makePlacementStrategy("WORST_FIT"));
        check(wf.allocate(1, 212).start == 1100, "Mem WF places 212 in 600-block");
        check(wf.allocate(2, 417).start == 100, "Mem WF places 417 in 500-block");
        check(wf.allocate(3, 112).start == 800, "Mem WF places 112 in 300-block");
        check(!wf.allocate(4, 426).ok, "Mem WF rejects 426");
    }
    // Dynamic coalescing & external fragmentation
    {
        MemoryManager dyn(300, MemoryMode::Dynamic, {}, makePlacementStrategy("FIRST_FIT"));
        dyn.allocate(1, 100);
        dyn.allocate(2, 100);
        dyn.allocate(3, 100);
        dyn.release(1); // hole [0,100)
        dyn.release(3); // hole [200,300)
        auto res = dyn.allocate(4, 150);
        check(!res.ok && res.externalFragmentation, "Mem dynamic detects external fragmentation");
        check(dyn.externalFragmentation() == 100, "Mem external fragmentation == 100");
        dyn.release(2); // coalesces all 3 holes into [0,300)
        check(dyn.freeBlockCount() == 1 && dyn.largestFree() == 300, "Mem coalesces adjacent free blocks");
        check(dyn.allocate(4, 150).ok, "Mem allocates 150 after coalescing");
    }
    // Next Fit wrap-around
    {
        MemoryManager nf(0, MemoryMode::Fixed, {100, 100, 100}, makePlacementStrategy("NEXT_FIT"));
        check(nf.allocate(1, 50).start == 0, "Mem NF first at 0");
        check(nf.allocate(2, 50).start == 100, "Mem NF second at 100");
        nf.release(1);
        check(nf.allocate(3, 50).start == 200, "Mem NF third advances to 200 (not 0)");
    }
}

void testDiskScheduling() {
    // Classic Silberschatz disk queue: 98, 183, 37, 122, 14, 124, 65, 67; head=53, cyl=200
    std::vector<int> reqs = {98, 183, 37, 122, 14, 124, 65, 67};
    DiskLabConfig cfg;
    cfg.cylinders = 200;
    cfg.head = 53;
    cfg.direction = Direction::Up;

    // FCFS: |53-98|+|98-183|+|183-37|+|37-122|+|122-14|+|14-124|+|124-65|+|65-67| = 640
    cfg.algorithm = "FCFS";
    check(DiskSimulator::run(cfg, reqs).getInt("totalMovement") == 640, "Disk FCFS == 640");

    // SSTF: 53->65->67->37->14->98->122->124->183 = 12+2+30+23+84+24+2+59 = 236
    cfg.algorithm = "SSTF";
    check(DiskSimulator::run(cfg, reqs).getInt("totalMovement") == 236, "Disk SSTF == 236");

    // SCAN (Up): 53->65->67->98->122->124->183->199->37->14 = (199-53)+(199-14) = 146+185 = 331
    cfg.algorithm = "SCAN";
    check(DiskSimulator::run(cfg, reqs).getInt("totalMovement") == 331, "Disk SCAN (Up) == 331");

    // C-SCAN (Up, includeReturnSeek=true): (199-53) + 199 + (37-0) = 146 + 199 + 37 = 382
    cfg.algorithm = "CSCAN";
    check(DiskSimulator::run(cfg, reqs).getInt("totalMovement") == 382, "Disk C-SCAN (Up) == 382");

    // LOOK (Up): (183-53) + (183-14) = 130 + 169 = 299
    cfg.algorithm = "LOOK";
    check(DiskSimulator::run(cfg, reqs).getInt("totalMovement") == 299, "Disk LOOK (Up) == 299");

    // Edge cases: duplicate cylinders, head at 0
    DiskLabConfig edge;
    edge.cylinders = 100;
    edge.head = 0;
    edge.direction = Direction::Down;
    edge.algorithm = "SCAN";
    auto er = DiskSimulator::run(edge, {0, 0, 10, 10});
    check(er.getInt("totalMovement") == 10, "Disk edge head=0 with duplicate requests == 10");
}

void testSynchronization() {
    // 1. Real OS threads with Mutex must never lose an update
    {
        SyncDemoConfig c;
        c.mode = "mutex"; c.threads = 4; c.iterations = 500; c.traceLimit = 50;
        Json r = ConcurrencyDemo::run(c);
        check(r.getBool("correct"), "Sync Mutex has 0 lost updates");
        check(r.getInt("maxConcurrentInCriticalSection") == 1, "Sync Mutex enforces mutual exclusion (max=1)");
    }
    // 2. Real OS threads with Semaphore (permits=2)
    {
        SyncDemoConfig c;
        c.mode = "semaphore"; c.threads = 5; c.iterations = 300; c.permits = 2; c.traceLimit = 50;
        Json r = ConcurrencyDemo::run(c);
        check(r.getBool("correct"), "Sync Semaphore correct & respects permits<=2");
        check(r.getInt("maxConcurrent") <= 2, "Sync Semaphore maxConcurrent <= 2");
    }
    // 3. Producer-Consumer bounded buffer
    {
        SyncDemoConfig c;
        c.mode = "prodcons"; c.producers = 2; c.consumers = 2; c.bufferSize = 3; c.iterations = 200; c.traceLimit = 50;
        Json r = ConcurrencyDemo::run(c);
        check(r.getBool("correct"), "Sync Producer-Consumer checksum & bounds match");
    }
    // 4. Discrete ResourceManager invariants
    {
        ResourceManager rm;
        rm.define("DB", "mutex", 1);
        check(rm.acquire("DB", 1), "RM P1 acquires DB");
        check(!rm.acquire("DB", 2), "RM P2 blocked on DB");
        check(rm.totalConflicts() == 1, "RM conflict counted");
        check(rm.release("DB", 1) == 2, "RM release hands DB to P2");
        check(rm.holds("DB", 2) && !rm.holds("DB", 1), "RM ownership transferred cleanly");
        check(rm.invariantsHold(), "RM invariants hold");
    }
}

void testIntegratedEngine() {
    // Run every built-in sample workload and verify OS invariants
    Json samples = SimulationEngine::sampleWorkloads();
    check(samples.size() == 6, "6 sample workloads defined");

    for (const auto& s : samples.items()) {
        std::string sid = s.getStr("id");
        SimConfig cfg = SimConfig::fromJson(s["recommendedConfig"]);
        auto procs = WorkloadParser::fromJson(s["processes"]);
        Json res = SimulationEngine::run(cfg, procs);

        int completed = res["summary"].getInt("completed");
        int failed = res["summary"].getInt("failed");
        check(completed + failed == static_cast<int>(procs.size()), "Integrated[" + sid + "] all jobs accounted for");
        check(completed == static_cast<int>(procs.size()), "Integrated[" + sid + "] all jobs completed");
        check(res["memory"].getInt("used") == 0, "Integrated[" + sid + "] all memory released at end");

        // Verify no timeline tick violates core OS invariants:
        // - at most 1 RUNNING process
        // - no RUNNING process lacks memory
        bool invariantsOk = true;
        for (const auto& snap : res["timeline"].items()) {
            int runningCount = 0;
            for (const auto& p : snap["processes"].items()) {
                if (p.getStr("state") == "RUNNING") {
                    runningCount++;
                    if (!p.getBool("memoryAllocated")) invariantsOk = false;
                }
            }
            if (runningCount > 1) invariantsOk = false;
        }
        check(invariantsOk, "Integrated[" + sid + "] timeline OS invariants hold");
    }

    // Memory-unavailable edge case: job requests 2048 KB when total RAM is 512 KB
    {
        SimConfig cfg;
        cfg.memoryTotal = 512;
        ProcessSpec p;
        p.pid = 1; p.name = "too-big"; p.arrival = 0; p.priority = 1; p.memory = 2048;
        p.bursts = WorkloadParser::parseBurstSpec("C4");
        Json res = SimulationEngine::run(cfg, {p});
        check(res["summary"].getInt("failed") == 1 && res["summary"].getInt("completed") == 0,
              "Integrated rejects job exceeding total memory");
    }
}

} // namespace

int main() {
    testCpuScheduling();
    testMemoryManagement();
    testDiskScheduling();
    testSynchronization();
    testIntegratedEngine();

    std::cout << "========================================\n";
    std::cout << "Smart Data Center OS Simulator Test Suite\n";
    std::cout << "Passed: " << gPassed << " | Failed: " << gFailed << "\n";
    std::cout << "========================================\n";
    return gFailed == 0 ? 0 : 1;
}

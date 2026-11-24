# Algorithm & Simulation Engine Reference

This document details how each Operating System module in `SmartDataCenterOS` is implemented in C++.

## 1. CPU Scheduling (`src/cpu/CpuScheduler.cpp`, `src/cpu/CpuSimulator.cpp`)
- **Strategy Pattern**: `CpuScheduler` defines `compareKey(a, b)`, `isPreemptive()`, and `timeQuantum()`. Ties are broken FIFO by ready-queue entry order (`readySeq`), then by PID.
- **Algorithms**:
  - `FCFS`: FIFO order (`compareKey = 0`).
  - `SJF` / `SRTF`: compares remaining time of the current CPU burst; `SRTF` sets `isPreemptive() = true`.
  - `PRIORITY` / `PRIORITY_P`: compares numeric priority according to `PriorityOrder` (`LowerIsHigher` or `HigherIsHigher`).
  - `RR`: FIFO queue with configurable quantum `q >= 1`; on expiry, the running job moves to the tail of the ready queue (unless the queue is empty, in which case it continues without an unnecessary context switch).
- **Metrics**: Waiting time, Turnaround time (`completion - arrival`), Response time (`firstStart - arrival`), CPU utilization (`busy / makespan * 100`), Throughput (`completed / makespan`), and Context Switches.

## 2. Memory Management (`src/memory/MemoryManager.cpp`, `src/memory/MemorySimulator.cpp`)
- **Dynamic Partitioning**: Starts as one contiguous free block `[0, total)`. Allocations split the chosen hole; releases mark the block free and immediately **coalesce** adjacent free holes.
- **Fixed Partitioning**: Configured from user-specified partition sizes; tracks **internal fragmentation** (`reserved - requested`).
- **Placement Strategies**:
  - `FirstFit`: first free block with `size >= request`.
  - `BestFit`: smallest free block with `size >= request`.
  - `WorstFit`: largest free block with `size >= request`.
  - `NextFit`: circular search starting from the last allocation's cursor.
- **External Fragmentation**: Computed as `totalFree - largestFreeHole`; allocation failures when `totalFree >= request` are explicitly flagged as external-fragmentation failures.

## 3. Disk Scheduling (`src/disk/DiskScheduler.cpp`, `src/disk/DiskSimulator.cpp`)
- Incremental `selectNext(pending, head, dir, cylinders, moves)` interface powers both the standalone Disk Lab and the dynamic disk queue in the integrated engine.
- **Algorithms**: `FCFS`, `SSTF`, `SCAN` (sweeps to edge `0` or `N-1` before reversing), `CSCAN` (sweeps to edge, jumps to opposite edge without servicing), `LOOK` (reverses at last request), `CLOOK` (jumps directly to opposite extreme request).

## 4. Process Synchronization (`src/synchronization/`)
- **Real OS Threads (`Threading.cpp`, `ConcurrencyDemo.cpp`)**: Uses `std::thread`/`std::mutex`/`std::condition_variable` on POSIX/MSVC and native Win32 `_beginthreadex`/`CRITICAL_SECTION`/`CreateSemaphore` on MinGW.org win32-thread builds. Records lock-free microsecond traces (`std::atomic` index) so tracing never artificially serializes threads.
- **Discrete Resource Manager (`ResourceManager.cpp`)**: Models mutexes (capacity 1) and counting semaphores (capacity `K`) with FIFO wait queues and atomic handoff on release.

## 5. Integrated Discrete-Event Simulation Engine (`src/simulation/SimulationEngine.cpp`)
At each discrete tick `t`, the engine executes in deterministic phase order:
1. Process arrivals (`NEW`) & capacity check.
2. Disk I/O completion (`WAITING(IO) → READY` or `TERMINATED`).
3. CPU burst completion (releases burst locks, wakes waiters, advances to next burst or `TERMINATED` + releases memory) or RR quantum expiry.
4. Memory admission (`NEW → READY` or `WAITING(IO)` when contiguous space is available).
5. Disk request dispatch via configured `DiskScheduler`.
6. CPU preemption & dispatch (acquiring required burst resources in sorted order; blocking in `WAITING(Resource)` if contended).
7. Snapshot capture for timeline playback & live charts, followed by 1-tick execution advance.

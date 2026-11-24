# Smart Data Center OS Simulator — Interactive Operating System Resource Management Lab

A full-stack, modular Operating Systems simulation lab powered by a **C++14 simulation engine** and a modern dark-mode **infrastructure monitoring dashboard**. It simulates how an Operating System manages processes, CPU scheduling, main memory, disk I/O, and shared-resource synchronization in a data-center environment.

All OS algorithms and concurrency primitives are implemented in C++ (zero OS algorithms rewritten in JavaScript).

---

## Features

1. **Integrated Discrete-Event OS Engine**
   - Full 5-state process lifecycle: `NEW → READY → RUNNING → WAITING → READY → RUNNING → TERMINATED`.
   - Enforces real OS invariants: a process cannot run without allocated memory, cannot hold the CPU while waiting for disk I/O or a lock, releases memory and locks on completion, and accounts for CPU idle time and arrival times.
   - Interactive playback controls: **Start, Pause, Resume, Next Step, Reset, Timeline Scrubber, Speed Control, Export JSON**.
   - Live OS Event Log and real-time Chart.js utilization telemetry.
2. **Process & Job Management**
   - Create, edit, delete, batch-generate, and **CSV import/export** jobs.
   - Supports simple fields (arrival, CPU burst, priority, memory, disk cylinders, shared resources) or explicit multi-phase burst sequences (e.g., `C2 I2@98 C3[DB_LOCK]`).
   - Color-coded PCB table with state filter pills.
3. **CPU Scheduling Module (6 Algorithms)**
   - **FCFS**, **SJF (Non-Preemptive)**, **SRTF (Preemptive)**, **Priority (Non-Preemptive)**, **Preemptive Priority**, and **Round Robin** (configurable quantum & priority direction).
   - Interactive hoverable Gantt Chart + **Step-by-Step Algorithm Walkthrough** showing ready queue, remaining burst, and context switches at every decision point.
4. **Memory Management Module (4 Placement Strategies)**
   - **First Fit**, **Best Fit**, **Worst Fit**, and **Next Fit** across both **Dynamic Partitioning** (splitting & adjacent free-block coalescing) and **Fixed Partitioning**.
   - Step-by-step Visual Memory Map showing allocated blocks, free holes, external/internal fragmentation, and allocation failures.
5. **Disk Scheduling Module (6 Algorithms)**
   - **FCFS**, **SSTF**, **SCAN**, **C-SCAN**, **LOOK**, and **C-LOOK**.
   - Animated HTML5 Canvas disk-head movement visualizer with replay and single-step controls.
6. **Synchronization & Real Multithreading Lab**
   - Spawns **actual OS threads** in C++ (`std::thread`/`std::mutex` on POSIX/MSVC toolchains; Win32 `_beginthreadex`/`CRITICAL_SECTION`/`CreateSemaphore` on MinGW.org win32-thread builds) to demonstrate:
     - **Race Condition (Unsynchronized)** — exposes real lost updates.
     - **Mutex Protected Critical Section** — guarantees 0 lost updates & mutual exclusion.
     - **Counting Semaphore Pool** — bounds concurrent threads to `permits`.
     - **Producer-Consumer Bounded Buffer** — verified checksum with zero overflow/underflow.
   - Side-by-side Race vs. Mutex comparison + microsecond thread interleaving trace.
7. **Algorithm Comparison Lab**
   - Runs all 6 CPU, 4 Memory, or 6 Disk algorithms independently in C++ on the identical workload, renders Chart.js bar charts, highlights best metrics, and explains tradeoffs.
8. **6 Built-In Realistic Sample Workloads**
   - Small (5 jobs), CPU-Intensive, Memory-Intensive (Fragmentation Lab), Disk-Intensive (classic Silberschatz cylinders), Synchronization & Lock Contention, and Mixed Data Center Production.

---

## Architecture

```
SmartDataCenterOS/
├── include/
│   ├── core/             # Json.h, Types.h
│   ├── process/          # Process.h, ProcessControlBlock.h
│   ├── cpu/              # CpuScheduler.h, CpuSimulator.h
│   ├── memory/           # MemoryManager.h, MemorySimulator.h
│   ├── disk/             # DiskScheduler.h, DiskSimulator.h
│   ├── synchronization/  # Threading.h, ConcurrencyDemo.h, ResourceManager.h
│   ├── analytics/        # Metrics.h
│   └── simulation/       # SimulationEngine.h
├── src/                  # Modular C++ implementations + main.cpp JSON CLI
├── tests/                # test_all.cpp (82 unit & integration assertions)
├── server/               # server.js (zero-dependency Node.js HTTP <-> C++ bridge)
├── frontend/
│   ├── index.html
│   ├── css/styles.css
│   ├── js/app.js
│   └── vendor/chart.umd.js
├── scripts/
│   ├── build.ps1         # Compiles dc_sim.exe and dc_tests.exe
│   └── start.ps1         # Builds if needed and starts http://127.0.0.1:8080
├── docs/
│   └── ALGORITHMS.md     # Detailed algorithm & engine documentation
└── README.md
```

**Data Flow:**
`Browser Dashboard (HTML5/CSS3/JS/Chart.js)` ⇄ `HTTP JSON (/api/*)` ⇄ `Node.js Bridge (server.js)` ⇄ `stdin/stdout JSON` ⇄ `Compiled C++ Engine (bin/dc_sim.exe)`

---

## Prerequisites

- **C++ Compiler**: `g++` (MinGW / MinGW-w64 / GCC / Clang supporting C++14+)
- **Node.js**: v16+ (zero npm packages required — uses only built-in `http`, `fs`, `path`, `child_process`)

---

## How to Build & Run (Windows PowerShell)

```powershell
cd "c:\Users\adity\Desktop\os project\SmartDataCenterOS"

# 1. Build the C++ engine and test suite
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1

# 2. Run the 82-assertion C++ unit & integration test suite
.\bin\dc_tests.exe

# 3. Start the web server (opens on http://127.0.0.1:8080)
powershell -ExecutionPolicy Bypass -File .\scripts\start.ps1
```

Then open **http://127.0.0.1:8080** in your browser.

---

## Testing & Verification

The C++ test suite (`tests/test_all.cpp`) validates all modules against hand-computed OS textbook examples:
- **CPU Scheduling**: FCFS (`8.75` avg wait), SJF (`7.75`), SRTF (`6.50`), Priority NP/Preemptive, Round Robin (`q=4`), empty workload, single process with idle gap.
- **Memory Management**: Classic Silberschatz fixed partitions `[100, 500, 200, 300, 600]` with requests `[212, 417, 112, 426]`, dynamic coalescing, external fragmentation detection, Next Fit wrap-around.
- **Disk Scheduling**: Classic queue `[98, 183, 37, 122, 14, 124, 65, 67]` from head `53` — FCFS (`640`), SSTF (`236`), SCAN (`331`), C-SCAN (`382`), LOOK (`299`), edge-cylinder and duplicate-request tests.
- **Synchronization**: Real multi-threaded Mutex (0 lost updates, max 1 in CS), Counting Semaphore (`maxConcurrent <= permits`), Producer-Consumer checksum & bounds, discrete `ResourceManager` handoff invariants.
- **Integrated Engine**: Runs all 6 sample workloads and verifies per-tick OS invariants (at most 1 running process, never running without memory, 100% memory released at completion) plus oversized-memory rejection.

---

## Screenshots

*(Add screenshots of Overview Telemetry, Gantt Chart, Memory Map, Animated Disk Track, Real-Thread Synchronization Lab, and Comparison Lab here after capturing during your demo.)*

---

## Limitations & Future Improvements

- **Single-Core CPU**: The integrated engine models a single CPU core and a single disk arm; extending to multi-core SMP scheduling and RAID disk arrays is a natural next step.
- **Virtual Memory / Paging**: Memory management models contiguous dynamic and fixed partitioning; adding demand paging with LRU/Optimal/Clock page replacement would further expand the lab.
- **Deadlock Avoidance**: The integrated engine prevents circular wait via sorted lock ordering; adding an interactive Banker's Algorithm safety-check lab would complement the synchronization module.

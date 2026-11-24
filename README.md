# DataCenterOS — Virtual Infrastructure Management & OS Simulation Platform

> **Subtitle:** Enterprise Virtual Infrastructure Management, Discrete-Event OS Simulation Core & Resource Laboratory.

An enterprise-grade, full-stack Operating Systems platform powered by a high-performance **C++14 simulation kernel** and a modern dark-mode **infrastructure operations dashboard**. It models and visualizes how a modern data center operating system manages compute fleets, process scheduling, dynamic memory, persistent block storage, concurrency locks, and distributed failure states.

All OS dispatch algorithms, placement strategies, synchronization primitives, and graph-theoretic deadlock analyzers are implemented natively in C++.

---

## 🚀 12 Integrated Product Modules

### 1. Infrastructure Overview Dashboard
- **Fleet-Wide Telemetry:** Live aggregated compute metrics across virtual servers, CPU cores, physical memory, and storage volumes.
- **Health Alert Banner:** Real-time cluster state detection (`OPTIMAL`, `DEGRADED`, `CRITICAL`) linked to active node outages and deadlock incidents.
- **Integrated Discrete-Event Timeline:** 5-state process lifecycle (`NEW → READY → RUNNING → WAITING → TERMINATED`) with real-time Gantt intervals and address space allocation maps.

### 2. Virtual Server Fleet Management
- **Provisioning & Lifecycle:** Deploy virtual instances across availability zones with custom CPU cores, RAM buffers, and block storage quotas.
- **Node Operations:** Dynamic `Start`, `Stop`, `Restart`, `Simulate Failure / Crash`, and `Health Recovery`.
- **Fault Simulation:** Trigger node-level outages that immediately open operational incidents and test failover resilience.

### 3. Process & Job Control Center
- **Process Control Blocks (PCB):** Detailed process tracking including arrival windows, burst sequences, priority classes, memory requirements, and server affinities.
- **Flexible Workload Definition:** Support for simple job configurations or multi-stage burst specs (e.g. `C2 I2@98 C3[DB_LOCK]`).
- **Batch Operations & Import/Export:** One-click synthetic workload generation, CSV export, and CSV file parser.

### 4. CPU Monitoring & Scheduling Laboratory
- **6 Native Scheduling Algorithms:** First-Come-First-Serve (FCFS), Shortest Job First (SJF), Shortest Remaining Time First (SRTF Preemptive), Priority (Non-Preemptive), Priority (Preemptive), and Round Robin (RR).
- **Interactive Execution Timeline:** Sub-second Gantt chart with interval inspectors.
- **Decision Walkthrough:** Step-by-step debugger tracking ready queues, remaining quantum slices, and context switch points.

### 5. Memory Management Center
- **4 Partition Placement Strategies:** First Fit, Best Fit, Worst Fit, and Next Fit.
- **Dual Partitioning Modes:** Contiguous dynamic partitioning with adjacent hole coalescing and fixed-partitioning pools.
- **Fragmentation Diagnostics:** Real-time metrics for external fragmentation, internal fragmentation, and allocation rejection rates.

### 6. Storage & Disk I/O Management
- **6 Arm Movement Algorithms:** FCFS, Shortest Seek Time First (SSTF), SCAN (Elevator), C-SCAN, LOOK, and C-LOOK.
- **Animated Platter Simulation:** HTML5 Canvas visualizer rendering exact cylinder head paths, seek distances, and service sequences.
- **Workload Integration:** Feed disk I/O requests directly from running multi-stage batch processes.

### 7. Synchronization & Real Multithreading Lab
- **Native OS Thread Execution:** Spawns actual threads on the host kernel (POSIX threads or Win32 kernel primitives) to benchmark:
  - **Race Condition Exposure:** Proves data corruption and lost updates under unsynchronized concurrent access.
  - **Mutex Protected Critical Sections:** Guarantees absolute mutual exclusion and 0 lost updates.
  - **Counting Semaphore Pools:** Restricts concurrent permits to available worker limits.
  - **Producer-Consumer Bounded Buffers:** Validates checksum bounds under producer/consumer thread contention.
- **Side-by-Side Verification:** Dual execution comparison with microsecond thread interleaving event traces.

### 8. Deadlock Detection & Resource Allocation Graph (RAG)
- **Wait-For Graph Cycle Detection:** Depth-First Search (DFS) graph cycle detector analyzing active process-resource dependencies.
- **Visual RAG Canvas:** Interactive graph rendering process nodes (circles), resource nodes (squares), assignment edges, request edges, and highlighted deadlock cycles.
- **Curated Scenarios & Recovery:** Includes Classic 2-Process Deadlock, 3-Way Pipeline Lockup, Dining Philosophers, and Safe Hierarchical Allocation with victim termination actions.

### 9. Scenario Lab & Stress Testing
- **Pre-Packaged Production Scenarios:**
  - *Extreme Compute Spike (CPU Saturation)*
  - *Memory Fragmentation & Exhaustion*
  - *Disk I/O Bottleneck & Head Thrashing*
  - *Multi-Process Deadlock Chain*
  - *Virtual Server Crash & Failover*
  - *Balanced Enterprise Production Load*

### 10. System Monitoring & Incident Management
- **Operational Issue Tracking:** Categorized incidents (`Critical`, `Warning`, `Info`) with node targets and timestamps.
- **Manual & Automated Alerts:** Log custom infrastructure incidents or receive automatic alerts triggered by simulated hardware faults.
- **Resolution Workflow:** Single-click incident mitigation logging root causes and resolutions.

### 11. Benchmarking Analytics
- **Empirical Multi-Algorithm Comparison:** Run all CPU, Memory, or Disk algorithms simultaneously on the identical workload.
- **Automated Winner Highlighting:** Highlights mathematically optimal metrics (lowest wait, turnaround, seek, or fragmentation).
- **Data Export:** Export complete comparison matrices as CSV or JSON.

### 12. Centralized System Event Logs
- **Comprehensive Audit Trail:** Real-time log capture across kernel dispatches, server provisioning, hardware alerts, and user operations.
- **Search & Filtering:** Multi-field filtering by category (`SYSTEM`, `SERVER`, `SIMULATION`, `DEADLOCK`, `INCIDENT`) and severity.
- **CSV Data Export:** One-click audit trail download.

---

## 🏛 Technical Architecture

```
DataCenterOS/
├── include/
│   ├── core/             # Json.h, Types.h
│   ├── process/          # Process.h, ProcessControlBlock.h
│   ├── cpu/              # CpuScheduler.h, CpuSimulator.h
│   ├── memory/           # MemoryManager.h, MemorySimulator.h
│   ├── disk/             # DiskScheduler.h, DiskSimulator.h
│   ├── synchronization/  # Threading.h, ConcurrencyDemo.h, ResourceManager.h
│   ├── deadlock/         # DeadlockDetector.h
│   ├── analytics/        # Metrics.h
│   └── simulation/       # SimulationEngine.h
├── src/                  # C++14 Engine implementations + main.cpp JSON CLI
├── tests/                # test_all.cpp (85+ comprehensive unit & integration tests)
├── server/               # server.js (Zero-dependency Node.js enterprise bridge & state persistence)
├── frontend/
│   ├── index.html        # 12 integrated module dashboard views
│   ├── css/styles.css    # Enterprise dark-mode design system
│   ├── js/app.js         # Single-page application controller
│   └── vendor/           # chart.umd.js (local Chart.js bundle)
├── data/                 # state.json (JSON state persistence for servers & incidents)
├── scripts/
│   ├── build.ps1         # Compiles dc_sim.exe and dc_tests.exe
│   └── start.ps1         # Starts http://127.0.0.1:8080
├── Dockerfile            # Multi-stage production container build (gcc:12 -> node:20-slim)
├── render.yaml           # One-click Render Blueprint specification
└── README.md
```

---

## ⚙️ Quickstart (Local Windows Execution)

### Prerequisites
- **C++ Compiler:** `g++` (MinGW / MinGW-w64 / GCC supporting C++14+)
- **Node.js:** v16+ (Zero external npm dependencies needed)

### Build & Run

```powershell
cd "c:\Users\adity\Desktop\os project\SmartDataCenterOS"

# 1. Compile C++ Simulation Kernel & Test Suite
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1

# 2. Run Comprehensive Engine Test Suite
.\bin\dc_tests.exe

# 3. Launch Platform Server
powershell -ExecutionPolicy Bypass -File .\scripts\start.ps1
```

Access the dashboard at **http://127.0.0.1:8080** in any modern web browser.

---

## 🐳 Docker & Cloud Deployment

The repository includes a multi-stage `Dockerfile` and `render.yaml` Blueprint for zero-configuration cloud hosting.

```bash
# Build Docker image
docker build -t datacenteros .

# Run container
docker run -p 8080:8080 datacenteros
```


# Smart Data Center OS Simulator

A full-stack Operating Systems simulator built with a **C++14 simulation engine** and a modern web-based dashboard. Simulates and visualizes process management, CPU scheduling, memory allocation, disk scheduling, and synchronization.

🌐 **Live Demo:** [Smart Data Center OS Simulator](https://smart-datacenter-os-simulator.onrender.com/)

## Features

- **Integrated OS Simulation:** Discrete-event simulation with process lifecycle management, resource allocation, and OS invariants.
- **CPU Scheduling:** FCFS, SJF, SRTF, Priority, Preemptive Priority, and Round Robin.
- **Memory Management:** First Fit, Best Fit, Worst Fit, and Next Fit with fixed and dynamic partitioning.
- **Disk Scheduling:** FCFS, SSTF, SCAN, C-SCAN, LOOK, and C-LOOK.
- **Synchronization:** Real multithreading, race conditions, mutexes, counting semaphores, and Producer-Consumer.
- **Process Management:** Create, edit, delete, batch-generate, and CSV import/export workloads.
- **Algorithm Comparison:** Compare CPU, memory, and disk algorithms using performance metrics and charts.
- **Interactive Dashboard:** Gantt charts, memory maps, disk visualization, telemetry, event logs, and simulation controls.

## Tech Stack

- **Core Engine:** C++14
- **Backend:** Node.js
- **Frontend:** HTML5, CSS3, JavaScript
- **Visualization:** Chart.js, HTML5 Canvas
- **Communication:** HTTP, JSON

## Architecture

```text
Frontend Dashboard
       ↕
HTTP / JSON API
       ↕
Node.js Bridge
       ↕
C++14 Simulation Engine
       ↕
CPU | Memory | Disk | Synchronization
```

## Project Structure

```text
SmartDataCenterOS/
├── include/          # Modular C++ headers
├── src/              # Simulation engine
├── tests/            # Unit and integration tests
├── server/           # Node.js backend bridge
├── frontend/          # Dashboard UI
├── scripts/           # Build and startup scripts
├── docs/              # Algorithm documentation
└── README.md
```

## Build & Run

**Prerequisites:** C++14 compiler, Node.js v16+, Windows PowerShell.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
.\bin\dc_tests.exe
powershell -ExecutionPolicy Bypass -File .\scripts\start.ps1
```

Open `http://127.0.0.1:8080` in your browser.

## Testing

The C++ test suite includes **82 unit and integration assertions** covering:

- CPU scheduling algorithms
- Memory allocation and fragmentation
- Disk scheduling
- Synchronization and concurrency
- Integrated simulation invariants

## Limitations & Future Scope

- Single-core CPU simulation
- Single disk-arm model
- Contiguous memory allocation
- Future scope: multicore scheduling, virtual memory, paging, and Banker’s Algorithm.

## Author

**Aditya Raj**
B.Tech – Computer Science & Engineering

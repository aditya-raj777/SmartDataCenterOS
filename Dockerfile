# Multi-stage Dockerfile for Smart Data Center OS Simulator (C++14 Engine + Node.js Bridge)
FROM gcc:12-bookworm AS builder

WORKDIR /build
COPY include ./include
COPY src ./src
COPY tests ./tests

RUN mkdir -p /build/bin && \
    g++ -std=c++14 -O2 -Wall -Wextra -pthread -Iinclude \
      src/core/Json.cpp \
      src/core/Types.cpp \
      src/process/Process.cpp \
      src/analytics/Metrics.cpp \
      src/cpu/CpuScheduler.cpp \
      src/cpu/CpuSimulator.cpp \
      src/memory/MemoryManager.cpp \
      src/memory/MemorySimulator.cpp \
      src/disk/DiskScheduler.cpp \
      src/disk/DiskSimulator.cpp \
      src/synchronization/Threading.cpp \
      src/synchronization/ConcurrencyDemo.cpp \
      src/synchronization/ResourceManager.cpp \
      src/simulation/SimulationEngine.cpp \
      src/deadlock/DeadlockDetector.cpp \
      src/main.cpp -o /build/bin/dc_sim && \
    g++ -std=c++14 -O2 -Wall -Wextra -pthread -Iinclude \
      src/core/Json.cpp \
      src/core/Types.cpp \
      src/process/Process.cpp \
      src/analytics/Metrics.cpp \
      src/cpu/CpuScheduler.cpp \
      src/cpu/CpuSimulator.cpp \
      src/memory/MemoryManager.cpp \
      src/memory/MemorySimulator.cpp \
      src/disk/DiskScheduler.cpp \
      src/disk/DiskSimulator.cpp \
      src/synchronization/Threading.cpp \
      src/synchronization/ConcurrencyDemo.cpp \
      src/synchronization/ResourceManager.cpp \
      src/simulation/SimulationEngine.cpp \
      src/deadlock/DeadlockDetector.cpp \
      tests/test_all.cpp -o /build/bin/dc_tests && \
    /build/bin/dc_tests

FROM node:20-bookworm-slim

WORKDIR /app
COPY --from=builder /build/bin/dc_sim ./bin/dc_sim
COPY server ./server
COPY frontend ./frontend
COPY package.json ./package.json

ENV NODE_ENV=production
ENV HOST=0.0.0.0
ENV PORT=8080
EXPOSE 8080

CMD ["node", "server/server.js"]

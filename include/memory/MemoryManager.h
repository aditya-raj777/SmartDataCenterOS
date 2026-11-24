// Contiguous main-memory manager with pluggable placement strategies.
// Supports dynamic (variable) partitioning with splitting/coalescing and
// fixed partitioning (internal fragmentation).
#pragma once

#include "core/Json.h"
#include "core/Types.h"

#include <memory>
#include <string>
#include <vector>

namespace dc {

struct MemoryBlock {
    int start = 0;
    int size = 0;
    int pid = -1;  // -1 => free
    int used = 0;  // bytes actually requested by pid (fixed mode: <= size)
    bool isFree() const { return pid < 0; }
};

enum class MemoryMode { Dynamic, Fixed };

/// Placement policy: pick a free block able to hold `size`.
class PlacementStrategy {
public:
    virtual ~PlacementStrategy() = default;
    virtual std::string code() const = 0;
    virtual std::string name() const = 0;
    /// Returns index into `blocks` or -1. `cursor` is the address where the
    /// previous search ended (used by Next Fit; others ignore it).
    virtual int choose(const std::vector<MemoryBlock>& blocks, int size, int cursor) const = 0;
};

class FirstFit : public PlacementStrategy {
public:
    std::string code() const override { return "FIRST_FIT"; }
    std::string name() const override { return "First Fit"; }
    int choose(const std::vector<MemoryBlock>& blocks, int size, int cursor) const override;
};
class BestFit : public PlacementStrategy {
public:
    std::string code() const override { return "BEST_FIT"; }
    std::string name() const override { return "Best Fit"; }
    int choose(const std::vector<MemoryBlock>& blocks, int size, int cursor) const override;
};
class WorstFit : public PlacementStrategy {
public:
    std::string code() const override { return "WORST_FIT"; }
    std::string name() const override { return "Worst Fit"; }
    int choose(const std::vector<MemoryBlock>& blocks, int size, int cursor) const override;
};
class NextFit : public PlacementStrategy {
public:
    std::string code() const override { return "NEXT_FIT"; }
    std::string name() const override { return "Next Fit"; }
    int choose(const std::vector<MemoryBlock>& blocks, int size, int cursor) const override;
};

std::unique_ptr<PlacementStrategy> makePlacementStrategy(const std::string& code);
std::vector<std::string> allMemoryStrategies();
MemoryMode parseMemoryMode(const std::string& s);

struct AllocResult {
    bool ok = false;
    int start = -1;
    int blockSize = 0;
    std::string reason; // on failure
    bool externalFragmentation = false; // failed although total free >= request
};

class MemoryManager {
public:
    MemoryManager(int totalSize, MemoryMode mode, const std::vector<int>& partitions,
                  std::unique_ptr<PlacementStrategy> strategy);

    AllocResult allocate(int pid, int size);
    bool release(int pid);

    bool isAllocated(int pid) const;
    bool canEverFit(int size) const;

    int total() const { return total_; }
    int used() const;      // bytes held by processes (requested sizes)
    int reserved() const;  // bytes in allocated blocks (>= used in fixed mode)
    int freeMemory() const;
    int largestFree() const;
    int freeBlockCount() const;
    int externalFragmentation() const; // free bytes not in the largest hole
    int internalFragmentation() const; // fixed mode waste inside blocks
    double utilization() const;        // reserved / total * 100
    int failures() const { return failures_; }
    int releaseVersion() const { return releaseVersion_; }
    MemoryMode mode() const { return mode_; }
    const PlacementStrategy& strategy() const { return *strategy_; }
    const std::vector<MemoryBlock>& blocks() const { return blocks_; }

    Json toJson() const;

private:
    void coalesce();

    int total_;
    MemoryMode mode_;
    std::unique_ptr<PlacementStrategy> strategy_;
    std::vector<MemoryBlock> blocks_;
    int cursor_ = 0;
    int failures_ = 0;
    int releaseVersion_ = 0;
};

} // namespace dc

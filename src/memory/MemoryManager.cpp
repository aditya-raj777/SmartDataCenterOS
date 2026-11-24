#include "memory/MemoryManager.h"

#include <algorithm>

namespace dc {

// ------------------------------------------------------------------ strategies
int FirstFit::choose(const std::vector<MemoryBlock>& b, int size, int) const {
    for (int i = 0; i < static_cast<int>(b.size()); ++i)
        if (b[i].isFree() && b[i].size >= size) return i;
    return -1;
}

int BestFit::choose(const std::vector<MemoryBlock>& b, int size, int) const {
    int best = -1;
    for (int i = 0; i < static_cast<int>(b.size()); ++i)
        if (b[i].isFree() && b[i].size >= size && (best < 0 || b[i].size < b[best].size)) best = i;
    return best;
}

int WorstFit::choose(const std::vector<MemoryBlock>& b, int size, int) const {
    int best = -1;
    for (int i = 0; i < static_cast<int>(b.size()); ++i)
        if (b[i].isFree() && b[i].size >= size && (best < 0 || b[i].size > b[best].size)) best = i;
    return best;
}

int NextFit::choose(const std::vector<MemoryBlock>& b, int size, int cursor) const {
    const int n = static_cast<int>(b.size());
    // first block whose start >= cursor, then wrap around
    int first = 0;
    while (first < n && b[first].start + b[first].size <= cursor) ++first;
    for (int k = 0; k < n; ++k) {
        int i = (first + k) % n;
        if (b[i].isFree() && b[i].size >= size) return i;
    }
    return -1;
}

std::unique_ptr<PlacementStrategy> makePlacementStrategy(const std::string& codeIn) {
    std::string c = util::toUpper(util::trim(codeIn));
    for (auto& ch : c) if (ch == ' ' || ch == '-') ch = '_';
    if (c == "FIRST_FIT" || c == "FIRST" || c == "FF") return std::unique_ptr<PlacementStrategy>(new FirstFit());
    if (c == "BEST_FIT" || c == "BEST" || c == "BF") return std::unique_ptr<PlacementStrategy>(new BestFit());
    if (c == "WORST_FIT" || c == "WORST" || c == "WF") return std::unique_ptr<PlacementStrategy>(new WorstFit());
    if (c == "NEXT_FIT" || c == "NEXT" || c == "NF") return std::unique_ptr<PlacementStrategy>(new NextFit());
    throw SimError("Unknown memory allocation strategy '" + codeIn + "'");
}

std::vector<std::string> allMemoryStrategies() { return {"FIRST_FIT", "BEST_FIT", "WORST_FIT", "NEXT_FIT"}; }

MemoryMode parseMemoryMode(const std::string& s) {
    return util::toUpper(s) == "FIXED" ? MemoryMode::Fixed : MemoryMode::Dynamic;
}

// ------------------------------------------------------------------ manager
MemoryManager::MemoryManager(int totalSize, MemoryMode mode, const std::vector<int>& partitions,
                             std::unique_ptr<PlacementStrategy> strategy)
    : total_(totalSize), mode_(mode), strategy_(std::move(strategy)) {
    if (!strategy_) throw SimError("Memory manager needs a placement strategy");
    if (mode_ == MemoryMode::Fixed) {
        if (partitions.empty()) throw SimError("Fixed partitioning needs at least one partition size");
        int addr = 0;
        for (int p : partitions) {
            if (p <= 0) throw SimError("Partition sizes must be positive");
            MemoryBlock b;
            b.start = addr;
            b.size = p;
            blocks_.push_back(b);
            addr += p;
        }
        total_ = addr;
    } else {
        if (total_ <= 0) throw SimError("Total memory must be positive");
        MemoryBlock b;
        b.start = 0;
        b.size = total_;
        blocks_.push_back(b);
    }
}

bool MemoryManager::canEverFit(int size) const {
    if (mode_ == MemoryMode::Dynamic) return size <= total_;
    for (const auto& b : blocks_) if (b.size >= size) return true;
    return false;
}

AllocResult MemoryManager::allocate(int pid, int size) {
    AllocResult r;
    if (isAllocated(pid)) {
        r.reason = "process already holds memory";
        return r;
    }
    if (size <= 0) size = 0;
    int idx = strategy_->choose(blocks_, std::max(size, 1), cursor_);
    if (size == 0) {
        // zero-size jobs do not need a block; treat as trivially allocated
        r.ok = true;
        r.start = -1;
        return r;
    }
    if (idx < 0) {
        ++failures_;
        int freeMem = freeMemory();
        if (!canEverFit(size)) {
            r.reason = "request " + std::to_string(size) + " exceeds " +
                       (mode_ == MemoryMode::Fixed ? "the largest partition" : "total memory");
        } else if (freeMem >= size && mode_ == MemoryMode::Dynamic) {
            r.externalFragmentation = true;
            r.reason = "external fragmentation: " + std::to_string(freeMem) + " free but largest hole is " +
                       std::to_string(largestFree());
        } else {
            r.reason = "insufficient free memory (" + std::to_string(freeMem) + " free, need " + std::to_string(size) + ")";
        }
        return r;
    }
    MemoryBlock& blk = blocks_[idx];
    r.ok = true;
    r.start = blk.start;
    if (mode_ == MemoryMode::Dynamic && blk.size > size) {
        MemoryBlock rest;
        rest.start = blk.start + size;
        rest.size = blk.size - size;
        blk.size = size;
        blk.pid = pid;
        blk.used = size;
        blocks_.insert(blocks_.begin() + idx + 1, rest);
    } else {
        blk.pid = pid;
        blk.used = size;
    }
    r.blockSize = blocks_[idx].size;
    cursor_ = blocks_[idx].start;
    return r;
}

bool MemoryManager::release(int pid) {
    bool found = false;
    for (auto& b : blocks_) {
        if (b.pid == pid) {
            b.pid = -1;
            b.used = 0;
            found = true;
        }
    }
    if (found) {
        if (mode_ == MemoryMode::Dynamic) coalesce();
        ++releaseVersion_;
    }
    return found;
}

void MemoryManager::coalesce() {
    std::vector<MemoryBlock> merged;
    for (const auto& b : blocks_) {
        if (!merged.empty() && merged.back().isFree() && b.isFree()) merged.back().size += b.size;
        else merged.push_back(b);
    }
    blocks_.swap(merged);
}

bool MemoryManager::isAllocated(int pid) const {
    for (const auto& b : blocks_) if (b.pid == pid) return true;
    return false;
}

int MemoryManager::used() const {
    int u = 0;
    for (const auto& b : blocks_) if (!b.isFree()) u += b.used;
    return u;
}
int MemoryManager::reserved() const {
    int u = 0;
    for (const auto& b : blocks_) if (!b.isFree()) u += b.size;
    return u;
}
int MemoryManager::freeMemory() const { return total_ - reserved(); }
int MemoryManager::largestFree() const {
    int m = 0;
    for (const auto& b : blocks_) if (b.isFree()) m = std::max(m, b.size);
    return m;
}
int MemoryManager::freeBlockCount() const {
    int c = 0;
    for (const auto& b : blocks_) if (b.isFree()) ++c;
    return c;
}
int MemoryManager::externalFragmentation() const {
    int f = freeMemory();
    return f > 0 ? f - largestFree() : 0;
}
int MemoryManager::internalFragmentation() const { return reserved() - used(); }
double MemoryManager::utilization() const { return total_ > 0 ? 100.0 * reserved() / total_ : 0.0; }

Json MemoryManager::toJson() const {
    Json j = Json::object();
    j["total"] = total_;
    j["mode"] = mode_ == MemoryMode::Fixed ? "FIXED" : "DYNAMIC";
    j["strategy"] = strategy_->code();
    j["used"] = used();
    j["reserved"] = reserved();
    j["free"] = freeMemory();
    j["largestFree"] = largestFree();
    j["freeBlocks"] = freeBlockCount();
    j["externalFragmentation"] = externalFragmentation();
    j["internalFragmentation"] = internalFragmentation();
    j["utilization"] = util::round2(utilization());
    j["failures"] = failures_;
    Json arr = Json::array();
    for (const auto& b : blocks_) {
        Json x = Json::object();
        x["start"] = b.start;
        x["size"] = b.size;
        x["pid"] = b.pid;
        x["used"] = b.used;
        arr.push(x);
    }
    j["blocks"] = arr;
    return j;
}

} // namespace dc

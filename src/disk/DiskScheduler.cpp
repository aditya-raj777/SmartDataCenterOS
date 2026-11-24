#include "disk/DiskScheduler.h"

#include <cstdlib>

namespace dc {

void DiskScheduler::moveTo(int& head, int target, std::vector<SeekMove>& moves, bool jump, bool edge) {
    SeekMove m;
    m.from = head;
    m.to = target;
    m.jump = jump;
    m.edge = edge;
    moves.push_back(m);
    head = target;
}

int DiskScheduler::nearestInDirection(const std::vector<DiskRequest>& p, int head, Direction dir) {
    int best = -1;
    for (int i = 0; i < static_cast<int>(p.size()); ++i) {
        int c = p[i].cylinder;
        bool ok = dir == Direction::Up ? c >= head : c <= head;
        if (!ok) continue;
        if (best < 0) { best = i; continue; }
        int d = std::abs(c - head), bd = std::abs(p[best].cylinder - head);
        if (d < bd || (d == bd && p[i].seq < p[best].seq)) best = i;
    }
    return best;
}

int FcfsDisk::selectNext(const std::vector<DiskRequest>& p, int& head, Direction& dir, int, std::vector<SeekMove>& moves) const {
    if (p.empty()) return -1;
    int best = 0;
    for (int i = 1; i < static_cast<int>(p.size()); ++i) if (p[i].seq < p[best].seq) best = i;
    int target = p[best].cylinder;
    if (target != head) dir = target > head ? Direction::Up : Direction::Down;
    moveTo(head, target, moves);
    return best;
}

int SstfDisk::selectNext(const std::vector<DiskRequest>& p, int& head, Direction& dir, int, std::vector<SeekMove>& moves) const {
    if (p.empty()) return -1;
    int best = 0;
    for (int i = 1; i < static_cast<int>(p.size()); ++i) {
        int d = std::abs(p[i].cylinder - head), bd = std::abs(p[best].cylinder - head);
        if (d < bd || (d == bd && p[i].seq < p[best].seq)) best = i;
    }
    int target = p[best].cylinder;
    if (target != head) dir = target > head ? Direction::Up : Direction::Down;
    moveTo(head, target, moves);
    return best;
}

int ScanDisk::selectNext(const std::vector<DiskRequest>& p, int& head, Direction& dir, int cylinders, std::vector<SeekMove>& moves) const {
    if (p.empty()) return -1;
    int idx = nearestInDirection(p, head, dir);
    if (idx < 0) {
        // nothing left in this direction: sweep to the edge, then reverse
        int edge = dir == Direction::Up ? cylinders - 1 : 0;
        if (head != edge) moveTo(head, edge, moves, false, true);
        dir = dir == Direction::Up ? Direction::Down : Direction::Up;
        idx = nearestInDirection(p, head, dir);
    }
    moveTo(head, p[idx].cylinder, moves);
    return idx;
}

int CScanDisk::selectNext(const std::vector<DiskRequest>& p, int& head, Direction& dir, int cylinders, std::vector<SeekMove>& moves) const {
    if (p.empty()) return -1;
    int idx = nearestInDirection(p, head, dir);
    if (idx < 0) {
        int edge = dir == Direction::Up ? cylinders - 1 : 0;
        int other = dir == Direction::Up ? 0 : cylinders - 1;
        if (head != edge) moveTo(head, edge, moves, false, true);
        moveTo(head, other, moves, true, true); // return sweep, services nothing
        idx = nearestInDirection(p, head, dir);
    }
    moveTo(head, p[idx].cylinder, moves);
    return idx;
}

int LookDisk::selectNext(const std::vector<DiskRequest>& p, int& head, Direction& dir, int, std::vector<SeekMove>& moves) const {
    if (p.empty()) return -1;
    int idx = nearestInDirection(p, head, dir);
    if (idx < 0) {
        dir = dir == Direction::Up ? Direction::Down : Direction::Up;
        idx = nearestInDirection(p, head, dir);
    }
    moveTo(head, p[idx].cylinder, moves);
    return idx;
}

int CLookDisk::selectNext(const std::vector<DiskRequest>& p, int& head, Direction& dir, int, std::vector<SeekMove>& moves) const {
    if (p.empty()) return -1;
    int idx = nearestInDirection(p, head, dir);
    if (idx < 0) {
        // jump to the farthest pending request at the other end
        int best = 0;
        for (int i = 1; i < static_cast<int>(p.size()); ++i) {
            bool better = dir == Direction::Up ? p[i].cylinder < p[best].cylinder : p[i].cylinder > p[best].cylinder;
            if (better || (p[i].cylinder == p[best].cylinder && p[i].seq < p[best].seq)) best = i;
        }
        moveTo(head, p[best].cylinder, moves, true, false);
        return best;
    }
    moveTo(head, p[idx].cylinder, moves);
    return idx;
}

std::unique_ptr<DiskScheduler> makeDiskScheduler(const std::string& codeIn) {
    std::string c = util::toUpper(util::trim(codeIn));
    for (auto it = c.begin(); it != c.end();) { if (*it == '-' || *it == '_' || *it == ' ') it = c.erase(it); else ++it; }
    if (c == "FCFS" || c == "FIFO") return std::unique_ptr<DiskScheduler>(new FcfsDisk());
    if (c == "SSTF") return std::unique_ptr<DiskScheduler>(new SstfDisk());
    if (c == "SCAN") return std::unique_ptr<DiskScheduler>(new ScanDisk());
    if (c == "CSCAN") return std::unique_ptr<DiskScheduler>(new CScanDisk());
    if (c == "LOOK") return std::unique_ptr<DiskScheduler>(new LookDisk());
    if (c == "CLOOK") return std::unique_ptr<DiskScheduler>(new CLookDisk());
    throw SimError("Unknown disk scheduling algorithm '" + codeIn + "'");
}

std::vector<std::string> allDiskAlgorithms() { return {"FCFS", "SSTF", "SCAN", "CSCAN", "LOOK", "CLOOK"}; }

Direction parseDirection(const std::string& s) {
    std::string u = util::toUpper(s);
    if (u == "DOWN" || u == "LEFT" || u == "DECREASING" || u == "LOW") return Direction::Down;
    return Direction::Up;
}

const char* toString(Direction d) { return d == Direction::Up ? "UP" : "DOWN"; }

int totalSeek(const std::vector<SeekMove>& moves, bool includeJumps) {
    int t = 0;
    for (const auto& m : moves) if (includeJumps || !m.jump) t += m.distance();
    return t;
}

} // namespace dc

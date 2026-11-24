// Disk scheduling policies. Every policy is implemented incrementally ("given
// the head position, direction and pending queue, which request is next and
// what path does the head travel?") so the very same code drives both the
// stand-alone disk lab and the dynamic disk queue of the integrated engine.
#pragma once

#include "core/Json.h"
#include "core/Types.h"

#include <memory>
#include <string>
#include <vector>

namespace dc {

enum class Direction { Up, Down }; // Up = toward higher cylinder numbers

struct DiskRequest {
    int id = 0;
    int cylinder = 0;
    int pid = -1;
    long seq = 0; // arrival order into the disk queue
};

struct SeekMove {
    int from = 0;
    int to = 0;
    bool jump = false; // C-SCAN / C-LOOK return sweep
    bool edge = false; // travel to a disk edge without serving a request
    int distance() const { return from > to ? from - to : to - from; }
};

class DiskScheduler {
public:
    virtual ~DiskScheduler() = default;
    virtual std::string code() const = 0;
    virtual std::string name() const = 0;
    /// Choose the next request; append head movements (ending at the chosen
    /// request's cylinder) to `moves`, update `head` and `dir`. Returns index
    /// into `pending` or -1 if empty.
    virtual int selectNext(const std::vector<DiskRequest>& pending, int& head, Direction& dir, int cylinders,
                           std::vector<SeekMove>& moves) const = 0;
protected:
    static void moveTo(int& head, int target, std::vector<SeekMove>& moves, bool jump = false, bool edge = false);
    static int nearestInDirection(const std::vector<DiskRequest>& p, int head, Direction dir);
};

class FcfsDisk : public DiskScheduler {
public:
    std::string code() const override { return "FCFS"; }
    std::string name() const override { return "First Come First Serve"; }
    int selectNext(const std::vector<DiskRequest>&, int&, Direction&, int, std::vector<SeekMove>&) const override;
};
class SstfDisk : public DiskScheduler {
public:
    std::string code() const override { return "SSTF"; }
    std::string name() const override { return "Shortest Seek Time First"; }
    int selectNext(const std::vector<DiskRequest>&, int&, Direction&, int, std::vector<SeekMove>&) const override;
};
class ScanDisk : public DiskScheduler {
public:
    std::string code() const override { return "SCAN"; }
    std::string name() const override { return "SCAN (Elevator)"; }
    int selectNext(const std::vector<DiskRequest>&, int&, Direction&, int, std::vector<SeekMove>&) const override;
};
class CScanDisk : public DiskScheduler {
public:
    std::string code() const override { return "CSCAN"; }
    std::string name() const override { return "Circular SCAN"; }
    int selectNext(const std::vector<DiskRequest>&, int&, Direction&, int, std::vector<SeekMove>&) const override;
};
class LookDisk : public DiskScheduler {
public:
    std::string code() const override { return "LOOK"; }
    std::string name() const override { return "LOOK"; }
    int selectNext(const std::vector<DiskRequest>&, int&, Direction&, int, std::vector<SeekMove>&) const override;
};
class CLookDisk : public DiskScheduler {
public:
    std::string code() const override { return "CLOOK"; }
    std::string name() const override { return "Circular LOOK"; }
    int selectNext(const std::vector<DiskRequest>&, int&, Direction&, int, std::vector<SeekMove>&) const override;
};

std::unique_ptr<DiskScheduler> makeDiskScheduler(const std::string& code);
std::vector<std::string> allDiskAlgorithms();
Direction parseDirection(const std::string& s);
const char* toString(Direction d);

/// Total distance of a move list (optionally excluding circular return jumps).
int totalSeek(const std::vector<SeekMove>& moves, bool includeJumps);

} // namespace dc

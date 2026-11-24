// Shared-resource manager used by the integrated discrete-event engine.
// Each resource is a mutex (capacity 1) or a counting semaphore (capacity n)
// with a FIFO wait queue; release hands the resource directly to the next waiter.
#pragma once

#include "core/Json.h"

#include <deque>
#include <map>
#include <string>
#include <vector>

namespace dc {

struct SharedResource {
    std::string name;
    std::string kind = "mutex"; // mutex | semaphore
    int capacity = 1;
    std::vector<int> holders;
    std::deque<int> waiters;
    long acquisitions = 0;
    long conflicts = 0; // requests that had to wait
    long releases = 0;
    long handoffs = 0;
};

class ResourceManager {
public:
    void define(const std::string& name, const std::string& kind, int capacity);
    bool exists(const std::string& name) const { return res_.count(name) > 0; }

    /// Try to acquire; on failure the pid is appended to the wait queue.
    bool acquire(const std::string& name, int pid);
    /// Release; returns the pid that was granted the resource (or -1).
    int release(const std::string& name, int pid);
    bool holds(const std::string& name, int pid) const;
    void removeWaiter(int pid);

    long totalConflicts() const;
    long totalAcquisitions() const;
    int totalWaiting() const;
    bool invariantsHold() const; // holders <= capacity, no pid both holder & waiter
    const std::map<std::string, SharedResource>& all() const { return res_; }

    Json toJson() const;

private:
    SharedResource& get(const std::string& name);
    std::map<std::string, SharedResource> res_;
};

} // namespace dc

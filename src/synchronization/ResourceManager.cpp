#include "synchronization/ResourceManager.h"

#include "core/Types.h"

#include <algorithm>

namespace dc {

void ResourceManager::define(const std::string& name, const std::string& kind, int capacity) {
    if (name.empty()) throw SimError("Resource name cannot be empty");
    SharedResource& r = res_[name];
    r.name = name;
    std::string k = util::toUpper(kind);
    if (k == "SEMAPHORE") {
        r.kind = "semaphore";
        r.capacity = std::max(1, capacity);
    } else {
        r.kind = "mutex";
        r.capacity = 1;
    }
}

SharedResource& ResourceManager::get(const std::string& name) {
    auto it = res_.find(name);
    if (it == res_.end()) {
        define(name, "mutex", 1); // unknown resources default to a mutex
        it = res_.find(name);
    }
    return it->second;
}

bool ResourceManager::acquire(const std::string& name, int pid) {
    SharedResource& r = get(name);
    if (std::find(r.holders.begin(), r.holders.end(), pid) != r.holders.end()) return true;
    if (static_cast<int>(r.holders.size()) < r.capacity && r.waiters.empty()) {
        r.holders.push_back(pid);
        r.acquisitions++;
        return true;
    }
    if (std::find(r.waiters.begin(), r.waiters.end(), pid) == r.waiters.end()) {
        r.waiters.push_back(pid);
        r.conflicts++;
    }
    return false;
}

int ResourceManager::release(const std::string& name, int pid) {
    SharedResource& r = get(name);
    auto it = std::find(r.holders.begin(), r.holders.end(), pid);
    if (it == r.holders.end()) return -1;
    r.holders.erase(it);
    r.releases++;
    if (!r.waiters.empty() && static_cast<int>(r.holders.size()) < r.capacity) {
        int next = r.waiters.front();
        r.waiters.pop_front();
        r.holders.push_back(next);
        r.acquisitions++;
        r.handoffs++;
        return next;
    }
    return -1;
}

bool ResourceManager::holds(const std::string& name, int pid) const {
    auto it = res_.find(name);
    if (it == res_.end()) return false;
    return std::find(it->second.holders.begin(), it->second.holders.end(), pid) != it->second.holders.end();
}

void ResourceManager::removeWaiter(int pid) {
    for (auto& kv : res_) {
        auto& w = kv.second.waiters;
        w.erase(std::remove(w.begin(), w.end(), pid), w.end());
    }
}

long ResourceManager::totalConflicts() const {
    long c = 0;
    for (const auto& kv : res_) c += kv.second.conflicts;
    return c;
}
long ResourceManager::totalAcquisitions() const {
    long c = 0;
    for (const auto& kv : res_) c += kv.second.acquisitions;
    return c;
}
int ResourceManager::totalWaiting() const {
    int c = 0;
    for (const auto& kv : res_) c += static_cast<int>(kv.second.waiters.size());
    return c;
}

bool ResourceManager::invariantsHold() const {
    for (const auto& kv : res_) {
        const auto& r = kv.second;
        if (static_cast<int>(r.holders.size()) > r.capacity) return false;
        for (int w : r.waiters)
            if (std::find(r.holders.begin(), r.holders.end(), w) != r.holders.end()) return false;
    }
    return true;
}

Json ResourceManager::toJson() const {
    Json arr = Json::array();
    for (const auto& kv : res_) {
        const auto& r = kv.second;
        Json x = Json::object();
        x["name"] = r.name;
        x["kind"] = r.kind;
        x["capacity"] = r.capacity;
        Json h = Json::array();
        for (int p : r.holders) h.push(p);
        x["holders"] = h;
        Json w = Json::array();
        for (int p : r.waiters) w.push(p);
        x["waiters"] = w;
        x["locked"] = static_cast<int>(r.holders.size()) >= r.capacity;
        x["acquisitions"] = static_cast<long long>(r.acquisitions);
        x["conflicts"] = static_cast<long long>(r.conflicts);
        x["releases"] = static_cast<long long>(r.releases);
        arr.push(x);
    }
    return arr;
}

} // namespace dc

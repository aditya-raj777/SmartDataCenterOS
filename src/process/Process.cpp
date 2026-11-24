#include "process/Process.h"

#include <algorithm>
#include <cctype>
#include <random>
#include <set>
#include <sstream>

namespace dc {

// ------------------------------------------------------------------ Burst
Json Burst::toJson() const {
    Json j = Json::object();
    j["type"] = type == BurstType::CPU ? "cpu" : "io";
    j["length"] = length;
    if (type == BurstType::IO) j["cylinder"] = cylinder;
    if (!resources.empty()) {
        Json r = Json::array();
        for (const auto& s : resources) r.push(s);
        j["resources"] = r;
    }
    return j;
}

std::string Burst::toSpec() const {
    std::ostringstream os;
    if (type == BurstType::CPU) {
        os << 'C' << length;
        if (!resources.empty()) {
            os << '[';
            for (std::size_t i = 0; i < resources.size(); ++i) os << (i ? "," : "") << resources[i];
            os << ']';
        }
    } else {
        os << 'I' << length << '@' << cylinder;
    }
    return os.str();
}

// ------------------------------------------------------------------ ProcessSpec
int ProcessSpec::totalCpu() const {
    int t = 0;
    for (const auto& b : bursts) if (b.type == BurstType::CPU) t += b.length;
    return t;
}
int ProcessSpec::totalIo() const {
    int t = 0;
    for (const auto& b : bursts) if (b.type == BurstType::IO) t += b.length;
    return t;
}
std::vector<int> ProcessSpec::ioCylinders() const {
    std::vector<int> v;
    for (const auto& b : bursts) if (b.type == BurstType::IO) v.push_back(b.cylinder);
    return v;
}
std::vector<std::string> ProcessSpec::allResources() const {
    std::set<std::string> s;
    for (const auto& b : bursts) for (const auto& r : b.resources) s.insert(r);
    return std::vector<std::string>(s.begin(), s.end());
}
std::string ProcessSpec::burstSpec() const {
    std::string out;
    for (std::size_t i = 0; i < bursts.size(); ++i) {
        if (i) out += ' ';
        out += bursts[i].toSpec();
    }
    return out;
}

Json ProcessSpec::toJson() const {
    Json j = Json::object();
    j["pid"] = pid;
    j["name"] = name;
    j["arrival"] = arrival;
    j["priority"] = priority;
    j["memory"] = memory;
    j["cpuBurst"] = totalCpu();
    j["ioTime"] = totalIo();
    Json cyl = Json::array();
    for (int c : ioCylinders()) cyl.push(c);
    j["ioRequests"] = cyl;
    Json res = Json::array();
    for (const auto& r : allResources()) res.push(r);
    j["resources"] = res;
    Json b = Json::array();
    for (const auto& x : bursts) b.push(x.toJson());
    j["bursts"] = b;
    j["burstSpec"] = burstSpec();
    return j;
}

// ------------------------------------------------------------------ WorkloadParser
namespace {

int parsePositiveInt(const std::string& s, const std::string& ctx) {
    if (s.empty()) throw SimError("Missing number in burst token '" + ctx + "'");
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) throw SimError("Invalid number '" + s + "' in burst token '" + ctx + "'");
    return std::stoi(s);
}

std::vector<int> jsonIntList(const Json& j) {
    std::vector<int> out;
    if (j.isArray()) {
        for (const auto& v : j.items()) out.push_back(v.asInt());
    } else if (j.isString()) {
        std::string s = j.asString();
        for (char& c : s) if (c == ';' || c == ' ') c = ',';
        for (const auto& tok : util::split(s, ',')) {
            std::string t = util::trim(tok);
            if (!t.empty()) out.push_back(std::stoi(t));
        }
    } else if (j.isNumber()) {
        out.push_back(j.asInt());
    }
    return out;
}

std::vector<std::string> jsonStrList(const Json& j) {
    std::vector<std::string> out;
    if (j.isArray()) {
        for (const auto& v : j.items()) { std::string t = util::trim(v.asString()); if (!t.empty()) out.push_back(t); }
    } else if (j.isString()) {
        std::string s = j.asString();
        for (char& c : s) if (c == ';' || c == ' ') c = ',';
        for (const auto& tok : util::split(s, ',')) { std::string t = util::trim(tok); if (!t.empty()) out.push_back(t); }
    }
    return out;
}

} // namespace

std::vector<Burst> WorkloadParser::parseBurstSpec(const std::string& specIn) {
    // Tokenise on whitespace / ';' / ',' that are outside of [...]
    std::vector<std::string> tokens;
    std::string cur;
    int depth = 0;
    for (char c : specIn) {
        if (c == '[') depth++;
        if (c == ']') depth--;
        if (depth == 0 && (std::isspace(static_cast<unsigned char>(c)) || c == ';' || c == ',' || c == '>')) {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) tokens.push_back(cur);

    std::vector<Burst> out;
    for (const auto& raw : tokens) {
        std::string tok = util::trim(raw);
        if (tok.empty()) continue;
        if (tok == "-") continue;
        char kind = static_cast<char>(std::toupper(static_cast<unsigned char>(tok[0])));
        Burst b;
        if (kind == 'C') {
            b.type = BurstType::CPU;
            std::size_t lb = tok.find('[');
            std::string num = tok.substr(1, lb == std::string::npos ? std::string::npos : lb - 1);
            b.length = parsePositiveInt(num, tok);
            if (lb != std::string::npos) {
                std::size_t rb = tok.find(']', lb);
                if (rb == std::string::npos) throw SimError("Unclosed '[' in burst token '" + tok + "'");
                for (const auto& r : util::split(tok.substr(lb + 1, rb - lb - 1), ',')) {
                    std::string t = util::trim(r);
                    if (!t.empty()) b.resources.push_back(t);
                }
            }
        } else if (kind == 'I') {
            b.type = BurstType::IO;
            std::size_t at = tok.find('@');
            if (at == std::string::npos) throw SimError("I/O burst '" + tok + "' needs a cylinder, e.g. I3@98");
            b.length = parsePositiveInt(tok.substr(1, at - 1), tok);
            b.cylinder = parsePositiveInt(tok.substr(at + 1), tok);
        } else {
            throw SimError("Unknown burst token '" + tok + "' (use C<n>, C<n>[RES], I<n>@<cyl>)");
        }
        if (b.length <= 0) throw SimError("Burst length must be >= 1 in token '" + tok + "'");
        std::sort(b.resources.begin(), b.resources.end());
        b.resources.erase(std::unique(b.resources.begin(), b.resources.end()), b.resources.end());
        out.push_back(b);
    }
    return out;
}

std::vector<Burst> WorkloadParser::buildBursts(int cpuBurst, const std::vector<int>& io,
                                               const std::vector<std::string>& resourcesIn, int ioTime) {
    if (cpuBurst < 1) throw SimError("CPU burst must be >= 1");
    if (ioTime < 1) ioTime = 1;
    std::vector<std::string> resources = resourcesIn;
    std::sort(resources.begin(), resources.end());
    resources.erase(std::unique(resources.begin(), resources.end()), resources.end());

    // Critical section length (only if resources are requested).
    int cs = 0;
    if (!resources.empty()) cs = std::max(1, cpuBurst / 2);
    int plain = cpuBurst - cs;

    std::vector<Burst> out;
    auto addIo = [&](int cyl) {
        Burst b; b.type = BurstType::IO; b.length = ioTime; b.cylinder = cyl; out.push_back(b);
    };
    std::size_t ioIdx = 0;
    if (plain > 0) {
        int k = static_cast<int>(io.size());
        int segs = std::min(plain, k + 1);
        int base = plain / segs, extra = plain % segs;
        for (int s = 0; s < segs; ++s) {
            Burst c; c.type = BurstType::CPU; c.length = base + (s < extra ? 1 : 0);
            out.push_back(c);
            if (s < segs - 1 && ioIdx < io.size()) addIo(io[ioIdx++]);
        }
        // Remaining I/O requests (more I/O than CPU slices) go before the critical section / end.
        while (ioIdx < io.size()) addIo(io[ioIdx++]);
    }
    if (cs > 0) {
        Burst c; c.type = BurstType::CPU; c.length = cs; c.resources = resources;
        out.push_back(c);
        while (ioIdx < io.size()) addIo(io[ioIdx++]); // only when plain == 0
    }
    return out;
}

std::vector<ProcessSpec> WorkloadParser::fromJson(const Json& arr, int defaultIoTime) {
    if (!arr.isArray() && !arr.isNull()) throw SimError("'processes' must be an array");
    std::vector<ProcessSpec> out;
    std::set<int> used;
    // First pass: collect explicit PIDs to detect duplicates.
    for (const auto& j : arr.items()) {
        if (j.has("pid") && !j["pid"].isNull() && j["pid"].asString() != "") {
            int pid = j.getInt("pid");
            if (pid <= 0) throw SimError("PID must be a positive integer (got " + std::to_string(pid) + ")");
            if (!used.insert(pid).second) throw SimError("Duplicate PID " + std::to_string(pid));
        }
    }
    int nextPid = 1;
    for (const auto& j : arr.items()) {
        ProcessSpec p;
        if (j.has("pid") && !j["pid"].isNull() && j["pid"].asString() != "") {
            p.pid = j.getInt("pid");
        } else {
            while (used.count(nextPid)) ++nextPid;
            p.pid = nextPid;
            used.insert(nextPid);
        }
        p.name = util::trim(j.getStr("name", ""));
        if (p.name.empty()) p.name = "P" + std::to_string(p.pid);
        p.arrival = j.getInt("arrival", 0);
        p.priority = j.getInt("priority", 0);
        p.memory = j.getInt("memory", 0);
        if (p.arrival < 0) throw SimError(p.name + ": arrival time cannot be negative");
        if (p.memory < 0) throw SimError(p.name + ": memory requirement cannot be negative");

        int ioTime = j.getInt("ioTime", defaultIoTime);
        const Json& b = j["bursts"];
        if (b.isString() && !util::trim(b.asString()).empty()) {
            p.bursts = parseBurstSpec(b.asString());
        } else if (j.has("burstSpec") && !util::trim(j.getStr("burstSpec")).empty()) {
            p.bursts = parseBurstSpec(j.getStr("burstSpec"));
        } else if (b.isArray() && b.size() > 0) {
            for (const auto& bj : b.items()) {
                Burst x;
                std::string t = util::toUpper(bj.getStr("type", "cpu"));
                x.type = (t == "IO" || t == "I") ? BurstType::IO : BurstType::CPU;
                x.length = bj.getInt("length", 1);
                if (x.length < 1) throw SimError(p.name + ": burst length must be >= 1");
                if (x.type == BurstType::IO) {
                    x.cylinder = bj.getInt("cylinder", 0);
                } else {
                    x.resources = jsonStrList(bj["resources"]);
                    std::sort(x.resources.begin(), x.resources.end());
                    x.resources.erase(std::unique(x.resources.begin(), x.resources.end()), x.resources.end());
                }
                p.bursts.push_back(x);
            }
        } else {
            int cpu = j.getInt("cpuBurst", j.getInt("burst", 0));
            if (cpu < 1) throw SimError(p.name + ": CPU burst must be >= 1");
            p.bursts = buildBursts(cpu, jsonIntList(j["ioRequests"]), jsonStrList(j["resources"]), ioTime);
        }
        if (p.bursts.empty()) throw SimError(p.name + ": job has no bursts");
        bool hasCpu = false;
        for (const auto& x : p.bursts) {
            if (x.type == BurstType::CPU) hasCpu = true;
            if (x.type == BurstType::IO && x.cylinder < 0) throw SimError(p.name + ": I/O cylinder cannot be negative");
        }
        if (!hasCpu) throw SimError(p.name + ": job needs at least one CPU burst");
        out.push_back(p);
    }
    return out;
}

Json WorkloadParser::toJson(const std::vector<ProcessSpec>& procs) {
    Json arr = Json::array();
    for (const auto& p : procs) arr.push(p.toJson());
    return arr;
}

// ------------------------------------------------------------------ WorkloadGenerator
std::vector<ProcessSpec> WorkloadGenerator::generate(const std::string& profileIn, int count, unsigned seed,
                                                     int cylinders, int maxMemory) {
    if (count < 0) count = 0;
    if (count > 60) count = 60;
    if (cylinders < 2) cylinders = 200;
    if (maxMemory < 16) maxMemory = 256;
    std::mt19937 rng(seed);
    auto rnd = [&](int lo, int hi) { std::uniform_int_distribution<int> d(lo, hi); return d(rng); };
    std::string profile = profileIn;
    static const char* resNames[] = {"DB_LOCK", "LOG_FILE", "NET_PORT"};
    static const char* names[] = {"web", "db", "cache", "batch", "ml", "etl", "auth", "api", "backup", "stream", "index", "mail"};

    std::vector<ProcessSpec> out;
    int t = 0;
    for (int i = 0; i < count; ++i) {
        ProcessSpec p;
        p.pid = i + 1;
        p.name = std::string(names[rnd(0, 11)]) + "-" + std::to_string(p.pid);
        p.priority = rnd(1, 5);
        int cpu = 0, ioCount = 0;
        std::vector<std::string> res;
        std::string kind = profile;
        if (kind == "mixed") {
            int r = rnd(0, 3);
            kind = r == 0 ? "cpu" : r == 1 ? "disk" : r == 2 ? "memory" : "sync";
        }
        if (kind == "cpu") {
            cpu = rnd(8, 20); ioCount = rnd(0, 1); p.memory = rnd(maxMemory / 16, maxMemory / 6);
            t += rnd(0, 3);
        } else if (kind == "memory") {
            cpu = rnd(3, 9); ioCount = rnd(0, 1); p.memory = rnd(maxMemory / 6, maxMemory / 2);
            t += rnd(0, 2);
        } else if (kind == "disk") {
            cpu = rnd(3, 8); ioCount = rnd(2, 4); p.memory = rnd(maxMemory / 16, maxMemory / 5);
            t += rnd(0, 3);
        } else if (kind == "sync") {
            cpu = rnd(3, 8); ioCount = rnd(0, 1); p.memory = rnd(maxMemory / 16, maxMemory / 6);
            res.push_back(resNames[rnd(0, 1)]);
            if (rnd(0, 3) == 0) res.push_back(resNames[2]);
            t += rnd(0, 2);
        } else { // small / default
            cpu = rnd(2, 10); ioCount = rnd(0, 1); p.memory = rnd(maxMemory / 16, maxMemory / 4);
            t += rnd(0, 4);
        }
        p.arrival = (i == 0) ? 0 : t;
        std::vector<int> io;
        for (int k = 0; k < ioCount; ++k) io.push_back(rnd(0, cylinders - 1));
        p.bursts = WorkloadParser::buildBursts(cpu, io, res, rnd(1, 3));
        out.push_back(p);
    }
    return out;
}

} // namespace dc

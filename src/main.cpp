// Smart Data Center OS Simulator - C++ Engine CLI
// Reads a JSON request from stdin (or --request-file), dispatches to the real
// C++ OS modules, and writes a JSON response to stdout.
#include "analytics/Metrics.h"
#include "core/Json.h"
#include "core/Types.h"
#include "cpu/CpuScheduler.h"
#include "cpu/CpuSimulator.h"
#include "disk/DiskSimulator.h"
#include "memory/MemorySimulator.h"
#include "process/Process.h"
#include "simulation/SimulationEngine.h"
#include "synchronization/ConcurrencyDemo.h"
#include "synchronization/Threading.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace dc;

std::vector<CpuJob> parseCpuJobs(const Json& arr) {
    auto specs = WorkloadParser::fromJson(arr);
    std::vector<CpuJob> out;
    for (const auto& s : specs) {
        CpuJob j;
        j.pid = s.pid;
        j.name = s.name;
        j.arrival = s.arrival;
        j.burst = s.totalCpu();
        j.priority = s.priority;
        out.push_back(j);
    }
    return out;
}

std::vector<int> parseIntArray(const Json& j) {
    std::vector<int> out;
    if (j.isArray()) {
        for (const auto& v : j.items()) out.push_back(v.asInt());
    } else if (j.isString()) {
        std::string s = j.asString();
        for (char& c : s) if (c == ' ' || c == ';') c = ',';
        for (const auto& tok : util::split(s, ',')) {
            std::string t = util::trim(tok);
            if (!t.empty()) out.push_back(std::stoi(t));
        }
    }
    return out;
}

Json handleRequest(const Json& req) {
    std::string cmd = util::toUpper(req.getStr("action", req.getStr("command", "")));
    Json resp = Json::object();
    resp["ok"] = true;

    if (cmd == "INFO" || cmd == "PING") {
        Json data = Json::object();
        data["engine"] = "Smart Data Center OS Simulator C++ Engine";
        data["version"] = "1.0.0";
        data["threadingBackend"] = sync::backendName();
        Json cpuAlgos = Json::array();
        for (const auto& a : allCpuAlgorithms()) cpuAlgos.push(a);
        data["cpuAlgorithms"] = cpuAlgos;
        Json memAlgos = Json::array();
        for (const auto& a : allMemoryStrategies()) memAlgos.push(a);
        data["memoryStrategies"] = memAlgos;
        Json diskAlgos = Json::array();
        for (const auto& a : allDiskAlgorithms()) diskAlgos.push(a);
        data["diskAlgorithms"] = diskAlgos;
        resp["data"] = data;
        return resp;
    }

    if (cmd == "SAMPLES") {
        resp["data"] = SimulationEngine::sampleWorkloads();
        return resp;
    }

    if (cmd == "GENERATE") {
        std::string profile = req.getStr("profile", "mixed");
        int count = req.getInt("count", 6);
        unsigned seed = static_cast<unsigned>(req.getInt("seed", 42));
        int cyls = req.getInt("cylinders", 200);
        int maxMem = req.getInt("maxMemory", 256);
        auto specs = WorkloadGenerator::generate(profile, count, seed, cyls, maxMem);
        resp["data"] = WorkloadParser::toJson(specs);
        return resp;
    }

    if (cmd == "SIMULATE") {
        SimConfig cfg = SimConfig::fromJson(req["config"]);
        auto specs = WorkloadParser::fromJson(req["processes"], req.getInt("defaultIoTime", 2));
        resp["data"] = SimulationEngine::run(cfg, specs);
        return resp;
    }

    if (cmd == "CPU") {
        auto jobs = parseCpuJobs(req["processes"]);
        std::string algo = req.getStr("algorithm", "FCFS");
        int q = req.getInt("quantum", 2);
        bool preempt = req.getBool("preemptive", false);
        PriorityOrder po = parsePriorityOrder(req.getStr("priorityOrder", "LOWER"));
        auto sched = makeScheduler(algo, q, preempt, po);
        CpuResult r = CpuSimulator::run(jobs, *sched);
        resp["data"] = r.toJson(req.getBool("includeSteps", true));
        return resp;
    }

    if (cmd == "CPU_COMPARE") {
        auto jobs = parseCpuJobs(req["processes"]);
        int q = req.getInt("quantum", 2);
        PriorityOrder po = parsePriorityOrder(req.getStr("priorityOrder", "LOWER"));
        Json results = Json::array();
        Json rows = Json::array();
        for (const auto& algo : allCpuAlgorithms()) {
            auto sched = makeScheduler(algo, q, false, po);
            CpuResult r = CpuSimulator::run(jobs, *sched);
            Json rj = r.toJson(false);
            Json row = r.summary.toJson();
            row["algorithm"] = r.algorithm;
            row["algorithmName"] = r.algorithmName;
            rows.push(row);
            results.push(rj);
        }
        Json data = Json::object();
        data["results"] = results;
        data["rows"] = rows;
        data["best"] = PerformanceAnalyzer::bestOf(
            rows,
            {{"avgWaiting", true}, {"avgTurnaround", true}, {"avgResponse", true},
             {"cpuUtilization", false}, {"throughput", false}, {"contextSwitches", true}},
            "algorithm");
        resp["data"] = data;
        return resp;
    }

    if (cmd == "MEMORY" || cmd == "MEMORY_COMPARE") {
        MemoryLabConfig cfg;
        cfg.total = req.getInt("total", 1024);
        cfg.mode = parseMemoryMode(req.getStr("mode", "DYNAMIC"));
        cfg.strategy = req.getStr("strategy", "FIRST_FIT");
        cfg.partitions = parseIntArray(req["partitions"]);
        auto jobs = MemorySimulator::jobsFromJson(req.has("jobs") ? req["jobs"] : req["processes"]);
        resp["data"] = (cmd == "MEMORY") ? MemorySimulator::run(cfg, jobs) : MemorySimulator::compare(cfg, jobs);
        return resp;
    }

    if (cmd == "DISK" || cmd == "DISK_COMPARE") {
        DiskLabConfig cfg;
        cfg.cylinders = req.getInt("cylinders", 200);
        cfg.head = req.getInt("head", 53);
        cfg.direction = parseDirection(req.getStr("direction", "UP"));
        cfg.includeReturnSeek = req.getBool("includeReturnSeek", true);
        cfg.algorithm = req.getStr("algorithm", "FCFS");
        auto reqs = parseIntArray(req["requests"]);
        resp["data"] = (cmd == "DISK") ? DiskSimulator::run(cfg, reqs) : DiskSimulator::compare(cfg, reqs);
        return resp;
    }

    if (cmd == "SYNC") {
        SyncDemoConfig cfg;
        cfg.mode = req.getStr("mode", "race");
        cfg.threads = req.getInt("threads", 4);
        cfg.iterations = req.getInt("iterations", 1000);
        cfg.permits = req.getInt("permits", 2);
        cfg.bufferSize = req.getInt("bufferSize", 4);
        cfg.producers = req.getInt("producers", 2);
        cfg.consumers = req.getInt("consumers", 2);
        cfg.traceLimit = req.getInt("traceLimit", 400);
        resp["data"] = ConcurrencyDemo::run(cfg);
        return resp;
    }

    throw SimError("Unknown action '" + cmd + "'");
}

} // namespace

int main(int argc, char* argv[]) {
    std::string input;
    if (argc >= 2 && std::string(argv[1]) == "--info") {
        Json req = Json::object();
        req["action"] = "INFO";
        std::cout << handleRequest(req).dump() << "\n";
        return 0;
    }
    if (argc >= 3 && std::string(argv[1]) == "--file") {
        std::ifstream f(argv[2]);
        if (!f) {
            std::cout << "{\"ok\":false,\"error\":\"Cannot open request file\"}\n";
            return 1;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        input = ss.str();
    } else {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        input = ss.str();
    }

    try {
        Json req = Json::parse(input);
        Json resp = handleRequest(req);
        std::cout << resp.dump() << "\n";
        return 0;
    } catch (const std::exception& ex) {
        Json err = Json::object();
        err["ok"] = false;
        err["error"] = ex.what();
        std::cout << err.dump() << "\n";
        return 1;
    }
}

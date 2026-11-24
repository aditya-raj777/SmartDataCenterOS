#include "deadlock/DeadlockDetector.h"

#include <algorithm>
#include <map>
#include <queue>
#include <set>

namespace dc {

Json DeadlockResult::toJson() const {
    Json j = Json::object();
    j["hasDeadlock"] = hasDeadlock;
    
    Json cycleArr = Json::array();
    for (int pid : cycle) {
        cycleArr.push(pid);
    }
    j["cycle"] = cycleArr;

    Json nodesArr = Json::array();
    for (const auto& node : nodes) {
        Json nj = Json::object();
        nj["pid"] = node.pid;
        nj["name"] = node.name;
        
        Json h = Json::array();
        for (const auto& r : node.holding) h.push(r);
        nj["holding"] = h;

        Json w = Json::array();
        for (const auto& r : node.waiting) w.push(r);
        nj["waiting"] = w;

        nodesArr.push(nj);
    }
    j["nodes"] = nodesArr;
    j["description"] = description;
    j["ragEdges"] = ragEdges;
    return j;
}

DeadlockResult DeadlockDetector::detect(const ResourceManager& rm) {
    std::vector<DeadlockNode> nodes;
    std::map<int, DeadlockNode> procMap;

    for (const auto& kv : rm.all()) {
        const std::string& rname = kv.first;
        const SharedResource& res = kv.second;

        for (int holderPid : res.holders) {
            procMap[holderPid].pid = holderPid;
            if (procMap[holderPid].name.empty()) {
                procMap[holderPid].name = "P" + std::to_string(holderPid);
            }
            procMap[holderPid].holding.push_back(rname);
        }

        for (int waiterPid : res.waiters) {
            procMap[waiterPid].pid = waiterPid;
            if (procMap[waiterPid].name.empty()) {
                procMap[waiterPid].name = "P" + std::to_string(waiterPid);
            }
            procMap[waiterPid].waiting.push_back(rname);
        }
    }

    for (const auto& kv : procMap) {
        nodes.push_back(kv.second);
    }

    return detectFromScenario(nodes);
}

DeadlockResult DeadlockDetector::detectFromScenario(const std::vector<DeadlockNode>& nodes) {
    DeadlockResult result;
    result.nodes = nodes;
    result.hasDeadlock = false;

    // Build resource holder map: resource -> list of holder PIDs
    std::map<std::string, std::vector<int>> resourceHolders;
    std::set<std::string> allResources;

    for (const auto& n : nodes) {
        for (const auto& r : n.holding) {
            resourceHolders[r].push_back(n.pid);
            allResources.insert(r);
        }
        for (const auto& r : n.waiting) {
            allResources.insert(r);
        }
    }

    // Build directed edges for RAG visualization:
    // 1. Process -> Resource (Request / Wait)
    // 2. Resource -> Process (Assignment / Hold)
    Json edges = Json::array();
    for (const auto& n : nodes) {
        for (const auto& r : n.holding) {
            Json e = Json::object();
            e["from"] = r;
            e["to"] = "P" + std::to_string(n.pid);
            e["type"] = "assignment";
            edges.push(e);
        }
        for (const auto& r : n.waiting) {
            Json e = Json::object();
            e["from"] = "P" + std::to_string(n.pid);
            e["to"] = r;
            e["type"] = "request";
            edges.push(e);
        }
    }
    result.ragEdges = edges;

    // Construct Wait-For Graph (WFG) between processes:
    // If process P is waiting for resource R, and process Q is holding R,
    // then there is a directed wait-for edge P -> Q.
    std::map<int, std::set<int>> adj;
    std::set<int> allPids;

    for (const auto& n : nodes) {
        allPids.insert(n.pid);
        for (const auto& r : n.waiting) {
            auto it = resourceHolders.find(r);
            if (it != resourceHolders.end()) {
                for (int holderPid : it->second) {
                    if (holderPid != n.pid) {
                        adj[n.pid].insert(holderPid);
                    }
                }
            }
        }
    }

    // Detect cycle in directed WFG using DFS (colors: 0=unvisited, 1=visiting, 2=visited)
    std::map<int, int> color;
    std::map<int, int> parent;
    std::vector<int> cycleFound;

    auto dfs = [&](auto& self, int u) -> bool {
        color[u] = 1;
        for (int v : adj[u]) {
            if (color[v] == 1) {
                // Cycle detected! Reconstruct cycle from u to v
                cycleFound.push_back(v);
                int curr = u;
                while (curr != v && curr != 0) {
                    cycleFound.push_back(curr);
                    curr = parent[curr];
                }
                cycleFound.push_back(v);
                std::reverse(cycleFound.begin(), cycleFound.end());
                return true;
            }
            if (color[v] == 0) {
                parent[v] = u;
                if (self(self, v)) return true;
            }
        }
        color[u] = 2;
        return false;
    };

    for (int pid : allPids) {
        if (color[pid] == 0) {
            if (dfs(dfs, pid)) {
                result.hasDeadlock = true;
                result.cycle = cycleFound;
                break;
            }
        }
    }

    if (result.hasDeadlock) {
        std::string desc = "Circular wait detected across processes: ";
        for (std::size_t i = 0; i < result.cycle.size(); ++i) {
            desc += "P" + std::to_string(result.cycle[i]);
            if (i + 1 < result.cycle.size()) desc += " -> ";
        }
        result.description = desc;
    } else {
        result.description = "No circular wait detected. The resource allocation graph is acyclic.";
    }

    return result;
}

Json DeadlockDetector::presetScenarios() {
    Json scenarios = Json::array();

    // 1. Classic 2-Process Deadlock
    {
        Json s = Json::object();
        s["id"] = "classic_2p";
        s["name"] = "Classic 2-Process Deadlock (Circular Wait)";
        s["description"] = "P1 holds DB_LOCK and requests LOG_FILE; P2 holds LOG_FILE and requests DB_LOCK.";
        Json nodes = Json::array();

        Json p1 = Json::object();
        p1["pid"] = 1; p1["name"] = "tx-processor-1";
        Json h1 = Json::array(); h1.push("DB_LOCK"); p1["holding"] = h1;
        Json w1 = Json::array(); w1.push("LOG_FILE"); p1["waiting"] = w1;
        nodes.push(p1);

        Json p2 = Json::object();
        p2["pid"] = 2; p2["name"] = "audit-logger-2";
        Json h2 = Json::array(); h2.push("LOG_FILE"); p2["holding"] = h2;
        Json w2 = Json::array(); w2.push("DB_LOCK"); p2["waiting"] = w2;
        nodes.push(p2);

        s["nodes"] = nodes;
        scenarios.push(s);
    }

    // 2. 3-Way Circular Wait (Pipeline Lockup)
    {
        Json s = Json::object();
        s["id"] = "triangle_3p";
        s["name"] = "3-Way Multi-Resource Pipeline Deadlock";
        s["description"] = "P1 holds R1, waits for R2. P2 holds R2, waits for R3. P3 holds R3, waits for R1.";
        Json nodes = Json::array();

        Json p1 = Json::object();
        p1["pid"] = 1; p1["name"] = "etl-extract";
        Json h1 = Json::array(); h1.push("INGEST_BUF"); p1["holding"] = h1;
        Json w1 = Json::array(); w1.push("TRANSFORM_LOCK"); p1["waiting"] = w1;
        nodes.push(p1);

        Json p2 = Json::object();
        p2["pid"] = 2; p2["name"] = "etl-transform";
        Json h2 = Json::array(); h2.push("TRANSFORM_LOCK"); p2["holding"] = h2;
        Json w2 = Json::array(); w2.push("LOAD_CHANNEL"); p2["waiting"] = w2;
        nodes.push(p2);

        Json p3 = Json::object();
        p3["pid"] = 3; p3["name"] = "etl-load";
        Json h3 = Json::array(); h3.push("LOAD_CHANNEL"); p3["holding"] = h3;
        Json w3 = Json::array(); w3.push("INGEST_BUF"); p3["waiting"] = w3;
        nodes.push(p3);

        s["nodes"] = nodes;
        scenarios.push(s);
    }

    // 3. Dining Philosophers (5 processes, circular dependency)
    {
        Json s = Json::object();
        s["id"] = "dining_5p";
        s["name"] = "Dining Philosophers Resource Starvation";
        s["description"] = "5 workers each holding one chopstick/lock and waiting on the next in a ring.";
        Json nodes = Json::array();
        for (int i = 1; i <= 5; ++i) {
            int next = (i % 5) + 1;
            Json p = Json::object();
            p["pid"] = i;
            p["name"] = "worker-node-" + std::to_string(i);
            Json h = Json::array(); h.push("FORK_" + std::to_string(i)); p["holding"] = h;
            Json w = Json::array(); w.push("FORK_" + std::to_string(next)); p["waiting"] = w;
            nodes.push(p);
        }
        s["nodes"] = nodes;
        scenarios.push(s);
    }

    // 4. Safe / Deadlock-Free Hierarchical Allocation
    {
        Json s = Json::object();
        s["id"] = "safe_hierarchy";
        s["name"] = "Deadlock-Free Ordered Acquisition (Banker's Safe State)";
        s["description"] = "Strict global lock ordering prevents circular wait; acyclic DAG of dependencies.";
        Json nodes = Json::array();

        Json p1 = Json::object();
        p1["pid"] = 1; p1["name"] = "ingress-worker";
        Json h1 = Json::array(); h1.push("RESOURCE_A"); p1["holding"] = h1;
        Json w1 = Json::array(); w1.push("RESOURCE_B"); p1["waiting"] = w1;
        nodes.push(p1);

        Json p2 = Json::object();
        p2["pid"] = 2; p2["name"] = "backend-service";
        Json h2 = Json::array(); h2.push("RESOURCE_B"); p2["holding"] = h2;
        Json w2 = Json::array(); w2.push("RESOURCE_C"); p2["waiting"] = w2;
        nodes.push(p2);

        Json p3 = Json::object();
        p3["pid"] = 3; p3["name"] = "db-writer";
        Json h3 = Json::array(); h3.push("RESOURCE_C"); p3["holding"] = h3;
        Json w3 = Json::array(); p3["waiting"] = w3; // Not waiting on anything
        nodes.push(p3);

        s["nodes"] = nodes;
        scenarios.push(s);
    }

    return scenarios;
}

} // namespace dc

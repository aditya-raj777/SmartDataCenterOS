// Deadlock detection and Resource Allocation Graph (RAG) analysis.
// Supports wait-for cycle detection, scenario validation, and graph serialization.
#pragma once

#include "core/Json.h"
#include "synchronization/ResourceManager.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace dc {

struct DeadlockNode {
    int pid = 0;
    std::string name;
    std::vector<std::string> holding; // Resource names currently held
    std::vector<std::string> waiting; // Resource names waiting for
};

struct DeadlockResult {
    bool hasDeadlock = false;
    std::vector<int> cycle;          // PIDs involved in circular wait
    std::vector<DeadlockNode> nodes; // All nodes in the evaluated graph
    std::string description;
    Json ragEdges;                   // Directed edges for UI RAG visualization

    Json toJson() const;
};

class DeadlockDetector {
public:
    /// Build RAG from active ResourceManager state and detect circular wait
    static DeadlockResult detect(const ResourceManager& rm);

    /// Detect deadlock in an arbitrary process-resource scenario
    static DeadlockResult detectFromScenario(const std::vector<DeadlockNode>& nodes);

    /// Curated realistic scenarios demonstrating deadlock conditions & prevention
    static Json presetScenarios();
};

} // namespace dc

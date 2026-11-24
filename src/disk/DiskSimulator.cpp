#include "disk/DiskSimulator.h"

#include "analytics/Metrics.h"

namespace dc {

Json DiskSimulator::run(const DiskLabConfig& cfg, const std::vector<int>& requests) {
    if (cfg.cylinders < 2) throw SimError("Disk must have at least 2 cylinders");
    if (cfg.head < 0 || cfg.head >= cfg.cylinders) throw SimError("Initial head position must be within [0, cylinders-1]");
    for (int r : requests)
        if (r < 0 || r >= cfg.cylinders) throw SimError("Request " + std::to_string(r) + " is outside the disk (0.." + std::to_string(cfg.cylinders - 1) + ")");

    auto sched = makeDiskScheduler(cfg.algorithm);
    std::vector<DiskRequest> pending;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        DiskRequest d;
        d.id = static_cast<int>(i);
        d.cylinder = requests[i];
        d.seq = static_cast<long>(i);
        pending.push_back(d);
    }
    int head = cfg.head;
    Direction dir = cfg.direction;
    std::vector<SeekMove> allMoves;
    Json order = Json::array();
    Json steps = Json::array();
    int cumulative = 0;
    while (!pending.empty()) {
        std::vector<SeekMove> moves;
        int from = head;
        int idx = sched->selectNext(pending, head, dir, cfg.cylinders, moves);
        int dist = totalSeek(moves, cfg.includeReturnSeek);
        cumulative += dist;
        Json s = Json::object();
        s["from"] = from;
        s["to"] = pending[idx].cylinder;
        s["requestIndex"] = pending[idx].id;
        s["distance"] = dist;
        s["cumulative"] = cumulative;
        s["direction"] = toString(dir);
        Json mv = Json::array();
        for (const auto& m : moves) {
            Json x = Json::object();
            x["from"] = m.from; x["to"] = m.to; x["jump"] = m.jump; x["edge"] = m.edge; x["distance"] = m.distance();
            mv.push(x);
            allMoves.push_back(m);
        }
        s["moves"] = mv;
        steps.push(s);
        order.push(pending[idx].cylinder);
        pending.erase(pending.begin() + idx);
    }
    Json path = Json::array();
    path.push(cfg.head);
    for (const auto& m : allMoves) path.push(m.to);

    int total = totalSeek(allMoves, cfg.includeReturnSeek);
    Json out = Json::object();
    out["algorithm"] = sched->code();
    out["algorithmName"] = sched->name();
    out["cylinders"] = cfg.cylinders;
    out["initialHead"] = cfg.head;
    out["initialDirection"] = toString(cfg.direction);
    out["includeReturnSeek"] = cfg.includeReturnSeek;
    Json req = Json::array();
    for (int r : requests) req.push(r);
    out["requests"] = req;
    out["order"] = order;
    out["path"] = path;
    out["steps"] = steps;
    out["totalMovement"] = total;
    out["totalMovementWithJumps"] = totalSeek(allMoves, true);
    out["averageSeek"] = requests.empty() ? 0.0 : util::round2(static_cast<double>(total) / requests.size());
    out["requestCount"] = static_cast<int>(requests.size());
    out["finalHead"] = head;
    return out;
}

Json DiskSimulator::compare(const DiskLabConfig& cfgIn, const std::vector<int>& requests) {
    Json results = Json::array();
    Json rows = Json::array();
    for (const auto& a : allDiskAlgorithms()) {
        DiskLabConfig cfg = cfgIn;
        cfg.algorithm = a;
        Json r = run(cfg, requests);
        Json row = Json::object();
        row["algorithm"] = r["algorithm"];
        row["algorithmName"] = r["algorithmName"];
        row["totalMovement"] = r["totalMovement"];
        row["averageSeek"] = r["averageSeek"];
        row["requestCount"] = r["requestCount"];
        rows.push(row);
        results.push(r);
    }
    Json out = Json::object();
    out["results"] = results;
    out["rows"] = rows;
    out["best"] = PerformanceAnalyzer::bestOf(rows, {{"totalMovement", true}, {"averageSeek", true}}, "algorithm");
    return out;
}

} // namespace dc

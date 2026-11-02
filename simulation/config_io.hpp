// JSON <-> Config / ProcessSpec translation shared by the API server, scenario loader and CLI.
#pragma once
#include <stdexcept>
#include "../kernel/common/config.hpp"
#include "../kernel/common/json.hpp"
#include "../kernel/process/program.hpp"

namespace owr {

inline void applyConfig(Config& c, const json::Value& j) {
    if (!j.isObject()) return;
    if (j.has("scheduler")) {
        auto p = parseSched(j["scheduler"].str());
        if (!p) throw std::runtime_error("unknown scheduler '" + j["scheduler"].str() + "'");
        c.sched = *p;
    }
    if (j.has("replacement")) {
        auto p = parseRepl(j["replacement"].str());
        if (!p) throw std::runtime_error("unknown replacement policy '" + j["replacement"].str() + "'");
        c.repl = *p;
    }
    if (j.has("recovery")) {
        auto p = parseEnum<RecoveryPolicy>(RECOVERY_NAMES, j["recovery"].str());
        if (!p) throw std::runtime_error("unknown recovery policy");
        c.recovery = *p;
    }
    auto num = [&](const char* k, int& dst, int lo, int hi) {
        if (!j.has(k)) return;
        int v = j[k].integer(dst);
        if (v < lo || v > hi) throw std::runtime_error(std::string(k) + " must be between " + std::to_string(lo) + " and " + std::to_string(hi));
        dst = v;
    };
    num("quantum", c.quantum, 1, 1000);
    num("physicalKB", c.physicalKB, 4, 1 << 20);
    if (j.has("memoryKB")) c.physicalKB = std::clamp(j["memoryKB"].integer(c.physicalKB), 4, 1 << 20);
    num("pageSizeKB", c.pageSizeKB, 1, 1024);
    num("agingInterval", c.agingInterval, 1, 10000);
    num("ctxSwitchCost", c.ctxSwitchCost, 0, 50);
    num("pageFaultLatency", c.pageFaultLatency, 1, 200);
    num("diskBuffer", c.diskBuffer, 1, 256);
    num("spoolCapacity", c.spoolCapacity, 1, 256);
    num("deviceRepairTicks", c.deviceRepairTicks, 1, 100000);
    num("deadlockRecoveryDelay", c.deadlockRecoveryDelay, 0, 10000);
    num("msgQueueCapacity", c.msgQueueCapacity, 1, 256);
    num("starvationTicks", c.starvationTicks, 10, 100000);
    num("maxProcesses", c.maxProcesses, 1, 512);
    if (j.has("aging")) c.aging = j["aging"].boolean(c.aging);
    if (j.has("thrashingControl")) c.thrashingControl = j["thrashingControl"].boolean(c.thrashingControl);
    if (j.has("autoDeadlockRecovery")) c.autoDeadlockRecovery = j["autoDeadlockRecovery"].boolean(c.autoDeadlockRecovery);
    if (j.has("seed")) c.seed = (uint32_t)j["seed"].num(c.seed);
}

inline ProcessSpec specFromJson(const json::Value& j) {
    ProcessSpec s;
    s.name = j["name"].str();
    if (j.has("type")) {
        auto t = parsePType(j["type"].str());
        if (!t) throw std::runtime_error("unknown process type '" + j["type"].str() + "'");
        s.type = *t;
    }
    s.cpuBurst = std::clamp(j["cpuBurst"].integer(20), 1, 5000);
    s.ioBurst = std::clamp(j["ioBurst"].integer(0), 0, 50000);
    s.priority = std::clamp(j["priority"].integer(5), 0, 9);
    s.memoryKB = std::clamp(j["memoryKB"].integer(32), 1, 1 << 20);
    s.arrival = j.has("arrival") ? j["arrival"].integer(-1) : -1;
    s.localityPages = j["localityPages"].integer(-1);
    s.memoryRefs = j["memoryRefs"].boolean(true);
    if (j["script"].isArray()) {
        std::vector<std::string> lines;
        for (auto& l : j["script"].a) lines.push_back(l.str());
        s.script = assemble(lines);
        if (s.script.empty()) throw std::runtime_error("script contains no instructions");
    } else if (j["script"].isString()) {
        std::vector<std::string> lines;
        std::string cur;
        for (char ch : j["script"].s) { if (ch == '\n') { lines.push_back(cur); cur.clear(); } else cur += ch; }
        lines.push_back(cur);
        s.script = assemble(lines);
        if (s.script.empty()) throw std::runtime_error("script contains no instructions");
    }
    return s;
}

}  // namespace owr

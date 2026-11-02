#pragma once
#include "../kernel/kernel.hpp"
#include "../simulation/config_io.hpp"
#include "../simulation/experiment_lab.hpp"
#include "../simulation/failure_lab.hpp"
#include "../simulation/simulation_engine.hpp"
#include "test_framework.hpp"

namespace th {
using namespace owr;

// Pure-CPU textbook configuration: no context-switch cost, no aging, no memory references.
inline Config textbook(SchedPolicy p, int quantum = 4) {
    Config c;
    c.sched = p; c.quantum = quantum; c.ctxSwitchCost = 0; c.aging = false; c.thrashingControl = false; c.autoDeadlockRecovery = false;
    return c;
}
inline int spawn(Kernel& k, int burst, int arrival = 0, int priority = 5) {
    ProcessSpec s;
    s.cpuBurst = burst; s.arrival = arrival; s.priority = priority; s.memoryRefs = false; s.memoryKB = 8;
    return k.createProcess(s);
}
inline int spawnScript(Kernel& k, const std::vector<std::string>& lines, int priority = 5, int arrival = -1, int memKB = 16, bool memRefs = false) {
    ProcessSpec s;
    s.script = assemble(lines); s.priority = priority; s.arrival = arrival; s.memoryKB = memKB; s.memoryRefs = memRefs;
    return k.createProcess(s);
}
inline bool logHas(const Kernel& k, const std::string& needle) { return k.log().contains(needle); }
inline size_t logPos(const Kernel& k, const std::string& needle, size_t from = 0) {
    size_t i = 0;
    for (auto& e : k.log().events()) { if (i >= from && e.msg.find(needle) != std::string::npos) return i; ++i; }
    return std::string::npos;
}
inline void runUntilDone(Kernel& k, int limit = 5000) { while (!k.allDone() && k.now() < limit) k.tick(); }
}  // namespace th

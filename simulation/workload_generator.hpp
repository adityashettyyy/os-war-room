// Deterministic synthetic workloads for the Experiment Lab and the OS Detective.
#pragma once
#include "../kernel/kernel.hpp"

namespace owr {

struct WorkloadSpec {
    int cpuBound = 10, ioBound = 10, interactive = 5;
    int memMinKB = 24, memMaxKB = 48;     // per-process virtual size
    int arrivalSpread = 20;               // arrivals are uniform in [0, spread)
    double ioScale = 1.0;                 // multiplies every process's device time
    uint32_t seed = 42;
    int total() const { return cpuBound + ioBound + interactive; }
};

inline std::vector<int> spawnWorkload(Kernel& k, const WorkloadSpec& w) {
    Rng rng(w.seed * 7919u + 17u);
    std::vector<int> pids;
    auto make = [&](ProcType t, int idx) {
        ProcessSpec s;
        s.type = t;
        s.memoryKB = rng.range(std::min(w.memMinKB, w.memMaxKB), std::max(w.memMinKB, w.memMaxKB));
        s.arrival = k.now() + (w.arrivalSpread > 0 ? rng.range(0, w.arrivalSpread - 1) : 0);
        int io = 0;
        switch (t) {
            case ProcType::CPU_BOUND: s.name = "cpu-" + std::to_string(idx); s.cpuBurst = rng.range(30, 60); io = rng.range(0, 1) ? 20 : 0; s.priority = rng.range(5, 8); break;
            case ProcType::IO_BOUND: s.name = "io-" + std::to_string(idx); s.cpuBurst = rng.range(20, 40); io = rng.range(40, 80); s.priority = rng.range(3, 6); break;
            default: s.name = "ui-" + std::to_string(idx); s.cpuBurst = rng.range(10, 20); io = rng.range(20, 40); s.priority = rng.range(1, 3); break;
        }
        s.ioBurst = (int)(io * w.ioScale + 0.5);
        int pid = k.createProcess(s);
        if (pid > 0) pids.push_back(pid);
    };
    for (int i = 1; i <= w.cpuBound; ++i) make(ProcType::CPU_BOUND, i);
    for (int i = 1; i <= w.ioBound; ++i) make(ProcType::IO_BOUND, i);
    for (int i = 1; i <= w.interactive; ++i) make(ProcType::INTERACTIVE, i);
    return pids;
}

}  // namespace owr

#include "experiment_lab.hpp"

#include <cmath>

namespace owr {

std::string ExperimentResult::toJson() const {
    json::Writer w;
    w.obj().k("label").v(label).k("finished").v(finished).k("metrics").raw(m.toJson()).end();
    return w.str();
}

ExperimentResult runExperiment(const WorkloadSpec& w, Config c, const std::string& label, int maxTicks) {
    c.seed = w.seed;
    c.maxProcesses = std::max(c.maxProcesses, w.total() + 8);
    Kernel k(c);
    spawnWorkload(k, w);
    while (!k.allDone() && k.now() < maxTicks) k.tick();
    ExperimentResult r;
    r.label = label.empty() ? std::string(str(c.sched)) : label;
    r.finished = k.allDone();
    r.m = k.metrics();
    return r;
}

std::vector<ExperimentResult> compareSchedulers(const WorkloadSpec& w, const Config& base, const std::vector<SchedPolicy>& policies) {
    std::vector<ExperimentResult> out;
    for (auto p : policies) { Config c = base; c.sched = p; out.push_back(runExperiment(w, c, str(p))); }
    return out;
}

std::vector<ExperimentResult> compareReplacement(const WorkloadSpec& w, const Config& base, const std::vector<ReplPolicy>& policies) {
    std::vector<ExperimentResult> out;
    for (auto p : policies) { Config c = base; c.repl = p; out.push_back(runExperiment(w, c, str(p))); }
    return out;
}

std::vector<WhatIfPoint> whatIf(const WorkloadSpec& w0, const Config& base, const std::string& var, const std::vector<double>& values) {
    std::vector<WhatIfPoint> out;
    for (double v : values) {
        WorkloadSpec w = w0;
        Config c = base;
        if (var == "processes") {
            int n = std::max(1, (int)std::lround(v));
            w.cpuBound = (int)std::lround(n * 0.4); w.ioBound = (int)std::lround(n * 0.4);
            w.interactive = std::max(0, n - w.cpuBound - w.ioBound);
        } else if (var == "quantum") c.quantum = std::max(1, (int)std::lround(v));
        else if (var == "memoryKB") c.physicalKB = std::max(c.pageSizeKB, (int)std::lround(v));
        else if (var == "pageSizeKB") c.pageSizeKB = std::max(1, (int)std::lround(v));
        else if (var == "ioRate") w.ioScale = std::max(0.1, v);
        else throw std::runtime_error("unknown what-if variable '" + var + "'");
        WhatIfPoint p;
        p.value = v;
        p.r = runExperiment(w, c, var + "=" + std::to_string((int)std::lround(v)));
        out.push_back(p);
    }
    return out;
}

}  // namespace owr

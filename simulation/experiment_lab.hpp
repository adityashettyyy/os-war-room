// OS Experiment Lab: run one workload under different algorithms / parameters and compare metrics.
#pragma once
#include "workload_generator.hpp"

namespace owr {

struct ExperimentResult {
    std::string label;
    Metrics m;
    bool finished = false;
    std::string toJson() const;
};
struct WhatIfPoint {
    double value = 0;
    ExperimentResult r;
};

// Runs `w` on a fresh kernel configured by `c` until every process finishes (or maxTicks).
ExperimentResult runExperiment(const WorkloadSpec& w, Config c, const std::string& label = "", int maxTicks = 30000);
std::vector<ExperimentResult> compareSchedulers(const WorkloadSpec& w, const Config& base, const std::vector<SchedPolicy>& policies);
std::vector<ExperimentResult> compareReplacement(const WorkloadSpec& w, const Config& base, const std::vector<ReplPolicy>& policies);
// variable: processes | quantum | memoryKB | pageSizeKB | ioRate
std::vector<WhatIfPoint> whatIf(const WorkloadSpec& w, const Config& base, const std::string& variable, const std::vector<double>& values);

}  // namespace owr

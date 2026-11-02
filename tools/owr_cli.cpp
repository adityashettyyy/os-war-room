// owr_cli — headless front-end to the simulator: scenarios, experiments, what-if sweeps, page-replacement lab.
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include "../simulation/detective.hpp"
#include "../simulation/experiment_lab.hpp"
#include "../simulation/simulation_engine.hpp"

using namespace owr;

static std::map<std::string, std::string> parseArgs(int argc, char** argv, int from) {
    std::map<std::string, std::string> a;
    for (int i = from; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) != 0) continue;
        std::string key = s.substr(2);
        if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) a[key] = argv[++i];
        else a[key] = "1";
    }
    return a;
}
static int geti(const std::map<std::string, std::string>& a, const char* k, int d) { auto it = a.find(k); return it == a.end() ? d : std::atoi(it->second.c_str()); }
static std::string gets(const std::map<std::string, std::string>& a, const char* k, const std::string& d) { auto it = a.find(k); return it == a.end() ? d : it->second; }

static void usage() {
    std::puts(
        "OS War Room CLI\n"
        "  owr_cli scenario <file.json> [--ticks N] [--log]      run a scenario file and print a summary\n"
        "  owr_cli experiment [--cpu 10 --io 10 --ui 5 --memory 256 --page 4 --quantum 4 --repl LRU --seed 42]\n"
        "  owr_cli replacement [--refs \"7 0 1 2 ...\"] [--frames 3]\n"
        "  owr_cli whatif --var processes|quantum|memoryKB|pageSizeKB|ioRate --values 10,20,30 [workload options]\n"
        "  owr_cli detective                                      print every case file (with answers) for review");
}

static WorkloadSpec workloadFrom(const std::map<std::string, std::string>& a) {
    WorkloadSpec w;
    w.cpuBound = geti(a, "cpu", 10); w.ioBound = geti(a, "io", 10); w.interactive = geti(a, "ui", 5);
    w.seed = (uint32_t)geti(a, "seed", 42);
    return w;
}
static Config configFrom(const std::map<std::string, std::string>& a) {
    Config c;
    c.physicalKB = geti(a, "memory", 256); c.pageSizeKB = geti(a, "page", 4); c.quantum = geti(a, "quantum", 4);
    c.thrashingControl = false;
    if (auto r = parseRepl(gets(a, "repl", "LRU"))) c.repl = *r;
    return c;
}
static void printTable(const std::vector<ExperimentResult>& rs) {
    std::printf("| Metric | ");
    for (auto& r : rs) std::printf("%s | ", r.label.c_str());
    std::printf("\n|---|"); for (size_t i = 0; i < rs.size(); ++i) std::printf("---:|"); std::printf("\n");
    auto row = [&](const char* name, auto get, const char* fmt) {
        std::printf("| %s | ", name);
        for (auto& r : rs) { char b[64]; std::snprintf(b, sizeof b, fmt, get(r.m)); std::printf("%s | ", b); }
        std::printf("\n");
    };
    row("Avg waiting (ms)", [](const Metrics& m) { return m.avgWaiting; }, "%.1f");
    row("Avg turnaround (ms)", [](const Metrics& m) { return m.avgTurnaround; }, "%.1f");
    row("Avg response (ms)", [](const Metrics& m) { return m.avgResponse; }, "%.1f");
    row("Makespan (ms)", [](const Metrics& m) { return (double)m.makespan; }, "%.0f");
    row("CPU utilisation (%)", [](const Metrics& m) { return m.cpuUtil * 100; }, "%.1f");
    row("Throughput (/100 ms)", [](const Metrics& m) { return m.throughput; }, "%.2f");
    row("Context switches", [](const Metrics& m) { return (double)m.ctxSwitches; }, "%.0f");
    row("Page faults", [](const Metrics& m) { return (double)m.pageFaults; }, "%.0f");
    row("Page-fault rate (%)", [](const Metrics& m) { return m.pfRate * 100; }, "%.1f");
    row("Replacements", [](const Metrics& m) { return (double)m.replacements; }, "%.0f");
    row("Avg I/O queue wait (ms)", [](const Metrics& m) { return m.avgIoWait; }, "%.1f");
    std::printf("\n");
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    std::string cmd = argv[1];
    try {
        if (cmd == "scenario" && argc >= 3) {
            auto a = parseArgs(argc, argv, 3);
            Simulation sim;
            auto info = sim.loadScenario(json::parse(Simulation::readFile(argv[2])));
            if (a.count("log")) sim.kernel().log().setEcho(true);
            int ticks = geti(a, "ticks", info.suggestedTicks);
            sim.step(ticks);
            const Kernel& k = sim.kernel();
            auto m = k.metrics();
            std::printf("\n== %s ==\n%s\n", info.title.c_str(), info.description.c_str());
            std::printf("ticks=%d completed=%d/%d failed=%d cpu=%.1f%% ctxSwitches=%ld pageFaults=%ld (%.1f%%) deadlocks=%ld thrashEvents=%ld\n",
                        m.ticks, m.completed, m.processes, m.failed, m.cpuUtil * 100, m.ctxSwitches, m.pageFaults, m.pfRate * 100, m.deadlocks, k.thrashingEvents());
        } else if (cmd == "experiment") {
            auto a = parseArgs(argc, argv, 2);
            WorkloadSpec w = workloadFrom(a);
            Config c = configFrom(a);
            std::printf("Workload: %d CPU-bound, %d I/O-bound, %d interactive; memory %d KB, page %d KB, quantum %d, replacement %s, seed %u\n\n",
                        w.cpuBound, w.ioBound, w.interactive, c.physicalKB, c.pageSizeKB, c.quantum, str(c.repl), w.seed);
            printTable(compareSchedulers(w, c, {SchedPolicy::FCFS, SchedPolicy::SJF, SchedPolicy::SRTF, SchedPolicy::RR, SchedPolicy::PRIORITY, SchedPolicy::MLFQ, SchedPolicy::ADAPTIVE}));
            std::printf("Page replacement (scheduler %s):\n\n", str(c.sched));
            printTable(compareReplacement(w, c, {ReplPolicy::FIFO, ReplPolicy::LRU, ReplPolicy::CLOCK, ReplPolicy::OPTIMAL}));
        } else if (cmd == "replacement") {
            auto a = parseArgs(argc, argv, 2);
            std::vector<int> refs;
            std::istringstream in(gets(a, "refs", "7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1"));
            for (int x; in >> x;) refs.push_back(x);
            int frames = geti(a, "frames", 3);
            std::printf("Reference string (%zu refs), %d frames\n\n| Algorithm | Faults | Hits | Fault rate |\n|---|---:|---:|---:|\n", refs.size(), frames);
            for (auto p : {ReplPolicy::FIFO, ReplPolicy::LRU, ReplPolicy::CLOCK, ReplPolicy::OPTIMAL}) {
                auto r = simulateReplacement(refs, frames, p);
                std::printf("| %s | %d | %d | %.1f%% |\n", str(p), r.faults, r.hits, 100.0 * r.faults / (double)refs.size());
            }
        } else if (cmd == "whatif") {
            auto a = parseArgs(argc, argv, 2);
            std::vector<double> vals;
            std::istringstream in(gets(a, "values", "10,20,30,40"));
            for (std::string t; std::getline(in, t, ',');) vals.push_back(std::atof(t.c_str()));
            auto pts = whatIf(workloadFrom(a), configFrom(a), gets(a, "var", "processes"), vals);
            std::printf("| %s | CPU util (%%) | Page-fault rate (%%) | Avg waiting (ms) | Throughput (/100 ms) | Makespan (ms) | Finished |\n|---:|---:|---:|---:|---:|---:|:--:|\n", gets(a, "var", "processes").c_str());
            for (auto& p : pts)
                std::printf("| %g | %.1f | %.1f | %.1f | %.2f | %d | %s |\n", p.value, p.r.m.cpuUtil * 100, p.r.m.pfRate * 100, p.r.m.avgWaiting, p.r.m.throughput, p.r.m.makespan, p.r.finished ? "yes" : "no");
        } else if (cmd == "detective") {
            for (int i = 1; i <= detectiveCaseCount(); ++i) {
                auto& c = detectiveCase(i);
                std::printf("CASE #%02d %s [%s]\n  answer: %s\n  symptoms: %s\n\n", i, c.title.c_str(), c.difficulty.c_str(), c.answer.c_str(), c.symptomsJson.c_str());
            }
        } else { usage(); return 1; }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
    return 0;
}

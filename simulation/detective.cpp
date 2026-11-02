#include "detective.hpp"

#include <cmath>

namespace owr {

const std::vector<std::string>& detectiveChoices() {
    static const std::vector<std::string> c{"THRASHING", "DEADLOCK", "I/O FLOOD", "DEVICE FAILURE", "STARVATION", "CONVOY EFFECT"};
    return c;
}
int detectiveCaseCount() { return 6; }

static bool hidden(const LogEvent& e) {
    if (e.cat == Category::FAILURE) return true;
    static const char* leaks[] = {"⚠", "THRASHING CONTROL", "RECOVERY", "Deadlock #", "Thrashing cleared", "SCENARIO", "DEVICE FAILURE"};
    for (auto* l : leaks) if (e.msg.find(l) != std::string::npos) return true;
    return false;
}

static std::vector<std::string> tail(const Kernel& k, std::set<Category> cats, size_t n) {
    std::vector<std::string> out;
    for (auto& e : k.log().events()) {
        if (!cats.count(e.cat) || hidden(e)) continue;
        out.push_back(Logger::stamp(e.tick) + "  " + e.msg);
    }
    if (out.size() > n) out.erase(out.begin(), out.end() - (long)n);
    return out;
}

static DetectiveCase build(int id) {
    DetectiveCase c;
    c.id = id;
    Config cfg;
    cfg.thrashingControl = false;
    cfg.autoDeadlockRecovery = false;
    Simulation sim(cfg);
    int ticks = 120;
    switch (id) {
        case 1:
            c.title = "System performance dropped suddenly"; c.difficulty = "Medium"; c.answer = "THRASHING";
            c.explanation = "Total working sets exceed physical memory: the page-fault rate is very high, so processes spend their time waiting for page-ins and CPU utilisation collapses even though many processes exist.";
            c.hints = {"Compare CPU utilisation with the number of active processes.", "Read the memory log: what keeps happening, and to whom?"};
            failure::exhaustMemory(sim.kernel(), 16); ticks = 320;
            break;
        case 2:
            c.title = "Jobs stopped finishing"; c.difficulty = "Easy"; c.answer = "DEADLOCK";
            c.explanation = "Two processes each hold a lock the other needs: a circular wait. Neither can ever proceed, while unrelated processes finish normally.";
            c.hints = {"Which processes are WAITING and never wake up?", "Follow the LOCK lines: who owns each resource?"};
            { ProcessSpec s; for (int i = 0; i < 3; ++i) { s.cpuBurst = 25; s.memoryKB = 16; s.priority = 5; sim.kernel().createProcess(s); } }
            failure::createDeadlock(sim.kernel(), 2); ticks = 150;
            break;
        case 3:
            c.title = "Everything is waiting on something"; c.difficulty = "Easy"; c.answer = "I/O FLOOD";
            c.explanation = "A burst of disk and printer requests overwhelmed the devices: queues are long, processes sit in WAITING for I/O, and the CPU has little runnable work.";
            c.hints = {"Look at the longest device queue.", "Is the CPU idle because nothing is ready, or because memory is the problem?"};
            failure::floodIO(sim.kernel(), 30, false); ticks = 70;
            break;
        case 4:
            c.title = "Requests are failing with errors"; c.difficulty = "Medium"; c.answer = "DEVICE FAILURE";
            c.explanation = "The disk went offline: queued requests were aborted with EIO and new ones are rejected until the repair window ends.";
            c.hints = {"Search the I/O log for the first error.", "Do errors affect every device or just one?"};
            { WorkloadSpec w; w.cpuBound = 2; w.ioBound = 8; w.interactive = 0; w.arrivalSpread = 5; spawnWorkload(sim.kernel(), w); }
            sim.schedule(25, [](Kernel& k) { failure::deviceFailure(k, "DISK", 300); }); ticks = 90;
            break;
        case 5:
            c.title = "One job never gets to run"; c.difficulty = "Hard"; c.answer = "STARVATION";
            c.explanation = "Strict priority scheduling without aging lets a stream of high-priority work keep a low-priority process in the ready queue indefinitely.";
            c.hints = {"Compare how long different READY processes have waited.", "Which scheduling policy is active, and is aging on?"};
            failure::starvation(sim.kernel(), true); ticks = 220;
            break;
        default:
            c.title = "CPU busy, disk idle, jobs piling up"; c.difficulty = "Hard"; c.answer = "CONVOY EFFECT";
            c.explanation = "Under FCFS one long CPU-bound job holds the CPU while short I/O-bound jobs queue behind it, so the devices sit idle until it finishes.";
            c.hints = {"Which process has been RUNNING for a very long time?", "What are the I/O-bound processes doing while the disk is idle?"};
            sim.reset([&] { Config x = cfg; x.sched = SchedPolicy::FCFS; return x; }());
            { ProcessSpec big; big.name = "batch-job"; big.cpuBurst = 130; big.memoryKB = 16; big.memoryRefs = false; sim.kernel().createProcess(big);
              for (int i = 0; i < 8; ++i) { ProcessSpec s; s.name = "io-" + std::to_string(i + 1); s.type = ProcType::IO_BOUND; s.cpuBurst = 12; s.ioBurst = 40; s.memoryKB = 16; s.arrival = 1; s.memoryRefs = false; sim.kernel().createProcess(s); } }
            ticks = 110;
            break;
    }
    sim.step(ticks);
    Kernel& k = sim.kernel();
    int blocked = 0, ready = 0;
    for (auto& [pid, p] : k.processes()) { blocked += p.state == State::WAITING; ready += p.state == State::READY; }
    json::Writer w;
    w.obj().k("tick").v(k.now()).k("cpuUtil").v(k.windowCpuUtil()).k("memoryUtil").v(k.memUtil()).k("pageFaultRate").v(k.windowPfRate())
        .k("activeProcesses").v(k.aliveCount()).k("readyQueue").v(ready).k("blockedProcesses").v(blocked).k("longestDeviceQueue").v(k.io().maxQueueNow())
        .k("diskUtilisation").v(k.now() ? (double)k.io().get("DISK")->busyTicks / k.now() : 0.0).k("completed").v(k.metrics().completed).end();
    c.symptomsJson = w.str();
    c.logs["process"] = tail(k, {Category::PROCESS, Category::SCHED, Category::SYNC, Category::IPC}, 70);
    c.logs["memory"] = tail(k, {Category::MEMORY}, 70);
    c.logs["io"] = tail(k, {Category::IO, Category::ERROR}, 70);
    c.logs["interrupt"] = tail(k, {Category::INTERRUPT}, 70);
    return c;
}

const DetectiveCase& detectiveCase(int id) {
    static std::map<int, DetectiveCase> cache;
    id = std::clamp(id, 1, detectiveCaseCount());
    auto it = cache.find(id);
    if (it == cache.end()) it = cache.emplace(id, build(id)).first;
    return it->second;
}

std::string detectiveListJson() {
    json::Writer w;
    w.obj().k("choices").strs(detectiveChoices()).k("cases").arr();
    for (int i = 1; i <= detectiveCaseCount(); ++i) {
        const auto& c = detectiveCase(i);
        w.obj().k("id").v(i).k("title").v(c.title).k("difficulty").v(c.difficulty).end();
    }
    w.end().end();
    return w.str();
}

std::string detectiveCaseJson(int id) {
    const auto& c = detectiveCase(id);
    json::Writer w;
    w.obj().k("id").v(c.id).k("title").v(c.title).k("difficulty").v(c.difficulty).k("symptoms").raw(c.symptomsJson).k("hints").strs(c.hints).k("logs").obj();
    for (auto& [name, lines] : c.logs) w.k(name).strs(lines);
    w.end().end();
    return w.str();
}

std::string detectiveDiagnose(int id, const std::string& guess) {
    const auto& c = detectiveCase(id);
    bool ok = upper(guess) == c.answer;
    json::Writer w;
    w.obj().k("correct").v(ok).k("answer").v(ok ? c.answer : std::string()).k("explanation").v(ok ? c.explanation : std::string("Not quite — re-read the logs and the symptoms.")).end();
    return w.str();
}

}  // namespace owr

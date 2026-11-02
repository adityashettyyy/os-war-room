#include "failure_lab.hpp"

namespace owr::failure {

static Report done(Kernel& k, Report r) {
    k.log().log(k.now(), Category::FAILURE, -1, "FAILURE LAB ▸ " + r.title + ": " + r.summary);
    k.noteFailure(r.id, r.title + ": " + r.summary);
    return r;
}
static Report refuse(const std::string& id, const std::string& title, const std::string& why) {
    Report r; r.id = id; r.title = title; r.summary = why; r.ok = false;
    return r;
}
static std::string P(int pid) { return "P" + std::to_string(pid); }

Report forcePageFault(Kernel& k, int pid) {
    int target = pid < 0 ? k.runningPid() : pid;
    int page = k.forcePageFault(target);
    if (page < 0) return refuse("page_fault", "Force Page Fault", "no live process with a non-resident page was found");
    int who = target >= 0 ? target : -1;
    Report r;
    r.id = "page_fault"; r.title = "Force Page Fault"; r.ok = true;
    r.summary = "next memory reference of " + (who >= 0 ? P(who) : std::string("a ready process")) + " targets non-resident page " + std::to_string(page);
    if (who >= 0) r.pids.push_back(who);
    return done(k, r);
}

Report exhaustMemory(Kernel& k, int count) {
    count = std::clamp(count, 1, 60);
    Report r; r.id = "memory_pressure"; r.title = "Exhaust Memory"; r.ok = true;
    int pageKB = k.memory().pageSizeKB(), frames = k.memory().totalFrames();
    for (int i = 0; i < count; ++i) {
        ProcessSpec s;
        s.name = "mem-hog-" + std::to_string(i + 1);
        s.type = ProcType::CPU_BOUND; s.cpuBurst = 80; s.priority = 3 + (i % 6);
        s.memoryKB = std::max(pageKB * 4, k.config().physicalKB / 3);
        s.localityPages = std::max(2, frames / 6);
        int pid = k.createProcess(s);
        if (pid > 0) r.pids.push_back(pid);
    }
    r.summary = "spawned " + std::to_string(r.pids.size()) + " memory-hungry processes (working set ≈ " + std::to_string(std::max(2, frames / 6)) + " pages each, " + std::to_string(frames) + " frames total)";
    return done(k, r);
}

Report createDeadlock(Kernel& k, int n) {
    n = std::clamp(n, 2, 8);
    auto busy = [&](const std::string& name) {
        auto it = k.sync().mutexes.find(name);
        return it != k.sync().mutexes.end() && (it->second.owner >= 0 || !it->second.waiters.empty());
    };
    int base = 1;
    for (;; base += n) {
        bool clash = false;
        for (int i = 0; i < n; ++i) clash = clash || busy("R" + std::to_string(base + i));
        if (!clash) break;
    }
    Report r; r.id = "deadlock"; r.title = "Create Deadlock"; r.ok = true;
    std::string chain;
    for (int i = 0; i < n; ++i) {
        std::string mine = "R" + std::to_string(base + i), next = "R" + std::to_string(base + (i + 1) % n);
        ProcessSpec s;
        s.name = "deadlock-" + std::to_string(i + 1);
        s.priority = 3 + i; s.memoryKB = 16;
        s.script = assemble({"COMPUTE 1", "LOCK " + mine, "COMPUTE 1", "READ DISK 3", "LOCK " + next, "COMPUTE 2", "UNLOCK " + next, "UNLOCK " + mine});
        int pid = k.createProcess(s);
        if (pid > 0) { r.pids.push_back(pid); chain += (chain.empty() ? "" : ", ") + P(pid) + " holds " + mine + " wants " + next; }
    }
    r.summary = "circular wait armed: " + chain;
    return done(k, r);
}

Report crashProcess(Kernel& k, int pid, const std::string& reason) {
    int target = pid;
    if (target < 0) target = k.runningPid();
    if (target < 0) for (auto& [id, p] : k.processes()) if (p.alive() && p.state != State::NEW) { target = id; break; }
    if (target < 0 || !k.crashProcess(target, reason)) return refuse("crash", "Crash Process", "no live process to crash");
    Report r; r.id = "crash"; r.title = "Crash Process"; r.ok = true; r.pids.push_back(target);
    r.summary = P(target) + " faulted (" + reason + "); kernel error handler took over";
    return done(k, r);
}

Report floodIO(Kernel& k, int n, bool memoryRefs) {
    n = std::clamp(n, 1, 60);
    Report r; r.id = "io_flood"; r.title = "Flood I/O Queue"; r.ok = true;
    for (int i = 0; i < n; ++i) {
        ProcessSpec s;
        s.name = "io-flood-" + std::to_string(i + 1); s.priority = 4; s.memoryKB = 16; s.memoryRefs = memoryRefs;
        s.script = assemble({"COMPUTE 1", "READ DISK 6", "COMPUTE 1", "WRITE PRINTER 4 flood_" + std::to_string(i + 1) + ".txt", "COMPUTE 1", "READ DISK 6", "COMPUTE 1", "WRITE DISK 4"});
        int pid = k.createProcess(s);
        if (pid > 0) r.pids.push_back(pid);
    }
    r.summary = "spawned " + std::to_string(r.pids.size()) + " processes issuing disk and printer requests";
    return done(k, r);
}

Report deviceFailure(Kernel& k, const std::string& device, int ticks) {
    if (ticks <= 0) ticks = k.config().deviceRepairTicks;
    if (!k.failDevice(device, ticks)) return refuse("device_failure", "Device Failure", "unknown device or already offline");
    Report r; r.id = "device_failure"; r.title = "Device Failure"; r.ok = true;
    r.summary = upper(device) + " offline for " + std::to_string(ticks) + " ms; queued requests aborted with EIO";
    return done(k, r);
}

Report bufferStress(Kernel& k, int n) {
    n = std::clamp(n, 1, 40);
    Report r; r.id = "buffer_stress"; r.title = "Buffer Stress"; r.ok = true;
    std::vector<std::string> prog;
    for (int i = 0; i < 6; ++i) { prog.push_back("COMPUTE 1"); prog.push_back("WRITE DISK 5"); prog.push_back("WRITE NETWORK 4"); }
    for (int i = 0; i < n; ++i) {
        ProcessSpec s; s.name = "producer-" + std::to_string(i + 1); s.priority = 4; s.memoryKB = 16; s.script = assemble(prog);
        int pid = k.createProcess(s);
        if (pid > 0) r.pids.push_back(pid);
    }
    r.summary = "spawned " + std::to_string(r.pids.size()) + " bursty producers against " + std::to_string(k.config().diskBuffer) + "-slot device buffers";
    return done(k, r);
}

Report starvation(Kernel& k, bool configure) {
    if (configure) { k.setScheduler(SchedPolicy::PRIORITY); k.mutableConfig().aging = false; }
    Report r; r.id = "starvation"; r.title = "Starvation Scenario"; r.ok = true;
    ProcessSpec v; v.name = "victim"; v.priority = 9; v.cpuBurst = 20; v.memoryKB = 16;
    int vp = k.createProcess(v);
    if (vp > 0) r.pids.push_back(vp);
    for (int i = 0; i < 5; ++i) {
        ProcessSpec s; s.name = "vip-" + std::to_string(i + 1); s.priority = 1; s.cpuBurst = 60; s.memoryKB = 16;
        int pid = k.createProcess(s);
        if (pid > 0) r.pids.push_back(pid);
    }
    r.summary = P(vp) + " (priority 9) competes with five priority-1 CPU hogs" + (configure ? "; scheduler set to PRIORITY with aging OFF" : "");
    return done(k, r);
}

Report inject(Kernel& k, const std::string& action, const json::Value& p) {
    if (action == "page_fault") return forcePageFault(k, p.has("pid") ? p["pid"].integer(-1) : -1);
    if (action == "memory_pressure") return exhaustMemory(k, p["count"].integer(12));
    if (action == "deadlock") return createDeadlock(k, p["count"].integer(2));
    if (action == "crash") return crashProcess(k, p.has("pid") ? p["pid"].integer(-1) : -1, p["reason"].str("INJECTED FAULT"));
    if (action == "io_flood") return floodIO(k, p["count"].integer(12));
    if (action == "device_failure") return deviceFailure(k, p["device"].str("DISK"), p["ticks"].integer(-1));
    if (action == "buffer_stress") return bufferStress(k, p["count"].integer(6));
    if (action == "starvation") return starvation(k, p["configure"].boolean(true));
    return refuse(action, "Unknown failure", "no such failure action '" + action + "'");
}

std::vector<std::pair<std::string, std::string>> catalog() {
    return {{"page_fault", "Force Page Fault"}, {"memory_pressure", "Exhaust Memory"}, {"deadlock", "Create Deadlock"}, {"crash", "Crash Process"},
            {"io_flood", "Flood I/O Queue"}, {"device_failure", "Device Failure"}, {"buffer_stress", "Buffer Stress"}, {"starvation", "Starvation Scenario"}};
}

}  // namespace owr::failure

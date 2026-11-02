// JSON export of kernel state for the dashboard / API. Read-only; never mutates the kernel.
#include "kernel.hpp"

#include <climits>

namespace owr {

using json::Writer;

std::string Metrics::toJson() const {
    Writer w;
    w.obj().k("ticks").v(ticks).k("processes").v(processes).k("completed").v(completed).k("failed").v(failed).k("makespan").v(makespan)
        .k("cpuUtil").v(cpuUtil).k("avgWaiting").v(avgWaiting).k("avgTurnaround").v(avgTurnaround).k("avgResponse").v(avgResponse)
        .k("avgCompletion").v(avgCompletion).k("throughput").v(throughput)
        .k("ctxSwitches").v(ctxSwitches).k("pageFaults").v(pageFaults).k("memRefs").v(memRefs).k("replacements").v(replacements)
        .k("pfRate").v(pfRate).k("memUtil").v(memUtil).k("ioRequests").v(ioRequests).k("avgIoWait").v(avgIoWait).k("avgIoQueue").v(avgIoQueue)
        .k("lockWaitTicks").v(lockWaitTicks).k("blockEvents").v(blockEvents).k("deadlocks").v(deadlocks).k("syscalls").v(syscalls)
        .k("interrupts").v(interrupts).k("starvation").v(starvation).k("peakBlocked").v(peakBlocked);
    w.k("deviceUtil").obj();
    for (auto& [n, u] : deviceUtil) w.k(n).v(u);
    w.end().end();
    return w.str();
}

static void interruptJson(Writer& w, const Interrupt& i) {
    w.obj().k("id").v(i.id).k("type").v(str(i.type)).k("pid").v(i.pid).k("tick").v(i.tick).k("page").v(i.page).k("device").v(i.device)
        .k("cause").v(i.cause).k("handler").v(i.handler).k("action").v(i.action).k("quiet").v(i.quiet).k("sync").v(i.sync).end();
}

std::string Kernel::stateJson() const {
    Writer w;
    auto hs = health();
    bool anyRed = false;
    for (auto& h : hs) anyRed = anyRed || h.level == "RED";
    bool dl = false;
    for (auto& e : deadlocks_) dl = dl || !e.resolved;
    w.obj().k("tick").v(now_);
    w.k("status").v(dl || thrashing_ ? "CRITICAL" : anyRed ? "DEGRADED" : "OPERATIONAL");
    w.k("allDone").v(allDone());

    w.k("config").obj().k("scheduler").v(str(cfg_.sched)).k("quantum").v(cfg_.quantum).k("aging").v(cfg_.aging).k("replacement").v(str(cfg_.repl))
        .k("physicalKB").v(cfg_.physicalKB).k("pageSizeKB").v(cfg_.pageSizeKB).k("frames").v(mem_.totalFrames()).k("ctxSwitchCost").v(cfg_.ctxSwitchCost)
        .k("thrashingControl").v(cfg_.thrashingControl).k("autoDeadlockRecovery").v(cfg_.autoDeadlockRecovery).k("recovery").v(str(cfg_.recovery))
        .k("pageFaultLatency").v(cfg_.pageFaultLatency).k("seed").v((long long)cfg_.seed).end();

    // CPU
    const PCB* run = running_;
    w.k("cpu").obj().k("running").v(run ? run->pid : -1).k("util").v(windowCpuUtil()).k("utilTotal").v(now_ ? (double)busyTicks_ / now_ : 0.0)
        .k("ctxSwitches").v(ctxSwitches_).k("busyTicks").v(busyTicks_).k("idleTicks").v(idleTicks_).k("overheadTicks").v(overheadTicks_)
        .k("switchLeft").v(switchLeft_).k("slice").v(run ? sched_->timeSlice(*run) : 0).k("quantumUsed").v(run ? run->quantumUsed : 0);
    w.k("lastSwitch").obj().k("tick").v(lastSwitch_.tick).k("from").v(lastSwitch_.from).k("to").v(lastSwitch_.to).k("fromPc").v(lastSwitch_.fromPc).k("toPc").v(lastSwitch_.toPc).end();
    w.end();

    // processes
    w.k("processes").arr();
    for (auto& [pid, p] : procs_) {
        int resident = 0;
        if (auto* t = mem_.table(pid)) for (auto& e : *t) resident += e.present;
        w.obj().k("pid").v(pid).k("ppid").v(p.ppid).k("name").v(p.name).k("type").v(str(p.type)).k("state").v(str(p.state)).k("wait").v(str(p.wait))
            .k("waitTarget").v(p.waitTarget).k("priority").v(p.priority).k("effPriority").v(p.effPriority).k("arrival").v(p.arrival)
            .k("cpuBurst").v(p.cpuBurst).k("remaining").v(p.remaining()).k("pc").v(p.pc).k("vpages").v(p.vpages).k("resident").v(resident)
            .k("memoryKB").v(p.memoryKB).k("pageFaults").v(p.pageFaults).k("waiting").v(p.waitingTime).k("cpuTime").v(p.cpuTime).k("blocked").v(p.blockedTime)
            .k("level").v(p.level).k("starved").v(p.starved).k("exitCode").v(p.exitCode).k("exitReason").v(p.exitReason)
            .k("completion").v(p.completion).k("firstRun").v(p.firstRun);
        w.k("held").strs(sync_.heldBy(pid)).end();
    }
    w.end();
    w.k("readyQueue").ints(sched_->order());

    // memory
    w.k("memory").obj().k("total").v(mem_.totalFrames()).k("used").v(mem_.usedFrames()).k("pageSizeKB").v(mem_.pageSizeKB()).k("policy").v(str(cfg_.repl))
        .k("faults").v(faults_).k("refs").v(refs_).k("replacements").v(mem_.replacements()).k("pfRate").v(windowPfRate()).k("pfRateTotal").v(refs_ ? (double)faults_ / refs_ : 0.0)
        .k("util").v(memUtil()).k("thrashing").v(thrashing_).k("thrashEvents").v(thrashEvents_).k("suspensions").v(suspensions_);
    w.k("frames").arr();
    for (auto& f : mem_.frames()) {
        if (!f.used) { w.null(); continue; }
        w.obj().k("pid").v(f.pid).k("page").v(f.page).k("pinned").v(f.pinned).k("ref").v(f.ref).k("loadedAt").v(f.loadedAt).k("lastUsed").v(f.lastUsed).end();
    }
    w.end().end();

    // I/O
    w.k("io").obj().k("totalQueue").v(io_.totalQueue()).k("devices").arr();
    for (auto& d : io_.devices()) {
        w.obj().k("name").v(d.name).k("failed").v(d.failed).k("failedUntil").v(d.failedUntil).k("busy").v(d.cur.has_value())
            .k("util").v(now_ ? (double)d.busyTicks / now_ : 0.0).k("completed").v(d.completed).k("errors").v(d.errors).k("maxQueue").v(d.maxQueue)
            .k("queueLength").v(d.queueLength()).k("buffered").v(d.buffered).k("spooled").v(d.spooled).k("capacity").v(d.capacity).k("occupancy").v(d.occupancy)
            .k("fullEvents").v(d.fullEvents).k("producerBlocks").v(d.producerBlocks).k("consumerIdle").v(d.consumerIdleTicks).k("lostWrites").v(d.lostWrites);
        w.k("current");
        if (d.cur) w.obj().k("pid").v(d.cur->pid).k("write").v(d.cur->write).k("remaining").v(d.cur->remaining).k("duration").v(d.cur->duration).k("doc").v(d.cur->doc).end();
        else w.null();
        w.k("queue").arr();
        for (auto& r : d.queue) w.obj().k("pid").v(r.pid).k("write").v(r.write).k("duration").v(r.duration).k("doc").v(r.doc).end();
        w.end().k("parked").arr();
        for (auto& r : d.parked) w.obj().k("pid").v(r.pid).k("doc").v(r.doc).end();
        w.end().end();
    }
    w.end().end();

    // interrupts
    w.k("interrupts").obj().k("total").v(intc_.total()).k("counts").obj();
    for (int t = 0; t < 5; ++t) w.k(INT_NAMES[(size_t)t]).v(intc_.count((IntType)t));
    w.end().k("pending").arr();
    for (auto& i : intc_.pending()) interruptJson(w, i);
    w.end().k("recent").arr();
    size_t from = intc_.history().size() > 40 ? intc_.history().size() - 40 : 0;
    for (size_t k = from; k < intc_.history().size(); ++k) interruptJson(w, intc_.history()[k]);
    w.end().end();

    // synchronisation
    w.k("sync").obj().k("blockEvents").v(sync_.blockEvents()).k("mutexes").arr();
    for (auto& [n, m] : sync_.mutexes) { w.obj().k("name").v(n).k("owner").v(m.owner).k("acquisitions").v(m.acquisitions).k("contentions").v(m.contentions).k("waiters").ints(std::vector<int>(m.waiters.begin(), m.waiters.end())).end(); }
    w.end().k("semaphores").arr();
    for (auto& [n, s] : sync_.semaphores) { w.obj().k("name").v(n).k("value").v(s.value).k("initial").v(s.initial).k("blocks").v(s.blocks).k("waiters").ints(std::vector<int>(s.waiters.begin(), s.waiters.end())).end(); }
    w.end().k("rwlocks").arr();
    for (auto& [n, l] : sync_.rwlocks) {
        w.obj().k("name").v(n).k("writer").v(l.writer).k("readers").ints(std::vector<int>(l.readers.begin(), l.readers.end())).k("waiters").arr();
        for (auto& x : l.waiters) w.obj().k("pid").v(x.first).k("write").v(x.second).end();
        w.end().end();
    }
    w.end().k("edges").arr();
    for (auto& [a, bs] : sync_.waitForGraph()) for (int b : bs) w.arr().v(a).v(b).end();
    w.end().k("deadlocks").arr();
    for (auto& e : deadlocks_)
        w.obj().k("id").v(e.id).k("detectedAt").v(e.detectedAt).k("resolvedAt").v(e.resolvedAt).k("recoverAt").v(e.recoverAt).k("resolved").v(e.resolved)
            .k("pids").ints(e.pids).k("description").v(e.description).k("victim").v(e.victim).k("action").v(e.action).end();
    w.end().end();

    // IPC
    w.k("ipc").obj().k("queues").arr();
    for (auto& [n, q] : ipc_.queues) {
        w.obj().k("name").v(n).k("capacity").v(q.capacity).k("sent").v(q.sent).k("received").v(q.received).k("sendBlocks").v(q.sendBlocks).k("recvBlocks").v(q.recvBlocks)
            .k("recvWaiters").ints(std::vector<int>(q.recvWaiters.begin(), q.recvWaiters.end())).k("messages").arr();
        for (auto& m : q.msgs) w.obj().k("from").v(m.from).k("text").v(m.text).end();
        w.end().k("parked").arr();
        for (auto& m : q.sendWaiters) w.obj().k("from").v(m.from).k("text").v(m.text).end();
        w.end().end();
    }
    w.end().k("segments").arr();
    for (auto& [n, s] : ipc_.segments) {
        w.obj().k("name").v(n).k("reads").v(s.reads).k("writes").v(s.writes).k("data").ints(s.data).k("history").arr();
        size_t f2 = s.history.size() > 8 ? s.history.size() - 8 : 0;
        for (size_t k = f2; k < s.history.size(); ++k) { auto& a = s.history[k]; w.obj().k("tick").v(a.tick).k("pid").v(a.pid).k("index").v(a.index).k("value").v(a.value).k("write").v(a.write).end(); }
        w.end().end();
    }
    w.end().end();

    // health, counters, series, failures, crash dumps
    w.k("health").arr();
    for (auto& h : hs) w.obj().k("name").v(h.name).k("level").v(h.level).k("value").v(h.value).k("rule").v(h.rule).end();
    w.end();
    w.k("syscalls").obj();
    for (size_t k = 0; k < SYS_NAMES.size(); ++k) w.k(SYS_NAMES[k]).v(sysCounts_[k]);
    w.end();
    w.k("series").arr();
    size_t sf = series_.size() > 150 ? series_.size() - 150 : 0;
    for (size_t k = sf; k < series_.size(); ++k) {
        auto& s = series_[k];
        w.obj().k("t").v(s.tick).k("cpu").v(s.cpu).k("mem").v(s.mem).k("pf").v(s.pf).k("ready").v(s.ready).k("active").v(s.active).k("ioq").v(s.ioq).end();
    }
    w.end().k("failures").arr();
    for (auto& f : failures_) w.obj().k("tick").v(f.tick).k("id").v(f.id).k("summary").v(f.summary).end();
    w.end().k("crashDumps").arr();
    for (auto& d : dumps_) {
        w.obj().k("tick").v(d.tick).k("pid").v(d.pid).k("reason").v(d.reason).k("state").v(str(d.stateBefore)).k("pc").v(d.pc).k("lastInstr").v(d.lastInstr)
            .k("resident").v(d.residentPages).k("pendingIo").v(d.pendingIo).k("held").strs(d.heldResources).end();
    }
    w.end().k("lastLogId").v(log_.lastId());
    w.k("metrics").raw(metrics().toJson());
    w.end();
    return w.str();
}

std::string Kernel::processJson(int pid) const {
    const PCB* p = find(pid);
    if (!p) return "{\"error\":\"no such process\"}";
    Writer w;
    w.obj().k("pid").v(p->pid).k("ppid").v(p->ppid).k("name").v(p->name).k("type").v(str(p->type)).k("state").v(str(p->state)).k("wait").v(str(p->wait))
        .k("waitTarget").v(p->waitTarget).k("pc").v(p->pc);
    w.k("regs").ints({p->regs[0], p->regs[1], p->regs[2], p->regs[3]});
    w.k("priority").v(p->priority).k("effPriority").v(p->effPriority).k("arrival").v(p->arrival).k("cpuBurst").v(p->cpuBurst).k("ioBurst").v(p->ioBurst)
        .k("remaining").v(p->remaining()).k("level").v(p->level).k("tau").v(p->tau).k("memoryKB").v(p->memoryKB).k("vpages").v(p->vpages)
        .k("firstRun").v(p->firstRun).k("completion").v(p->completion).k("waiting").v(p->waitingTime).k("cpuTime").v(p->cpuTime).k("blocked").v(p->blockedTime)
        .k("pageFaults").v(p->pageFaults).k("memRefs").v(p->memRefs).k("dispatches").v(p->dispatches).k("syscalls").v(p->syscalls).k("ioRequests").v(p->ioRequests)
        .k("ioErrors").v(p->ioErrors).k("rollbacks").v(p->rollbacks).k("starved").v(p->starved).k("exitCode").v(p->exitCode).k("exitReason").v(p->exitReason);
    w.k("held").strs(sync_.heldBy(pid)).k("waitingOn").v(sync_.waitingOn(pid).empty() ? ipc_.waitingOn(pid) : sync_.waitingOn(pid));
    w.k("ipc").obj().k("sent").v(p->msgSent).k("received").v(p->msgRecv).k("inbox").v(p->inbox).end();
    w.k("pageTable").arr();
    if (auto* t = mem_.table(pid)) for (size_t k = 0; k < t->size(); ++k) {
        w.obj().k("page").v((int)k).k("frame").v((*t)[k].present ? (*t)[k].frame : -1).k("present").v((*t)[k].present).end();
    }
    w.end().k("program").arr();
    size_t lo = p->pc > 12 ? (size_t)p->pc - 12 : 0, hi = std::min(p->program.size(), lo + 40);
    for (size_t k = lo; k < hi; ++k) w.obj().k("i").v((int)k).k("text").v(p->program[k].text()).k("current").v((int)k == p->pc).end();
    w.end().end();
    return w.str();
}

std::string Kernel::logJson(long since, const std::set<Category>& cats, int pid, size_t limit) const {
    Writer w;
    w.obj().k("last").v(log_.lastId()).k("events").arr();
    for (auto& e : log_.query(since, cats, pid, limit))
        w.obj().k("id").v(e.id).k("tick").v(e.tick).k("t").v(Logger::stamp(e.tick)).k("cat").v(str(e.cat)).k("pid").v(e.pid).k("msg").v(e.msg).end();
    w.end().end();
    return w.str();
}

}  // namespace owr

#include "kernel.hpp"

#include <climits>
#include <cmath>

namespace owr {

Kernel::Kernel(Config cfg)
    : cfg_(cfg), sched_(makeScheduler(cfg.sched, cfg_)), mem_(cfg.frames(), cfg.pageSizeKB, cfg.repl), io_(cfg_) {
    ipc_.defaultCapacity = cfg.msgQueueCapacity;
    installHandlers();
    log_.log(0, Category::SYSTEM, -1,
             std::string("KERNEL BOOT: scheduler=") + str(cfg_.sched) + " quantum=" + std::to_string(cfg_.quantum) + " replacement=" + str(cfg_.repl) +
                 " memory=" + std::to_string(cfg_.physicalKB) + "KB/" + std::to_string(cfg_.pageSizeKB) + "KB pages (" + std::to_string(mem_.totalFrames()) + " frames)");
}

// ====================================================================================
// Main loop: one call == one simulated millisecond
// ====================================================================================
void Kernel::tick() {
    tickStat_ = {};
    for (size_t k = 0; k < pendingNew_.size();) {              // 1. admit processes whose arrival time has come
        PCB* p = find(pendingNew_[k]);
        if (!p || !p->alive()) { pendingNew_.erase(pendingNew_.begin() + (long)k); continue; }
        if (p->arrival <= now_) { admit(*p); pendingNew_.erase(pendingNew_.begin() + (long)k); }
        else ++k;
    }
    hardwareStep();                                            // 2. devices, sleepers, page-ins, timers raise interrupts
    intc_.service();                                           // 3. kernel services pending interrupts by priority
    for (size_t k = 0; k < deadlocks_.size(); ++k) {           // 4. scheduled deadlock recovery
        DeadlockEvent& ev = deadlocks_[k];
        if (!ev.resolved && ev.recoverAt >= 0 && now_ >= ev.recoverAt) { ev.recoverAt = -1; recoverDeadlock(ev); }
    }
    dispatchStep();                                            // 5. choose who owns the CPU
    executeTick();                                             // 6. run one instruction
    postTick();                                                // 7. accounting
    ++now_;
}

void Kernel::hardwareStep() {
    for (auto& [pid, p] : procs_)
        if (p.state == State::WAITING && p.wait == WaitReason::SLEEP && p.wakeAt <= now_) {
            p.wakeAt = INT_MAX;                                 // one wake-up interrupt per sleep
            Interrupt i; i.type = IntType::TIMER; i.pid = pid; i.tick = now_; i.cause = "sleep";
            intc_.raise(i, false);
        }
    for (size_t k = 0; k < deferred_.size();) {                // faults that found every frame pinned
        if (deferred_[k].readyAt <= now_) {
            PageLoad d = deferred_[k];
            deferred_.erase(deferred_.begin() + (long)k);
            PCB* p = find(d.pid);
            if (p && p->alive() && p->state == State::WAITING && p->wait == WaitReason::PAGE_FAULT) {
                Interrupt i; i.type = IntType::PAGE_FAULT; i.pid = d.pid; i.page = d.page; i.tick = now_; i.cause = "retry"; i.b = 1;
                intc_.raise(i, true);
            }
        } else ++k;
    }
    for (size_t k = 0; k < pageLoads_.size();) {               // backing-store reads that finished
        if (pageLoads_[k].readyAt <= now_) {
            Interrupt i; i.type = IntType::IO_COMPLETE; i.pid = pageLoads_[k].pid; i.page = pageLoads_[k].page; i.tick = now_;
            i.device = "SWAP"; i.cause = "page-in complete"; i.a = 1;
            pageLoads_.erase(pageLoads_.begin() + (long)k);
            intc_.raise(i, false);
        } else ++k;
    }
    std::vector<Device::Event> ev;
    for (auto& d : io_.devices()) {
        ev.clear();
        d.advance(now_, ev);
        for (auto& e : ev) {
            switch (e.kind) {
                case Device::Event::STARTED:
                    log_.log(now_, Category::IO, e.req.pid, d.name + ": start " + (e.req.write ? "WRITE" : "READ") + " for " + pname(e.req.pid) + " (" +
                                                                 std::to_string(e.req.duration) + " ms, waited " + std::to_string(now_ - e.req.enq) + ")" +
                                                                 (d.spooled ? " printing " + e.req.doc : ""));
                    break;
                case Device::Event::DONE:
                    if (e.req.pid < 0 && e.req.sync) break;     // orphaned request of a dead process
                    { Interrupt i; i.type = IntType::IO_COMPLETE; i.pid = e.req.pid; i.tick = now_; i.device = d.name; i.cause = "operation complete";
                      i.a = e.req.sync ? 1 : 0; i.s = e.req.doc; intc_.raise(i, false); }
                    break;
                case Device::Event::PRODUCER_UNBLOCKED:
                    { Interrupt i; i.type = IntType::IO_COMPLETE; i.pid = e.req.pid; i.tick = now_; i.device = d.name; i.cause = "buffer-space"; i.a = 1;
                      intc_.raise(i, false); }
                    break;
                case Device::Event::RECOVERED:
                    log_.log(now_, Category::IO, -1, d.name + " back ONLINE after repair");
                    break;
            }
        }
    }
    if (now_ > 0 && cfg_.timerPeriod > 0 && now_ % cfg_.timerPeriod == 0) {
        Interrupt i; i.type = IntType::TIMER; i.tick = now_; i.cause = "periodic"; i.quiet = true;
        intc_.raise(i, false);
    }
}

void Kernel::dispatchStep() {
    if (running_ && switchLeft_ == 0) {
        bool expired = needResched_ && quantumExpired_;
        bool preempt = needResched_ || sched_->shouldPreempt(*running_);
        if (preempt) preemptRunning(expired);
    }
    needResched_ = false;
    quantumExpired_ = false;
    if (!running_) if (PCB* n = sched_->pickNext()) startRunning(*n);
}

void Kernel::preemptRunning(bool quantumExpired) {
    PCB* p = running_;
    running_ = nullptr;
    if (quantumExpired) {
        sched_->onQuantumExpired(*p);
        log_.log(now_, Category::SCHED, p->pid, pname(p->pid) + " time slice expired" + (cfg_.sched == SchedPolicy::MLFQ ? " → demoted to level " + std::to_string(p->level) : ""));
    } else log_.log(now_, Category::SCHED, p->pid, pname(p->pid) + " preempted by a better ready process");
    p->state = State::READY;
    p->readyWait = 0;
    sched_->enqueue(p);
}

void Kernel::startRunning(PCB& n) {
    n.state = State::RUNNING;
    n.readyWait = 0;
    n.effPriority = n.priority;
    n.starved = false;
    n.quantumUsed = 0;
    ++n.dispatches;
    running_ = &n;
    if (lastRunPid_ >= 0 && lastRunPid_ != n.pid) {
        ++ctxSwitches_;
        switchLeft_ = cfg_.ctxSwitchCost;
        const PCB* from = find(lastRunPid_);
        lastSwitch_ = {now_, lastRunPid_, n.pid, from ? from->pc : 0, n.pc};
        log_.log(now_, Category::SCHED, n.pid,
                 "CONTEXT SWITCH " + pname(lastRunPid_) + " → " + pname(n.pid) + ": save " + pname(lastRunPid_) + " context (PC=" + std::to_string(lastSwitch_.fromPc) +
                     ") → update PCB → " + str(cfg_.sched) + " selects " + pname(n.pid) + " → load context (PC=" + std::to_string(n.pc) + ")");
    } else {
        switchLeft_ = 0;
        log_.log(now_, Category::SCHED, n.pid, std::string("Scheduler(") + str(cfg_.sched) + ") → " + pname(n.pid) + (lastRunPid_ == n.pid ? " (context already loaded)" : ""));
    }
    lastRunPid_ = n.pid;
    log_.log(now_, Category::PROCESS, n.pid, pname(n.pid) + " → RUNNING");
}

void Kernel::retire(PCB& p) {
    ++p.pc; ++p.cpuTime; ++p.burstRun; ++p.quantumUsed;
    ++busyTicks_; ++tickStat_.busy;
}

void Kernel::executeTick() {
    if (!running_) { ++idleTicks_; return; }
    PCB& p = *running_;
    if (switchLeft_ > 0) { --switchLeft_; ++overheadTicks_; return; }   // context switch consumes the CPU
    if (p.firstRun < 0) p.firstRun = now_;
    const Instr in = p.program[(size_t)p.pc];

    if (in.op == Op::COMPUTE && in.page < 0 && !p.retryPending && p.forcePage < 0) {
        retire(p);                                  // pure computation: no memory reference
    } else if (in.op == Op::COMPUTE) {
        int page;
        if (p.retryPending) page = p.pinnedPage;
        else if (p.forcePage >= 0) { page = p.forcePage; p.forcePage = -1; }
        else page = in.page;
        if (page < 0 || page >= p.vpages) {
            raiseError(p, "SEGMENTATION FAULT: page " + std::to_string(page) + " outside " + std::to_string(p.vpages) + "-page address space");
            return;
        }
        bool retry = p.retryPending;
        if (!retry) { ++refs_; ++p.memRefs; ++tickStat_.refs; }
        if (!mem_.present(p.pid, page)) {
            ++faults_; ++p.pageFaults; ++tickStat_.faults; ++overheadTicks_;
            p.retryPending = false; p.pinnedPage = -1;
            Interrupt i; i.type = IntType::PAGE_FAULT; i.pid = p.pid; i.page = page; i.tick = now_; i.cause = "page not present";
            intc_.raise(i, true);                                      // handler blocks the process
            return;
        }
        if (retry) { mem_.unpin(p.pid, page); p.retryPending = false; p.pinnedPage = -1; }
        mem_.touch(p.pid, page, now_);
        ++p.regs[0]; p.regs[1] = page; p.regs[2] = in.offset;
        retire(p);
    } else {
        executeSyscallInstr(p, in);
    }

    if (running_ == &p && p.state == State::RUNNING) {
        if (p.pc >= (int)p.program.size()) { sys(p, Sys::EXIT); return; }
        int slice = sched_->timeSlice(p);
        if (slice > 0 && p.quantumUsed >= slice) {
            if (sched_->size() > 0) {
                Interrupt i; i.type = IntType::TIMER; i.pid = p.pid; i.tick = now_; i.cause = "quantum";
                intc_.raise(i, false);
            } else p.quantumUsed = 0;
        }
    }
}

void Kernel::postTick() {
    window_.push_back(tickStat_);
    if ((int)window_.size() > kWindow) window_.pop_front();
    sched_->tick(now_);
    int blocked = 0;
    for (auto& [pid, p] : procs_) {
        if (p.state == State::READY) ++p.waitingTime;
        else if (p.state == State::WAITING) {
            ++p.blockedTime; ++blocked;
            if (p.wait == WaitReason::LOCK || p.wait == WaitReason::SEMAPHORE) { ++p.lockWaitTime; ++lockWaitTicks_; }
        }
    }
    blockedSum_ += blocked;
    peakBlocked_ = std::max(peakBlocked_, blocked);
    memUtilSum_ += mem_.usedFrames();
    if (now_ % 5 == 0) {
        SeriesPoint s;
        s.tick = now_; s.cpu = windowCpuUtil(); s.mem = memUtil(); s.pf = windowPfRate();
        s.ready = (int)sched_->size(); s.active = aliveCount(); s.ioq = io_.totalQueue();
        series_.push_back(s);
        if (series_.size() > 400) series_.pop_front();
    }
}

void Kernel::housekeeping() {
    detectDeadlocks();
    updateThrashing();
    scanStarvation();
}

// ====================================================================================
// Process lifecycle
// ====================================================================================
int Kernel::createProcess(ProcessSpec s) {
    if (aliveCount() + (int)pendingNew_.size() >= cfg_.maxProcesses) {
        log_.log(now_, Category::ERROR, -1, "create_process failed: process table full (" + std::to_string(cfg_.maxProcesses) + ")");
        return -1;
    }
    int pid = nextPid_;
    PCB p;
    p.pid = pid;
    p.ppid = s.ppid;
    p.name = s.name.empty() ? pname(pid) : s.name;
    p.priority = p.effPriority = std::clamp(s.priority, 0, 9);
    int pageKB = mem_.pageSizeKB();
    p.memoryKB = std::max(pageKB, s.memoryKB);
    p.vpages = (p.memoryKB + pageKB - 1) / pageKB;
    p.localityPages = s.localityPages;
    std::vector<Instr> script = s.script.empty() ? generateScript(s, pid) : s.script;
    if (script.empty()) { log_.log(now_, Category::ERROR, -1, "create_process failed: empty program"); return -1; }
    ++nextPid_;
    p.type = s.script.empty() ? s.type : ProcType::CUSTOM;
    p.program = linkProgram(script, p.vpages, p.localityPages, pageKB, cfg_.seed * 2654435761u + (uint32_t)pid * 40503u, s.memoryRefs);
    p.cpuBurst = (int)p.program.size();
    for (auto& in : script) if (in.op == Op::READ || in.op == Op::WRITE) p.ioBurst += in.a;
    p.arrival = s.arrival < 0 ? now_ : s.arrival;
    p.state = State::NEW;
    mem_.createSpace(pid, p.vpages);
    procs_[pid] = p;
    PCB& ref = procs_[pid];
    log_.log(now_, Category::PROCESS, pid,
             pname(pid) + " created: " + str(ref.type) + ", CPU burst " + std::to_string(ref.cpuBurst) + " ms, I/O " + std::to_string(ref.ioBurst) + " ms, priority " +
                 std::to_string(ref.priority) + ", " + std::to_string(ref.memoryKB) + " KB (" + std::to_string(ref.vpages) + " pages) → NEW");
    if (ref.arrival <= now_) admit(ref);
    else pendingNew_.push_back(pid);
    return pid;
}

void Kernel::admit(PCB& p) { makeReady(p); }

void Kernel::makeReady(PCB& p) {
    p.state = State::READY;
    p.wait = WaitReason::NONE;
    p.waitTarget.clear();
    p.readyWait = 0;
    p.effPriority = p.priority;
    sched_->enqueue(&p);
    log_.log(now_, Category::PROCESS, p.pid, pname(p.pid) + " → READY");
}

void Kernel::wake(PCB& p) {
    if (p.state != State::WAITING) return;
    p.wait = WaitReason::NONE;
    p.waitTarget.clear();
    if (p.pc >= (int)p.program.size()) { finish(p, "exit()", 0, now_); return; }   // its last instruction was the blocking one
    makeReady(p);
}

void Kernel::block(PCB& p, WaitReason r, const std::string& target) {
    if (running_ == &p) {
        if (r != WaitReason::PAGE_FAULT) { sched_->onBurstEnd(p); p.burstRun = 0; }
        running_ = nullptr;
        switchLeft_ = 0;
    }
    p.state = State::WAITING;
    p.wait = r;
    p.waitTarget = target;
    log_.log(now_, Category::PROCESS, p.pid, pname(p.pid) + " → WAITING (" + str(r) + (target.empty() ? "" : ": " + target) + ")");
}

void Kernel::finish(PCB& p, const std::string& reason, int code, int completionTick) {
    if (!p.alive()) return;
    if (running_ == &p) { sched_->onBurstEnd(p); running_ = nullptr; switchLeft_ = 0; }
    sched_->remove(&p);
    p.state = State::TERMINATED;
    p.wait = WaitReason::NONE;
    p.completion = completionTick;
    p.exitCode = code;
    p.exitReason = reason;
    std::vector<int> woke = sync_.releaseAll(p.pid);
    ipc_.removePid(p.pid);
    io_.removePid(p.pid);
    intc_.dropPending(p.pid);
    pageLoads_.erase(std::remove_if(pageLoads_.begin(), pageLoads_.end(), [&](const PageLoad& l) { return l.pid == p.pid; }), pageLoads_.end());
    deferred_.erase(std::remove_if(deferred_.begin(), deferred_.end(), [&](const PageLoad& l) { return l.pid == p.pid; }), deferred_.end());
    int freed = mem_.release(p.pid);
    log_.log(now_, Category::PROCESS, p.pid, pname(p.pid) + " → TERMINATED (" + reason + "), " + std::to_string(freed) + " frames freed" +
                                                 (woke.empty() ? "" : ", " + std::to_string(woke.size()) + " waiter(s) released"));
    for (int w : woke) if (PCB* q = find(w)) wake(*q);
}

void Kernel::suspend(PCB& p) {
    if (running_ == &p) { running_ = nullptr; switchLeft_ = 0; }
    sched_->remove(&p);
    p.state = State::SUSPENDED;
    p.retryPending = false;
    p.pinnedPage = -1;
    int n = mem_.evictAll(p.pid);
    ++suspensions_;
    lastSuspendAt_ = now_;
    log_.log(now_, Category::MEMORY, p.pid, "THRASHING CONTROL: " + pname(p.pid) + " swapped out (priority " + std::to_string(p.priority) + "), " + std::to_string(n) + " frames reclaimed");
}

int Kernel::aliveCount() const {
    int n = 0;
    for (auto& [pid, p] : procs_) if (p.alive() && p.state != State::NEW) ++n;
    return n;
}
bool Kernel::allDone() const {
    for (auto& [pid, p] : procs_) if (p.alive()) return false;
    return true;
}
std::map<State, int> Kernel::stateCounts() const {
    std::map<State, int> m;
    for (auto& [pid, p] : procs_) ++m[p.state];
    return m;
}

// ====================================================================================
// Configuration
// ====================================================================================
void Kernel::setScheduler(SchedPolicy p, int quantum) {
    auto old = sched_->drain();
    cfg_.sched = p;
    if (quantum > 0) cfg_.quantum = quantum;
    sched_ = makeScheduler(p, cfg_);
    for (PCB* x : old) { x->level = 0; x->effPriority = x->priority; sched_->enqueue(x); }
    log_.log(now_, Category::SYSTEM, -1, std::string("Scheduler switched to ") + str(p) + " (quantum " + std::to_string(cfg_.quantum) + " ms)");
}
void Kernel::setReplacement(ReplPolicy p) {
    cfg_.repl = p;
    mem_.setPolicy(p);
    log_.log(now_, Category::SYSTEM, -1, std::string("Page replacement switched to ") + str(p));
}

// ====================================================================================
// Interrupt handlers
// ====================================================================================
void Kernel::installHandlers() {
    intc_.on(IntType::TIMER, "TimerHandler", [this](Interrupt& i) { onTimer(i); });
    intc_.on(IntType::PAGE_FAULT, "PageFaultHandler", [this](Interrupt& i) { onPageFault(i); });
    intc_.on(IntType::IO_COMPLETE, "IoCompletionHandler", [this](Interrupt& i) { onIoComplete(i); });
    intc_.on(IntType::ERROR, "KernelErrorHandler", [this](Interrupt& i) { onError(i); });
    intc_.on(IntType::SYSCALL, "SupervisorCallDispatcher", [this](Interrupt& i) { onSyscall(i); });
}

void Kernel::onTimer(Interrupt& i) {
    PCB* p = find(i.pid);
    if (i.cause == "quantum") {
        if (p && p == running_) {
            needResched_ = true;
            quantumExpired_ = true;
            i.action = "Save context → update PCB → invoke scheduler";
            log_.log(now_, Category::INTERRUPT, i.pid, "TIMER INTERRUPT: " + pname(i.pid) + " used its " + std::to_string(sched_->timeSlice(*p)) + " ms slice → TimerHandler → scheduler");
        } else i.action = "Stale timer (process no longer running) — ignored";
    } else if (i.cause == "sleep") {
        if (p && p->state == State::WAITING && p->wait == WaitReason::SLEEP) {
            i.action = "Sleep expired → READY";
            log_.log(now_, Category::INTERRUPT, i.pid, "TIMER INTERRUPT: " + pname(i.pid) + " sleep expired");
            wake(*p);
        }
    } else {
        i.action = "Housekeeping: deadlock scan, thrashing check, starvation scan";
        housekeeping();
    }
}

void Kernel::onPageFault(Interrupt& i) {
    PCB* p = find(i.pid);
    if (!p || !p->alive()) return;
    const bool retry = i.b == 1;
    if (!retry) log_.log(now_, Category::INTERRUPT, p->pid, "PAGE FAULT: " + pname(p->pid) + " referenced page " + std::to_string(i.page) + " (not present) → PageFaultHandler");
    Oracle oracle = [this](const Frame& f) { return nextUse(f); };
    auto r = mem_.load(p->pid, i.page, now_, oracle);
    if (!r.ok) {
        block(*p, WaitReason::PAGE_FAULT, "page " + std::to_string(i.page) + " (all frames busy, retrying)");
        deferred_.push_back({p->pid, i.page, now_ + 2});
        i.action = "No evictable frame → retry in 2 ms";
        return;
    }
    if (r.evicted) {
        log_.log(now_, Category::MEMORY, p->pid, std::string("VICTIM SELECTION (") + str(cfg_.repl) + "): " + pname(r.victimPid) + " page " + std::to_string(r.victimPage) + " evicted from frame " + std::to_string(r.frame));
    }
    log_.log(now_, Category::MEMORY, p->pid, "PAGE LOAD: " + pname(p->pid) + " page " + std::to_string(i.page) + " → FRAME " + std::to_string(r.frame) + " (page table updated, " + std::to_string(cfg_.pageFaultLatency) + " ms backing-store read)");
    p->pinnedPage = i.page;
    p->retryPending = true;
    block(*p, WaitReason::PAGE_FAULT, "page " + std::to_string(i.page));
    pageLoads_.push_back({p->pid, i.page, now_ + 1 + cfg_.pageFaultLatency});
    i.action = r.evicted ? "Select victim → page load → update page tables → resume" : "Allocate free frame → page load → update page table → resume";
}

void Kernel::onIoComplete(Interrupt& i) {
    PCB* p = find(i.pid);
    if (i.device == "SWAP") {
        if (p && p->state == State::WAITING && p->wait == WaitReason::PAGE_FAULT) {
            log_.log(now_, Category::INTERRUPT, i.pid, "I/O COMPLETE (swap): page " + std::to_string(i.page) + " resident → " + pname(i.pid) + " READY");
            wake(*p);
        }
        i.action = "Mark page present → process READY";
    } else if (i.cause == "buffer-space") {
        if (p && p->state == State::WAITING && p->wait == WaitReason::BUFFER_FULL) {
            log_.log(now_, Category::IO, i.pid, i.device + " buffer slot freed → " + pname(i.pid) + " unblocked (producer resumes)");
            wake(*p);
        }
        i.action = "Buffer slot freed → blocked producer READY";
    } else if (i.a == 1) {
        if (p && p->state == State::WAITING && p->wait == WaitReason::IO) {
            log_.log(now_, Category::INTERRUPT, i.pid, "I/O INTERRUPT: " + i.device + " complete → " + pname(i.pid) + " READY");
            wake(*p);
        }
        i.action = "Device done → blocked process READY";
    } else {
        log_.log(now_, Category::IO, i.pid, i.device + ": buffered write drained" + (i.s.empty() ? "" : " (" + i.s + ")"));
        i.action = "Buffered/spooled write drained, slot freed";
    }
}

void Kernel::raiseError(PCB& p, const std::string& cause) {
    Interrupt i; i.type = IntType::ERROR; i.pid = p.pid; i.tick = now_; i.cause = cause;
    intc_.raise(i, true);
}

void Kernel::onError(Interrupt& i) {
    if (!i.device.empty()) { i.action = "Abort queued requests → return EIO → device offline"; return; }
    PCB* p = find(i.pid);
    if (!p || !p->alive()) return;
    log_.log(now_, Category::ERROR, p->pid, "ERROR INTERRUPT: " + pname(p->pid) + " — " + i.cause + " → KernelErrorHandler");
    CrashDump d;
    d.tick = now_; d.pid = p->pid; d.name = p->name; d.reason = i.cause; d.stateBefore = p->state; d.pc = p->pc;
    for (int k = 0; k < 4; ++k) d.regs[k] = p->regs[k];
    d.lastInstr = p->pc > 0 ? p->program[(size_t)p->pc - 1].text() : "";
    d.heldResources = sync_.heldBy(p->pid);
    if (auto* t = mem_.table(p->pid)) for (auto& e : *t) d.residentPages += e.present;
    for (auto& dev : io_.devices()) for (auto& r : dev.queue) if (r.pid == p->pid) ++d.pendingIo;
    dumps_.push_back(d);
    if (dumps_.size() > 20) dumps_.pop_front();
    log_.log(now_, Category::ERROR, p->pid, "Diagnostics saved: PC=" + std::to_string(d.pc) + ", " + std::to_string(d.residentPages) + " resident pages, " +
                                                std::to_string(d.heldResources.size()) + " held resource(s), last instr \"" + d.lastInstr + "\"");
    bool wasRunning = running_ == p;
    finish(*p, i.cause, -1, now_ + (wasRunning ? 1 : 0));
    log_.log(now_, Category::ERROR, p->pid, "Resources released (frames, locks, queued I/O, IPC) → scheduler continues with remaining processes");
    i.action = "Save diagnostics → release resources → terminate → scheduler continues";
}

bool Kernel::crashProcess(int pid, const std::string& reason) {
    PCB* p = find(pid);
    if (!p || !p->alive()) return false;
    raiseError(*p, reason);
    return true;
}

bool Kernel::failDevice(const std::string& name, int ticks) {
    Device* d = io_.get(upper(name));
    if (!d || d->failed) return false;
    auto aborted = d->fail(now_, ticks);
    log_.log(now_, Category::ERROR, -1, "DEVICE FAILURE: " + d->name + " offline for " + std::to_string(ticks) + " ms; " + std::to_string(aborted.size()) + " request(s) aborted");
    Interrupt i; i.type = IntType::ERROR; i.tick = now_; i.device = d->name; i.cause = "device failure";
    intc_.raise(i, true);
    for (auto& r : aborted) {
        if (!r.sync || r.pid < 0) continue;
        PCB* p = find(r.pid);
        if (p && p->state == State::WAITING && (p->wait == WaitReason::IO || p->wait == WaitReason::BUFFER_FULL)) {
            ++p->ioErrors; p->regs[3] = -1;
            log_.log(now_, Category::IO, p->pid, pname(p->pid) + ": I/O error (EIO) on " + d->name + " → returned to user program");
            wake(*p);
        }
    }
    return true;
}

int Kernel::forcePageFault(int pid) {
    PCB* p = pid < 0 ? running_ : find(pid);
    if (!p || !p->alive()) {
        for (auto& [id, q] : procs_) if (q.state == State::READY || q.state == State::RUNNING) { p = &q; break; }
    }
    if (!p || !p->alive() || p->state == State::SUSPENDED) return -1;
    for (int k = 0; k < p->vpages; ++k) {
        int page = (p->pc * 7 + k) % p->vpages;
        if (!mem_.present(p->pid, page)) {
            p->forcePage = page;
            log_.log(now_, Category::FAILURE, p->pid, "FAILURE LAB: " + pname(p->pid) + " will next reference page " + std::to_string(page) + " (not resident)");
            return page;
        }
    }
    return -1;
}

long Kernel::nextUse(const Frame& f) const {
    const PCB* p = find(f.pid);
    if (!p) return INT_MAX;
    for (size_t j = (size_t)p->pc; j < p->program.size(); ++j)
        if (p->program[j].op == Op::COMPUTE && p->program[j].page == f.page) return (long)(j - (size_t)p->pc);
    return INT_MAX;
}

// ====================================================================================
// System calls (supervisor calls)
// ====================================================================================
int Kernel::sys(PCB& p, Sys nr, int a, int b, const std::string& s, const std::string& s2) {
    ++p.syscalls;
    Interrupt i; i.type = IntType::SYSCALL; i.pid = p.pid; i.tick = now_; i.a = (int)nr; i.b = a; i.c = b; i.s = s; i.s2 = s2; i.cause = "trap";
    sysResult_ = 0;
    intc_.raise(i, true);
    p.regs[3] = sysResult_;
    return sysResult_;
}

void Kernel::executeSyscallInstr(PCB& p, const Instr& in) {
    retire(p);
    switch (in.op) {
        case Op::READ: sys(p, Sys::READ, in.a, 0, in.s); break;
        case Op::WRITE: sys(p, Sys::WRITE, in.a, 0, in.s, in.s2); break;
        case Op::SLEEP: sys(p, Sys::SLEEP, in.a); break;
        case Op::LOCK: sys(p, Sys::LOCK, 0, 0, in.s); break;
        case Op::UNLOCK: sys(p, Sys::UNLOCK, 0, 0, in.s); break;
        case Op::WAIT: sys(p, Sys::WAIT, 0, 0, in.s); break;
        case Op::SIGNAL: sys(p, Sys::SIGNAL, 0, 0, in.s); break;
        case Op::RDLOCK: sys(p, Sys::RDLOCK, 0, 0, in.s); break;
        case Op::WRLOCK: sys(p, Sys::WRLOCK, 0, 0, in.s); break;
        case Op::RWUNLOCK: sys(p, Sys::RW_UNLOCK, 0, 0, in.s); break;
        case Op::SEND: sys(p, Sys::SEND_MESSAGE, 0, 0, in.s, in.s2); break;
        case Op::RECV: sys(p, Sys::RECEIVE_MESSAGE, 0, 0, in.s); break;
        case Op::SHMR: sys(p, Sys::SHM_READ, in.a, 0, in.s); break;
        case Op::SHMW: sys(p, Sys::SHM_WRITE, in.a, in.b, in.s); break;
        case Op::ALLOC: sys(p, Sys::ALLOCATE_MEMORY, in.a); break;
        case Op::FREE: sys(p, Sys::FREE_MEMORY, in.a); break;
        case Op::FORK: sys(p, Sys::CREATE_PROCESS, in.a); break;
        case Op::CRASH: raiseError(p, in.s2.empty() ? "ILLEGAL INSTRUCTION" : in.s2); break;
        case Op::COMPUTE: break;
    }
}

void Kernel::doIo(PCB& p, const std::string& dev, bool write, int duration, const std::string& doc) {
    Device* d = io_.get(dev);
    if (!d) { sysResult_ = -1; ++p.ioErrors; log_.log(now_, Category::ERROR, p.pid, pname(p.pid) + ": no such device " + dev); return; }
    IORequest r;
    r.id = nextIoId_++; r.pid = p.pid; r.write = write; r.duration = std::max(1, duration);
    r.doc = (doc.empty() && d->name == "PRINTER") ? "doc_" + std::to_string(p.pid) + ".txt" : doc;
    ++p.ioRequests;
    const std::string op = write ? "WRITE" : "READ";
    switch (d->submit(r, now_)) {
        case Device::Submit::ASYNC_ACCEPTED:
            if (d->spooled) log_.log(now_, Category::IO, p.pid, pname(p.pid) + " → PRINTER spooled \"" + r.doc + "\" (spool " + std::to_string(d->occupancy) + "/" + std::to_string(d->capacity) + "), process continues");
            else log_.log(now_, Category::IO, p.pid, pname(p.pid) + " → " + d->name + " buffered write (buffer " + std::to_string(d->occupancy) + "/" + std::to_string(d->capacity) + "), process continues");
            break;
        case Device::Submit::QUEUED_BLOCKING:
            log_.log(now_, Category::IO, p.pid, pname(p.pid) + " → " + d->name + " " + op + " (" + std::to_string(r.duration) + " ms), device queue " + std::to_string(d->queueLength()));
            block(p, WaitReason::IO, d->name);
            break;
        case Device::Submit::PARKED_FULL:
            log_.log(now_, Category::IO, p.pid, d->name + (d->spooled ? " spool" : " buffer") + " FULL (" + std::to_string(d->occupancy) + "/" + std::to_string(d->capacity) + "): " + pname(p.pid) + " blocked (producer blocking)");
            block(p, WaitReason::BUFFER_FULL, d->name);
            break;
        case Device::Submit::REJECTED:
            sysResult_ = -1; ++p.ioErrors;
            log_.log(now_, Category::ERROR, p.pid, d->name + " is OFFLINE: " + pname(p.pid) + " request rejected (EIO)");
            break;
    }
}

void Kernel::onSyscall(Interrupt& i) {
    Sys nr = (Sys)i.a;
    PCB* p = find(i.pid);
    sysResult_ = 0;
    ++sysCounts_[(size_t)i.a];
    std::string who = i.pid > 0 ? pname(i.pid) : "shell";
    std::string args = i.s;
    if (nr == Sys::READ || nr == Sys::WRITE || nr == Sys::SLEEP || nr == Sys::ALLOCATE_MEMORY || nr == Sys::FREE_MEMORY || nr == Sys::CREATE_PROCESS || nr == Sys::TERMINATE_PROCESS)
        args += (args.empty() ? "" : ", ") + std::to_string(i.b);
    log_.log(now_, Category::SYSCALL, i.pid, who + " → TRAP " + SYS_NAMES[(size_t)i.a] + "(" + args + ")");
    i.action = "Trap → kernel mode → service routine → return to user mode";
    auto wakeAll = [&](const std::vector<int>& v) { for (int w : v) if (PCB* q = find(w)) wake(*q); };
    auto needProc = [&]() { if (!p) sysResult_ = -1; return p != nullptr; };
    switch (nr) {
        case Sys::READ: case Sys::WRITE: if (needProc()) doIo(*p, i.s, nr == Sys::WRITE, i.b, i.s2); break;
        case Sys::SLEEP:
            if (needProc()) { p->wakeAt = now_ + 1 + i.b; block(*p, WaitReason::SLEEP, std::to_string(i.b) + " ms"); }
            break;
        case Sys::WAIT:
            if (!needProc()) break;
            if (sync_.semWait(i.s, p->pid)) log_.log(now_, Category::SYNC, p->pid, pname(p->pid) + " WAIT(" + i.s + ") → acquired, value=" + std::to_string(sync_.semaphores[i.s].value));
            else { log_.log(now_, Category::SYNC, p->pid, pname(p->pid) + " WAIT(" + i.s + ") → blocked (value 0)"); block(*p, WaitReason::SEMAPHORE, i.s); }
            break;
        case Sys::SIGNAL: {
            auto woke = sync_.semSignal(i.s);
            log_.log(now_, Category::SYNC, i.pid, who + " SIGNAL(" + i.s + ")" + (woke.empty() ? " → value=" + std::to_string(sync_.semaphores[i.s].value) : " → " + pname(woke[0]) + " READY"));
            wakeAll(woke);
            break;
        }
        case Sys::LOCK:
            if (!needProc()) break;
            if (sync_.lock(i.s, p->pid)) log_.log(now_, Category::SYNC, p->pid, pname(p->pid) + " LOCK(" + i.s + ") acquired");
            else {
                log_.log(now_, Category::SYNC, p->pid, pname(p->pid) + " LOCK(" + i.s + ") → blocked, owner " + pname(sync_.mutexes[i.s].owner));
                block(*p, WaitReason::LOCK, i.s);
                detectDeadlocks();
            }
            break;
        case Sys::UNLOCK: {
            bool ok = false;
            auto woke = sync_.unlock(i.s, i.pid, ok);
            if (!ok) { sysResult_ = -1; log_.log(now_, Category::ERROR, i.pid, who + " UNLOCK(" + i.s + ") failed: not the owner"); break; }
            log_.log(now_, Category::SYNC, i.pid, who + " UNLOCK(" + i.s + ")" + (woke.empty() ? "" : " → " + pname(woke[0]) + " granted, READY"));
            wakeAll(woke);
            break;
        }
        case Sys::RDLOCK: case Sys::WRLOCK: {
            if (!needProc()) break;
            bool got = nr == Sys::RDLOCK ? sync_.rdlock(i.s, p->pid) : sync_.wrlock(i.s, p->pid);
            const char* mode = nr == Sys::RDLOCK ? "read" : "write";
            if (got) log_.log(now_, Category::SYNC, p->pid, pname(p->pid) + " " + mode + "-lock(" + i.s + ") acquired");
            else { log_.log(now_, Category::SYNC, p->pid, pname(p->pid) + " " + mode + "-lock(" + i.s + ") → blocked"); block(*p, WaitReason::LOCK, i.s); detectDeadlocks(); }
            break;
        }
        case Sys::RW_UNLOCK: {
            bool ok = false;
            auto woke = sync_.rwunlock(i.s, i.pid, ok);
            if (!ok) { sysResult_ = -1; log_.log(now_, Category::ERROR, i.pid, who + " rw_unlock(" + i.s + ") failed: lock not held"); break; }
            log_.log(now_, Category::SYNC, i.pid, who + " rw_unlock(" + i.s + ")" + (woke.empty() ? "" : " → " + std::to_string(woke.size()) + " waiter(s) granted"));
            wakeAll(woke);
            break;
        }
        case Sys::ALLOCATE_MEMORY:
            if (needProc()) {
                p->vpages += std::max(1, i.b);
                p->memoryKB += std::max(1, i.b) * mem_.pageSizeKB();
                mem_.resize(p->pid, p->vpages);
                log_.log(now_, Category::MEMORY, p->pid, pname(p->pid) + " allocate_memory(" + std::to_string(i.b) + " pages) → " + std::to_string(p->vpages) + " pages (demand-paged)");
            }
            break;
        case Sys::FREE_MEMORY:
            if (needProc()) {
                p->vpages = std::max(1, p->vpages - std::max(1, i.b));
                p->memoryKB = p->vpages * mem_.pageSizeKB();
                mem_.resize(p->pid, p->vpages);
                log_.log(now_, Category::MEMORY, p->pid, pname(p->pid) + " free_memory(" + std::to_string(i.b) + " pages) → " + std::to_string(p->vpages) + " pages");
            }
            break;
        case Sys::SEND_MESSAGE: {
            auto r = ipc_.send(i.pid, i.s, i.s2, now_);
            if (p) ++p->msgSent;
            if (r.deliverTo >= 0) {
                if (PCB* q = find(r.deliverTo)) { q->inbox = i.s2; ++q->msgRecv; log_.log(now_, Category::IPC, i.pid, who + " → \"" + i.s2 + "\" → " + i.s + " → " + pname(q->pid) + " (direct hand-off)"); wake(*q); }
            } else if (r.blocked) {
                if (p) { log_.log(now_, Category::IPC, i.pid, who + " send to " + i.s + " blocked: queue full"); block(*p, WaitReason::MESSAGE, i.s); }
                else { ipc_.queue(i.s).sendWaiters.pop_back(); sysResult_ = -1; }
            } else log_.log(now_, Category::IPC, i.pid, who + " → \"" + i.s2 + "\" → " + i.s + " (" + std::to_string(ipc_.queue(i.s).msgs.size()) + "/" + std::to_string(ipc_.queue(i.s).capacity) + ")");
            break;
        }
        case Sys::RECEIVE_MESSAGE: {
            if (!needProc()) break;
            auto r = ipc_.recv(p->pid, i.s);
            if (r.blocked) { log_.log(now_, Category::IPC, p->pid, pname(p->pid) + " receive on " + i.s + " blocked: queue empty"); block(*p, WaitReason::MESSAGE, i.s); }
            else {
                p->inbox = r.msg.text; ++p->msgRecv;
                log_.log(now_, Category::IPC, p->pid, pname(p->pid) + " ← \"" + r.msg.text + "\" from " + pname(r.msg.from) + " via " + i.s);
                if (r.wokenSender >= 0) if (PCB* q = find(r.wokenSender)) wake(*q);
            }
            break;
        }
        case Sys::CREATE_PROCESS:
            if (needProc()) {
                ProcessSpec c;
                c.type = ProcType::CPU_BOUND; c.cpuBurst = std::max(1, i.b); c.priority = p->priority; c.ppid = p->pid;
                c.memoryKB = std::max(mem_.pageSizeKB() * 2, p->memoryKB / 2);
                int child = createProcess(c);
                sysResult_ = child;
                if (child > 0) log_.log(now_, Category::PROCESS, p->pid, pname(p->pid) + " forked child " + pname(child));
            }
            break;
        case Sys::TERMINATE_PROCESS: {
            PCB* t = find(i.b);
            if (!t || !t->alive()) { sysResult_ = -1; break; }
            finish(*t, "terminated by " + who + " via terminate_process", -15, now_ + (t == running_ ? 1 : 0));
            break;
        }
        case Sys::EXIT:
            if (needProc()) finish(*p, "exit()", 0, now_ + (running_ == p ? 1 : 0));
            break;
        case Sys::SHM_READ:
            if (needProc()) p->regs[0] = ipc_.shmRead(i.s, i.b, p->pid, now_);
            break;
        case Sys::SHM_WRITE:
            if (needProc()) ipc_.shmWrite(i.s, i.b, p->regs[0] + i.c, p->pid, now_);
            break;
    }
}

bool Kernel::shellTerminate(int pid) {
    Interrupt i; i.type = IntType::SYSCALL; i.pid = 0; i.tick = now_; i.a = (int)Sys::TERMINATE_PROCESS; i.b = pid; i.cause = "trap";
    sysResult_ = 0;
    intc_.raise(i, true);
    return sysResult_ == 0;
}
bool Kernel::shellSend(const std::string& queue, const std::string& text) {
    Interrupt i; i.type = IntType::SYSCALL; i.pid = 0; i.tick = now_; i.a = (int)Sys::SEND_MESSAGE; i.s = queue; i.s2 = text; i.cause = "trap";
    sysResult_ = 0;
    intc_.raise(i, true);
    return sysResult_ == 0;
}

// ====================================================================================
// Deadlock detection & recovery
// ====================================================================================
void Kernel::detectDeadlocks() {
    auto groups = sync_.findDeadlocks();
    for (auto& ev : deadlocks_) {
        if (ev.resolved) continue;
        bool still = std::find(groups.begin(), groups.end(), ev.pids) != groups.end();
        if (!still) {
            ev.resolved = true; ev.resolvedAt = now_;
            log_.log(now_, Category::SYNC, -1, "Deadlock #" + std::to_string(ev.id) + " resolved: circular wait broken, system resumes");
        }
    }
    for (auto& g : groups) {
        bool known = false;
        for (auto& ev : deadlocks_) if (!ev.resolved && ev.pids == g) known = true;
        if (known) continue;
        DeadlockEvent ev;
        ev.id = nextDeadlockId_++; ev.detectedAt = now_; ev.pids = g;
        for (int pid : g) {
            std::string res = sync_.waitingOn(pid);
            auto it = sync_.mutexes.find(res);
            int owner = it != sync_.mutexes.end() ? it->second.owner : -1;
            ev.description += pname(pid) + " → waiting for " + res + (owner >= 0 ? " (held by " + pname(owner) + ")" : "") + "; ";
        }
        if (cfg_.autoDeadlockRecovery) ev.recoverAt = now_ + cfg_.deadlockRecoveryDelay;
        deadlocks_.push_back(ev);
        if (deadlocks_.size() > 30) deadlocks_.pop_front();
        log_.log(now_, Category::SYNC, -1, "⚠ DEADLOCK DETECTED (#" + std::to_string(ev.id) + "): " + ev.description +
                                              (cfg_.autoDeadlockRecovery ? "recovery in " + std::to_string(cfg_.deadlockRecoveryDelay) + " ms" : "automatic recovery disabled"));
    }
}

void Kernel::recoverDeadlocksNow() {
    for (size_t k = 0; k < deadlocks_.size(); ++k)
        if (!deadlocks_[k].resolved) recoverDeadlock(deadlocks_[k]);
}

void Kernel::recoverDeadlock(DeadlockEvent& ev) {
    PCB* victim = nullptr;
    for (int pid : ev.pids) {
        PCB* p = find(pid);
        if (!p || !p->alive()) continue;
        if (!victim || p->priority > victim->priority || (p->priority == victim->priority && (p->cpuTime < victim->cpuTime || (p->cpuTime == victim->cpuTime && p->pid > victim->pid)))) victim = p;
    }
    if (!victim) { detectDeadlocks(); return; }
    ev.victim = victim->pid;
    log_.log(now_, Category::SYNC, victim->pid, "RECOVERY #" + std::to_string(ev.id) + ": victim " + pname(victim->pid) + " (lowest importance: priority " + std::to_string(victim->priority) + ", CPU time " + std::to_string(victim->cpuTime) + ")");
    if (cfg_.recovery == RecoveryPolicy::ROLLBACK && victim->rollbacks < 2) {
        std::vector<int> woke = sync_.releaseAll(victim->pid);
        victim->pc = 0; victim->regs[0] = victim->regs[1] = victim->regs[2] = victim->regs[3] = 0;
        ++victim->rollbacks;
        victim->wait = WaitReason::NONE;
        ev.action = "rolled back " + pname(victim->pid);
        log_.log(now_, Category::SYNC, victim->pid, "ROLLBACK " + pname(victim->pid) + ": locks released, program restarted from PC=0");
        makeReady(*victim);
        for (int w : woke) if (PCB* q = find(w)) wake(*q);
    } else {
        ev.action = "terminated " + pname(victim->pid);
        finish(*victim, "deadlock victim", -9, now_);
    }
    log_.log(now_, Category::SYNC, -1, "Resources released → remaining processes resume");
    detectDeadlocks();
}

// ====================================================================================
// Thrashing / starvation management
// ====================================================================================
double Kernel::windowPfRate() const {
    long r = 0, f = 0;
    for (auto& t : window_) { r += t.refs; f += t.faults; }
    return r > 0 ? (double)f / (double)r : 0.0;
}
double Kernel::windowCpuUtil() const {
    if (window_.empty()) return 0.0;
    long b = 0;
    for (auto& t : window_) b += t.busy;
    return (double)b / (double)window_.size();
}

void Kernel::updateThrashing() {
    long refsWin = 0;
    for (auto& t : window_) refsWin += t.refs;
    double pf = windowPfRate(), cpu = windowCpuUtil(), mu = memUtil();
    int active = 0;
    for (auto& [pid, p] : procs_) if (p.alive() && p.state != State::NEW && p.state != State::SUSPENDED) ++active;
    bool bad = refsWin >= 12 && pf >= 0.55 && cpu < 0.50 && mu > 0.85 && active >= 3;
    auto pct = [](double x) { return std::to_string((int)std::lround(x * 100)) + "%"; };
    if (bad && !thrashing_) {
        thrashing_ = true; ++thrashEvents_;
        log_.log(now_, Category::MEMORY, -1, "⚠ THRASHING DETECTED: page-fault rate " + pct(pf) + ", CPU " + pct(cpu) + ", memory " + pct(mu) + ", " + std::to_string(active) + " active processes");
    } else if (thrashing_ && !bad && pf < 0.40) {
        thrashing_ = false;
        log_.log(now_, Category::MEMORY, -1, "Thrashing cleared: page-fault rate " + pct(pf) + ", CPU " + pct(cpu));
    }
    if (thrashing_ && cfg_.thrashingControl && active > 2 && now_ - lastSuspendAt_ >= 20) {
        PCB* v = nullptr;
        for (auto& [pid, p] : procs_) {
            if (p.state != State::READY && p.state != State::RUNNING) continue;
            if (!v || p.priority > v->priority || (p.priority == v->priority && (p.vpages > v->vpages || (p.vpages == v->vpages && p.pid > v->pid)))) v = &p;
        }
        if (v) suspend(*v);
    }
    if (!thrashing_ && pf < 0.25 && now_ - lastSuspendAt_ >= 30 && now_ - lastResumeAt_ >= 30) {
        for (auto& [pid, p] : procs_)
            if (p.state == State::SUSPENDED) {
                log_.log(now_, Category::MEMORY, p.pid, "THRASHING CONTROL: memory pressure eased → " + pname(p.pid) + " swapped back in");
                lastResumeAt_ = now_;
                makeReady(p);
                break;
            }
    }
}

void Kernel::scanStarvation() {
    for (auto& [pid, p] : procs_)
        if (p.state == State::READY && !p.starved && p.readyWait >= cfg_.starvationTicks) {
            p.starved = true;
            ++starvationEvents_;
            log_.log(now_, Category::SCHED, pid, "⚠ STARVATION: " + pname(pid) + " has waited " + std::to_string(p.readyWait) + " ms in the ready queue (priority " + std::to_string(p.priority) + ")");
        }
}

// ====================================================================================
// Health & metrics
// ====================================================================================
std::vector<HealthItem> Kernel::health() const {
    std::vector<HealthItem> h;
    auto pct = [](double x) { return std::to_string((int)std::lround(x * 100)) + "%"; };
    int active = 0, vsum = 0;
    for (auto& [pid, p] : procs_) if (p.alive() && p.state != State::NEW) { ++active; vsum += p.vpages; }
    double cpu = windowCpuUtil();
    h.push_back({"CPU", active == 0 ? "GREEN" : cpu >= 0.6 ? "GREEN" : cpu >= 0.3 ? "YELLOW" : "RED", active == 0 ? "idle" : pct(cpu) + " busy", "GREEN ≥60% busy, YELLOW ≥30%, else RED (when work exists)"});
    double commit = (double)vsum / mem_.totalFrames();
    h.push_back({"MEMORY", commit < 1.0 ? "GREEN" : commit < 1.5 ? "YELLOW" : "RED", pct(commit) + " committed", "Virtual pages ÷ frames: GREEN <100%, YELLOW <150%, else RED"});
    bool failed = false;
    for (auto& d : io_.devices()) failed = failed || d.failed;
    int q = io_.maxQueueNow();
    h.push_back({"I/O", failed || q >= 8 ? "RED" : q >= 4 ? "YELLOW" : "GREEN", failed ? "device offline" : "longest queue " + std::to_string(q), "Longest device queue: GREEN <4, YELLOW <8, RED ≥8 or any device offline"});
    bool starved = false;
    for (auto& [pid, p] : procs_) starved = starved || (p.state == State::READY && p.starved);
    h.push_back({"SCHEDULER", starved || sched_->size() > 16 ? "RED" : sched_->size() > 10 ? "YELLOW" : "GREEN", starved ? "starvation" : std::to_string(sched_->size()) + " ready", "RED on starvation or >16 ready, YELLOW >10 ready"});
    double pf = windowPfRate();
    h.push_back({"PAGE FAULTS", pf < 0.2 ? "GREEN" : pf < 0.5 ? "YELLOW" : "RED", pct(pf) + " of references", "Fault rate over last 50 ms: GREEN <20%, YELLOW <50%, RED ≥50%"});
    bool dl = false, holdWait = false;
    for (auto& ev : deadlocks_) dl = dl || !ev.resolved;
    for (auto& [pid, p] : procs_) if (p.state == State::WAITING && p.wait == WaitReason::LOCK && !sync_.heldBy(pid).empty()) holdWait = true;
    h.push_back({"DEADLOCK RISK", dl ? "RED" : holdWait ? "YELLOW" : "GREEN", dl ? "deadlock active" : holdWait ? "hold-and-wait present" : "none", "RED: cycle in wait-for graph; YELLOW: a process holds one lock while waiting for another"});
    return h;
}

Metrics Kernel::metrics() const {
    Metrics m;
    m.ticks = now_;
    double w = 0, t = 0, r = 0, c = 0;
    int n = 0, rn = 0;
    for (auto& [pid, p] : procs_) {
        ++m.processes;
        m.ioRequests += p.ioRequests;
        if (p.state != State::TERMINATED) continue;
        if (p.exitCode != 0 || p.completion < 0) { ++m.failed; continue; }
        ++m.completed; ++n;
        w += p.waitingTime; t += p.completion - p.arrival; c += p.completion;
        m.makespan = std::max(m.makespan, p.completion);
        if (p.firstRun >= 0) { r += p.firstRun - p.arrival; ++rn; }
    }
    if (n) { m.avgWaiting = w / n; m.avgTurnaround = t / n; m.avgCompletion = c / n; }
    if (rn) m.avgResponse = r / rn;
    if (now_ > 0) {
        m.cpuUtil = (double)busyTicks_ / now_;
        m.memUtil = (double)memUtilSum_ / ((double)now_ * mem_.totalFrames());
        m.throughput = m.completed * 100.0 / now_;
    }
    m.ctxSwitches = ctxSwitches_;
    m.pageFaults = faults_; m.memRefs = refs_; m.replacements = mem_.replacements();
    m.pfRate = refs_ ? (double)faults_ / refs_ : 0;
    long waitSum = 0, started = 0, qsum = 0;
    for (auto& d : io_.devices()) {
        waitSum += d.totalQueueWait; started += d.started; qsum += d.queueLenSum;
        m.deviceUtil.emplace_back(d.name, now_ ? (double)d.busyTicks / now_ : 0.0);
    }
    m.avgIoWait = started ? (double)waitSum / started : 0;
    m.avgIoQueue = now_ ? (double)qsum / now_ : 0;
    m.lockWaitTicks = lockWaitTicks_; m.blockEvents = sync_.blockEvents();
    m.deadlocks = (long)nextDeadlockId_ - 1;
    for (long c2 : sysCounts_) m.syscalls += c2;
    m.interrupts = intc_.total();
    m.starvation = starvationEvents_;
    m.peakBlocked = peakBlocked_;
    return m;
}

}  // namespace owr

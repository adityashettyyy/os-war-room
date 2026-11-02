// The simulated kernel: process manager, dispatcher, system-call layer, page-fault / I/O / timer /
// error handlers, deadlock and thrashing management, metrics and JSON export.
#pragma once
#include <array>
#include <string>
#include "common/config.hpp"
#include "common/json.hpp"
#include "common/logger.hpp"
#include "errors/crash_dump.hpp"
#include "interrupts/interrupt_controller.hpp"
#include "io/io_manager.hpp"
#include "ipc/ipc_manager.hpp"
#include "memory/memory_manager.hpp"
#include "process/pcb.hpp"
#include "scheduler/scheduler.hpp"
#include "synchronization/banker.hpp"
#include "synchronization/sync_manager.hpp"

namespace owr {

enum class Sys { CREATE_PROCESS, TERMINATE_PROCESS, READ, WRITE, SLEEP, WAIT, SIGNAL, ALLOCATE_MEMORY, FREE_MEMORY,
                 SEND_MESSAGE, RECEIVE_MESSAGE, EXIT, LOCK, UNLOCK, RDLOCK, WRLOCK, RW_UNLOCK, SHM_READ, SHM_WRITE };
inline const std::array<const char*, 19> SYS_NAMES{"create_process", "terminate_process", "read", "write", "sleep", "wait", "signal",
    "allocate_memory", "free_memory", "send_message", "receive_message", "exit", "lock", "unlock", "rdlock", "wrlock", "rw_unlock",
    "shm_read", "shm_write"};

struct Metrics {
    int ticks = 0, processes = 0, completed = 0, failed = 0, makespan = 0;
    double cpuUtil = 0, avgWaiting = 0, avgTurnaround = 0, avgResponse = 0, avgCompletion = 0, throughput = 0;  // throughput: completions per 100 ticks
    long ctxSwitches = 0, pageFaults = 0, memRefs = 0, replacements = 0;
    double pfRate = 0, memUtil = 0;           // memUtil: time-averaged frame occupancy
    long ioRequests = 0;
    double avgIoWait = 0, avgIoQueue = 0;
    std::vector<std::pair<std::string, double>> deviceUtil;
    long lockWaitTicks = 0, blockEvents = 0, deadlocks = 0, syscalls = 0, interrupts = 0, starvation = 0;
    int peakBlocked = 0;
    std::string toJson() const;
};

struct HealthItem { std::string name, level, value, rule; };

struct DeadlockEvent {
    int id = 0, detectedAt = 0, resolvedAt = -1, recoverAt = -1, victim = -1;
    bool resolved = false;
    std::vector<int> pids;
    std::string description, action;
};

struct SeriesPoint { int tick = 0; double cpu = 0, mem = 0, pf = 0; int ready = 0, active = 0, ioq = 0; };
struct SwitchRecord { int tick = -1, from = -1, to = -1, fromPc = 0, toPc = 0; };
struct FailureRecord { int tick = 0; std::string id, summary; };

class Kernel {
public:
    explicit Kernel(Config cfg = {});
    Kernel(const Kernel&) = delete;
    Kernel& operator=(const Kernel&) = delete;

    // ---- time -------------------------------------------------------------------------
    void tick();
    void run(int n) { for (int i = 0; i < n; ++i) tick(); }
    int now() const { return now_; }

    // ---- processes --------------------------------------------------------------------
    int createProcess(ProcessSpec spec);            // returns pid or -1
    PCB* find(int pid) { auto it = procs_.find(pid); return it == procs_.end() ? nullptr : &it->second; }
    const PCB* find(int pid) const { auto it = procs_.find(pid); return it == procs_.end() ? nullptr : &it->second; }
    const std::map<int, PCB>& processes() const { return procs_; }
    int runningPid() const { return running_ ? running_->pid : -1; }
    int aliveCount() const;
    bool allDone() const;                           // nothing left to run (no NEW/live processes)
    std::map<State, int> stateCounts() const;

    // ---- shell / user commands (go through the system-call layer) ------------------------
    bool shellTerminate(int pid);
    bool shellSend(const std::string& queue, const std::string& text);

    // ---- configuration ------------------------------------------------------------------
    const Config& config() const { return cfg_; }
    Config& mutableConfig() { return cfg_; }
    void setScheduler(SchedPolicy p, int quantum = -1);
    void setReplacement(ReplPolicy p);

    // ---- failure-injection primitives (see simulation/failure_lab) ------------------------
    int forcePageFault(int pid);                    // returns the page that will be referenced, -1 if impossible
    bool crashProcess(int pid, const std::string& reason);
    bool failDevice(const std::string& dev, int ticks);
    void recoverDeadlocksNow();
    void noteFailure(const std::string& id, const std::string& summary) { failures_.push_back({now_, id, summary}); if (failures_.size() > 40) failures_.pop_front(); }

    // ---- subsystems & observers -----------------------------------------------------------
    Logger& log() { return log_; }
    const Logger& log() const { return log_; }
    Scheduler& scheduler() { return *sched_; }
    const Scheduler& scheduler() const { return *sched_; }
    MemoryManager& memory() { return mem_; }
    const MemoryManager& memory() const { return mem_; }
    InterruptController& interrupts() { return intc_; }
    const InterruptController& interrupts() const { return intc_; }
    SyncManager& sync() { return sync_; }
    const SyncManager& sync() const { return sync_; }
    IpcManager& ipc() { return ipc_; }
    const IpcManager& ipc() const { return ipc_; }
    IOManager& io() { return io_; }
    const IOManager& io() const { return io_; }
    const std::deque<DeadlockEvent>& deadlockEvents() const { return deadlocks_; }
    const std::deque<CrashDump>& crashDumps() const { return dumps_; }
    const std::deque<FailureRecord>& failures() const { return failures_; }
    bool thrashing() const { return thrashing_; }
    long thrashingEvents() const { return thrashEvents_; }
    long starvationEvents() const { return starvationEvents_; }
    long suspensions() const { return suspensions_; }
    double windowPfRate() const;
    double windowCpuUtil() const;
    double memUtil() const { return (double)mem_.usedFrames() / mem_.totalFrames(); }
    long contextSwitches() const { return ctxSwitches_; }
    long busyTicks() const { return busyTicks_; }
    long idleTicks() const { return idleTicks_; }
    std::vector<HealthItem> health() const;
    Metrics metrics() const;
    const std::deque<SeriesPoint>& series() const { return series_; }
    std::string syscallName(Sys s) const { return SYS_NAMES[(size_t)s]; }

    // ---- JSON export for the dashboard --------------------------------------------------------
    std::string stateJson() const;
    std::string processJson(int pid) const;
    std::string logJson(long since, const std::set<Category>& cats, int pid, size_t limit) const;

private:
    struct PageLoad { int pid, page, readyAt; };
    struct TickStat { int refs = 0, faults = 0, busy = 0; };

    // lifecycle
    void admit(PCB& p);
    void makeReady(PCB& p);
    void wake(PCB& p);
    void block(PCB& p, WaitReason r, const std::string& target);
    void finish(PCB& p, const std::string& reason, int code, int completionTick);
    void suspend(PCB& p);
    // per-tick phases
    void hardwareStep();
    void dispatchStep();
    void executeTick();
    void postTick();
    void housekeeping();
    void preemptRunning(bool quantumExpired);
    void startRunning(PCB& p);
    void retire(PCB& p);
    void executeSyscallInstr(PCB& p, const Instr& in);
    // interrupt handlers
    void installHandlers();
    void onTimer(Interrupt& i);
    void onPageFault(Interrupt& i);
    void onIoComplete(Interrupt& i);
    void onError(Interrupt& i);
    void onSyscall(Interrupt& i);
    void raiseError(PCB& p, const std::string& cause);
    int sys(PCB& p, Sys nr, int a = 0, int b = 0, const std::string& s = "", const std::string& s2 = "");
    void doIo(PCB& p, const std::string& dev, bool write, int duration, const std::string& doc);
    // deadlock / memory pressure
    void detectDeadlocks();
    void recoverDeadlock(DeadlockEvent& ev);
    void updateThrashing();
    void scanStarvation();
    long nextUse(const Frame& f) const;
    std::string pname(int pid) const { return "P" + std::to_string(pid); }

    Config cfg_;
    Logger log_;
    std::unique_ptr<Scheduler> sched_;
    MemoryManager mem_;
    InterruptController intc_;
    SyncManager sync_;
    IpcManager ipc_;
    IOManager io_;
    std::map<int, PCB> procs_;
    std::vector<int> pendingNew_;
    PCB* running_ = nullptr;
    int lastRunPid_ = -1, switchLeft_ = 0, nextPid_ = 1, nextIoId_ = 1, now_ = 0;
    bool needResched_ = false, quantumExpired_ = false;
    std::vector<PageLoad> pageLoads_, deferred_;
    SwitchRecord lastSwitch_{}, switchInProgress_{};
    int sysResult_ = 0;

    // statistics
    long busyTicks_ = 0, idleTicks_ = 0, overheadTicks_ = 0, ctxSwitches_ = 0, faults_ = 0, refs_ = 0;
    long memUtilSum_ = 0, blockedSum_ = 0, lockWaitTicks_ = 0, thrashEvents_ = 0, starvationEvents_ = 0, suspensions_ = 0;
    int peakBlocked_ = 0, lastSuspendAt_ = -1000, lastResumeAt_ = -1000, nextDeadlockId_ = 1;
    bool thrashing_ = false;
    std::array<long, 19> sysCounts_{};
    std::deque<TickStat> window_;
    TickStat tickStat_;
    std::deque<SeriesPoint> series_;
    std::deque<DeadlockEvent> deadlocks_;
    std::deque<CrashDump> dumps_;
    std::deque<FailureRecord> failures_;
    static constexpr int kWindow = 50;
};

}  // namespace owr

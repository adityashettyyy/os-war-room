// All tunable kernel parameters in one place. One tick == 1 simulated millisecond.
#pragma once
#include "types.hpp"

namespace owr {

struct Config {
    // CPU scheduling
    SchedPolicy sched = SchedPolicy::RR;
    int quantum = 4;               // base time slice (RR / MLFQ level 0)
    bool aging = true;             // priority aging (also lets PRIORITY avoid starvation)
    int agingInterval = 20;        // ticks of waiting per priority-level improvement
    int mlfqBoostInterval = 100;   // MLFQ: periodic boost of all processes to level 0
    int ctxSwitchCost = 1;         // ticks of kernel overhead per real context switch
    int timerPeriod = 10;          // housekeeping timer interrupt period
    int starvationTicks = 120;     // READY this long without a dispatch => starving
    // Memory
    ReplPolicy repl = ReplPolicy::LRU;
    int physicalKB = 256;
    int pageSizeKB = 4;
    int pageFaultLatency = 3;      // ticks to service a page-in from the backing store
    bool thrashingControl = true;  // load control: suspend low-priority processes under thrashing
    // I/O
    int diskBuffer = 4;            // write-behind buffer capacity (DISK, NETWORK)
    int spoolCapacity = 12;        // printer spool queue capacity (jobs)
    int deviceRepairTicks = 30;    // default outage length of an injected device failure
    // Synchronisation / IPC
    bool autoDeadlockRecovery = true;
    int deadlockRecoveryDelay = 8; // ticks between detection and recovery (keeps it observable)
    RecoveryPolicy recovery = RecoveryPolicy::TERMINATE;
    int msgQueueCapacity = 4;
    // Misc
    uint32_t seed = 42;
    int maxProcesses = 64;
    int frames() const { return std::max(1, physicalKB / std::max(1, pageSizeKB)); }
};

}  // namespace owr

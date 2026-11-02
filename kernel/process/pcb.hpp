// Process Control Block: everything the kernel knows about one process.
#pragma once
#include "program.hpp"

namespace owr {

struct PCB {
    // identity
    int pid = 0, ppid = -1;
    std::string name;
    ProcType type = ProcType::CPU_BOUND;
    // execution state
    State state = State::NEW;
    WaitReason wait = WaitReason::NONE;
    std::string waitTarget;
    int pc = 0;
    int regs[4] = {0, 0, 0, 0};      // R0 accumulator, R1 last page, R2 last offset, R3 last syscall result
    // scheduling attributes
    int priority = 5, effPriority = 5;   // effPriority is lowered (improved) by aging
    int arrival = 0;
    int cpuBurst = 0, ioBurst = 0;
    int level = 0;                        // MLFQ queue level
    int quantumUsed = 0;
    int readyWait = 0;                    // consecutive ticks spent READY
    int burstRun = 0;                     // CPU ticks since the last blocking call
    double tau = 6.0;                     // ADAPTIVE: predicted next CPU burst
    bool starved = false;
    // memory
    int memoryKB = 0, vpages = 0, localityPages = -1;
    int forcePage = -1;                   // Failure Lab: next reference goes to this page
    bool retryPending = false;            // re-executing a reference after servicing its page fault
    int pinnedPage = -1;
    std::vector<Instr> program;
    // timeline & accounting
    int wakeAt = -1;
    int firstRun = -1, completion = -1;
    int waitingTime = 0, cpuTime = 0, blockedTime = 0, lockWaitTime = 0;
    int pageFaults = 0, memRefs = 0, dispatches = 0, syscalls = 0, ioRequests = 0, ioErrors = 0, rollbacks = 0;
    // resources & IPC
    std::vector<std::string> held;
    int msgSent = 0, msgRecv = 0;
    std::string inbox;
    // termination
    int exitCode = 0;
    std::string exitReason;

    int remaining() const { return (int)program.size() - pc; }
    bool alive() const { return state != State::TERMINATED; }
};

}  // namespace owr

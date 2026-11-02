# OS War Room: An Interactive Multiprogramming Operating System Simulator for Workload Management, Failure Injection and Kernel Recovery

*Operating Systems — course project report*

## Abstract
Operating-systems courses usually teach scheduling, paging, synchronisation and I/O as separate algorithms. In a real kernel they interact: a page fault blocks a process, which changes the ready queue, which changes utilisation, which changes whether the system looks "thrashed". **OS War Room** is a C++17 multiprogramming-OS simulator with a live React/TypeScript control room in which these mechanisms run together. Users create processes, switch algorithms live, inject failures (page-fault storms, deadlocks, crashes, I/O floods, device failures, starvation), watch the kernel detect and recover, compare algorithms on identical seeded workloads, and diagnose generated failures from raw logs. The system comprises ≈ 4 100 lines of C++ (kernel, simulation, server), ≈ 1 250 lines of TypeScript/CSS, 87 automated tests (2 036 assertions) validated against textbook results, and 11 reproducible scenarios.

## 1. Introduction and motivation
A conventional course project implements FCFS, a paging simulator, a semaphore example and a deadlock detector, and prints output. That demonstrates that each algorithm *can* be coded, not how an operating system behaves when they run at once. The goal here is a *mini operating-system command centre* where the evaluator can **create → run → break → diagnose → recover**.

### Objectives
1. A faithful multiprogramming kernel model: PCB, states, dispatcher, context switch, timer, system calls, interrupts, error handling.
2. Virtual memory with demand paging and four replacement policies, plus thrashing detection and mitigation.
3. Synchronisation (mutex, semaphore, RW lock), IPC (message queue, shared memory), deadlock detection/recovery.
4. I/O with blocking, buffering and spooling.
5. A Failure Lab, Experiment Lab (with what-if analysis) and OS Detective mode.
6. A dashboard that exposes all of the above interactively.

## 2. System design
(See `ARCHITECTURE.md` for diagrams.) The kernel is a deterministic, tick-driven discrete-event model. Each tick executes seven phases (admit, hardware, interrupts, recovery, dispatch, execute, account). Processes are *programs* in a small instruction set (compute with page references, device I/O, locks, semaphores, messages, shared memory, fork, crash); each instruction costs one millisecond. Because behaviour comes from programs rather than scripted outcomes, races, deadlocks and convoys **emerge** from the model — e.g. two processes doing an unlocked read-compute-write on shared memory really do lose updates.

**Dispatch and scheduling.** Seven policies sit behind one strategy interface (FCFS, SJF, SRTF, RR, preemptive Priority with aging, 3-level MLFQ, and an Adaptive scheduler using exponentially averaged burst prediction). A context switch logs *save context → update PCB → select → load context* and costs a configurable overhead.

**Interrupts and system calls.** Synchronous exceptions (traps, page faults, errors) are dispatched immediately; asynchronous events (device completion, timers) are queued and served by priority ERROR > PAGE_FAULT > IO_COMPLETE > TIMER. All 19 system calls (create/terminate process, read, write, sleep, wait/signal, lock primitives, allocate/free memory, send/receive, exit, shared memory) cross a trap boundary.

**Memory.** Per-process page tables, demand paging, a locality-based reference model, address translation, FIFO/LRU/Clock/Optimal replacement. Frames being paged in are *pinned* so a freshly loaded page cannot be stolen before its owner uses it. Thrashing is detected from fault rate, CPU utilisation and memory occupancy, and mitigated by swapping out low-priority processes (and swapping them back in when pressure eases).

**Synchronisation and deadlock.** Mutexes, counting semaphores and fair reader-writer locks hand ownership directly to waiters. Deadlock detection builds the wait-for graph and finds cycles with Tarjan's SCC algorithm; recovery selects a victim (lowest importance) and terminates or rolls it back. A separate Banker's-algorithm lab covers avoidance.

**I/O.** Four devices with FIFO queues. DISK/NETWORK writes use bounded write-behind buffers (full buffer ⇒ producer blocks); PRINTER uses a spool queue; device failure aborts requests with EIO and later repairs.

**Labs.** *Failure Lab* — eight injectable faults. *Experiment Lab* — the same seeded workload under any set of algorithms, with what-if sweeps. *OS Detective* — six hidden-cause case files whose logs have kernel alerts removed.

## 3. Implementation
| Part | Technology | Size |
|---|---|---|
| Kernel & simulation | C++17, header-centric modules, no external libraries | ≈ 3 500 lines |
| API server | POSIX sockets / Winsock HTTP server, JSON API, static hosting | ≈ 500 lines |
| Dashboard | React 18, TypeScript (strict), Vite, hand-written SVG charts | ≈ 1 250 lines |
| Build | Makefile and CMake, warning-free at `-Wall -Wextra -Wpedantic` | |

## 4. Verification
87 tests (`TESTING.md`) check algorithms against published results — FCFS 17 ms, RR 5.67 ms, SJF 7.75 ms, SRTF 6.5 ms, Priority 8.2 ms average waiting; FIFO/LRU/Optimal 15/12/9 faults; Bélády's anomaly; the Banker's safe sequence — and check cross-module behaviour (crash cleanup hands a lock to a waiter; a page fault costs exactly the configured latency; thrashing is detected and resolved). The suite is clean under AddressSanitizer/UBSan, and the API server was stress-tested with concurrent clients under the same sanitizers. Writing the tests exposed real defects (e.g. a thrashing detector that could never fire because it demanded too many references), which were fixed.

## 5. Results
Full tables are in `EXPERIMENTS.md`. Highlights (seed 42, 25-process mixed workload, 64 frames):
* SJF/SRTF reduce average waiting time by ≈ 48 % versus FCFS (411 vs 798 ms) but give poor response (≈ 340 ms); RR/MLFQ/Adaptive give 25 ms response at the price of ≈ 100 more context switches than FCFS (which, in this blocking-heavy mix, also happens to respond in 25 ms).
* LRU takes 129 page faults, FIFO 148, Clock 127 and the approximate-optimal policy 106 on the same workload.
* Raising the process count from 10 to 50 drops CPU utilisation 63 % → 47 % and raises the fault rate 16.6 % → 54.2 %; shrinking memory to 64 KB produces a 78 % fault rate.
* Page size shows a U-shaped optimum (4 KB for this workload).
* The Memory Meltdown scenario triggers thrashing detection and 12 load-control swap-outs in 600 ms; the Deadlock Trap is detected at T+63 and recovered; the Process Crash scenario releases the crashed process's lock to its waiter.

An honest negative result: with this blocking-heavy mix FCFS shows *no* convoy effect (frequent page faults and I/O make it behave cooperatively), and preemptive Priority with aging performs worst because aging causes churn. The convoy effect appears clearly in the dedicated scenario with a long non-blocking job.

## 6. Limitations and future work
Educational simulation on one simulated CPU with discrete time; constant page-fault latency; idealised devices; semaphores have no owner (semaphore-only deadlocks are not detected); Optimal replacement inside the kernel uses an owner-local oracle; the Windows socket path is untested. Future scope (from the case study): multi-core scheduling, disk-arm scheduling, a file system, real-time scheduling, workload prediction, container/distributed-OS concepts.

## 7. Conclusion
OS War Room turns the syllabus topics into one observable system. The evaluator can follow a single process through NEW → READY → RUNNING → WAITING, watch a page fault travel through interrupt handler, victim selection and page load, create a deadlock and watch it detected and broken, flood the I/O system and see buffers and the spooler absorb it, and then compare algorithms numerically — all reproducibly, with every claim backed by an automated test.

## References
Silberschatz, Galvin, Gagne — *Operating System Concepts* (scheduling, paging, Banker's, deadlock examples used as test oracles); Tanenbaum — *Modern Operating Systems*; Tarjan, R. (1972) *Depth-first search and linear graph algorithms* (SCC).

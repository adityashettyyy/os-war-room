# OS War Room — Architecture

## 1. System overview

```
 ┌──────────────────────────── Browser ────────────────────────────┐
 │  React + TypeScript dashboard   (polls /api/state every 300 ms)  │
 │  Control Room · Memory Lab · Sync&IPC · Experiment Lab · Detective│
 └───────────────▲──────────────────────────────────────┬───────────┘
                 │ JSON                                  │ commands (POST)
 ┌───────────────┴──────────────────────────────────────▼───────────┐
 │ owr_server  (dashboard/backend/api_server.cpp)                    │
 │  HTTP/1.1 · routing · static files · background tick thread       │
 │  one mutex guards the live Simulation; labs use private kernels   │
 └───────────────▲──────────────────────────────────────┬───────────┘
                 │                                       │
 ┌───────────────┴───────────────────────────────────────▼──────────┐
 │ simulation/                                                        │
 │  Simulation (kernel + timed actions) · scenario loader             │
 │  Failure Lab · Workload generator · Experiment Lab · OS Detective  │
 └───────────────▲───────────────────────────────────────────────────┘
                 │
 ┌───────────────┴───────────────────────────────────────────────────┐
 │ kernel/  — the simulated operating system                          │
 │                                                                    │
 │   User programs (PCB.program: COMPUTE/READ/WRITE/LOCK/SEND/…)      │
 │ ═════════════════ system-call trap boundary ═══════════════════════│
 │   Kernel ── Scheduler (strategy)      ── Interrupt controller      │
 │          ├─ Memory manager (+ replacement strategy)                │
 │          ├─ Sync manager (mutex/sem/rwlock + deadlock detector)    │
 │          ├─ IPC manager (queues, shared memory)                    │
 │          ├─ I/O manager (devices, buffers, spooler)                │
 │          └─ Error handler · health monitor · metrics · logger      │
 └────────────────────────────────────────────────────────────────────┘
```

## 2. Layering and dependency rules

| Layer | Depends on | Notes |
|---|---|---|
| `kernel/common` | std only | enums, `Config`, JSON, logger, RNG |
| `kernel/<module>` | `common` (+`process`) | each manager is self-contained and unit-testable without the kernel |
| `kernel/kernel.cpp` | all modules | the only place where modules interact |
| `simulation/` | kernel | drives the kernel through its public API only |
| `dashboard/backend` | simulation | translation between HTTP/JSON and the simulation |
| `dashboard/frontend` | HTTP API only | no shared code with C++ |

The kernel never calls upward, and the managers never call each other: cross-module effects (e.g. "process died → release locks, frames, I/O requests, IPC waits") are sequenced explicitly in `Kernel::finish`.

## 3. The tick: how everything interacts

One call to `Kernel::tick()` is one simulated millisecond and always runs the same seven phases:

```
 1  ADMIT      NEW processes whose arrival time has come  → READY
 2  HARDWARE   sleep timers, deferred page faults, finished page-ins,
               device advance (completions → IO_COMPLETE interrupts), periodic timer
 3  INTERRUPTS service the async queue by priority:  ERROR > PAGE_FAULT > IO_COMPLETE > TIMER
 4  RECOVERY   scheduled deadlock recovery
 5  DISPATCH   preempt if quantum expired / better process ready; pick next; context switch
 6  EXECUTE    run ONE instruction of the running process (or pay switch overhead)
               COMPUTE → memory reference → hit | page fault (sync exception)
               syscall instr → TRAP (sync) → kernel service routine → return
 7  ACCOUNT    waiting/blocked time, aging, MLFQ boost, utilisation window, telemetry
```

Synchronous events (traps, page faults, errors) are dispatched *immediately* inside phase 6, exactly like CPU exceptions; asynchronous events (device completion, timers) are queued and serviced in phase 3 in priority order, exactly like hardware interrupts.

### Process life-cycle

```
          admit                dispatch               exit()/crash/kill
  NEW ──────────► READY ◄──────────────► RUNNING ───────────────────────► TERMINATED
                    ▲   preempt / slice     │
                    │                       │ read/write, page fault, lock, wait,
                    │ wake (interrupt,      ▼ recv/send, sleep
                    │  unlock, signal)    WAITING
                    │
        swap-in ◄── SUSPENDED   (swapped out by thrashing load control)
```

### Worked example — one page fault end to end

```
tick t    P3 executes COMPUTE → page 12 not in its page table
          ├ PAGE_FAULT exception raised (sync) → PageFaultHandler
          ├ free frame? no → replacement policy picks victim (P1 page 3, LRU)
          ├ victim's PTE cleared, P3's PTE set, frame PINNED (cannot be stolen)
          ├ P3 → WAITING(PAGE_FAULT); page-in scheduled for t+1+latency
tick t+1  CPU runs another ready process (multiprogramming hides the latency)
tick t+4  hardware phase: swap read done → IO_COMPLETE(SWAP) → P3 READY
tick t+5  P3 dispatched, re-executes the SAME reference: hit, frame unpinned, PC advances
```

## 4. Key design decisions

| Decision | Why |
|---|---|
| **Tick-driven discrete-event core** (not wall-clock threads) | Deterministic, reproducible (seeded), unit-testable to exact millisecond; speed slider is just ticks/second. |
| **One-instruction-per-tick program model** with an assembler | Gives real PCs/registers, real blocking points, race conditions and deadlocks that *emerge* from programs rather than being scripted. |
| **Strategy interfaces** for scheduling and replacement | Algorithms are swapped live and compared on identical workloads; adding one means one class. |
| **Direct hand-off** on unlock/signal/send | A woken process resumes *after* the blocking instruction; avoids lost wake-ups and busy retry loops. |
| **Pinned frames during page-in** | Prevents a live-lock where a just-loaded page is evicted before its owner can use it. |
| **HTTP polling instead of WebSocket** | Zero dependencies; a 300 ms poll of a ~15 KB JSON state is smooth and trivially debuggable with `curl`. |
| **Labs run on private kernels** | Experiment Lab / Detective never disturb the live simulation and are fully deterministic. |
| **Kernel alerts hidden in Detective logs** | The case is solved by reasoning from raw logs, not by reading "DEADLOCK DETECTED". |

## 5. Concurrency model of the server

* One **simulation thread** ticks the live kernel at `speed` ticks/s while `running`.
* One **thread per HTTP connection** (short-lived, 5 s receive timeout).
* A single `std::mutex` serialises every access to the live `Simulation`; labs (`/api/experiment`, `/api/whatif`, `/api/replacement-lab`, `/api/banker`, detective) use private objects and need no lock.
* The server binds to **127.0.0.1 only**.

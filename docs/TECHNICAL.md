# Technical documentation

## 1. Build system
`Makefile` (plain make) and `CMakeLists.txt` (library `owr_core` + `owr_server`, `owr_cli`, `owr_tests`, `ctest`). Both compile with `-Wall -Wextra -Wpedantic` and produce **zero warnings**. The suite was also executed under `-fsanitize=address,undefined` (clean), and the server was stress-tested with concurrent clients under the same sanitizers.

## 2. Timing semantics (precise)
* One instruction = one tick. A process that executes a blocking syscall at tick *t* is off the CPU for ticks *t+1 … t+d* and runnable at *t+d+1* (`READ DISK 5` then `COMPUTE 1` completes at T+7 — asserted in tests).
* Page fault at *t*: process blocked for `1 + pageFaultLatency` ticks, then re-executes the **same** reference (not counted as a new reference).
* Quantum expiry is a **timer interrupt** raised after the last tick of the slice and serviced at the start of the next tick; no interrupt is raised if nobody else is READY.
* A real context switch (CPU context changes to a different pid) costs `ctxSwitchCost` ticks of kernel overhead; re-dispatching the same process is free.
* Completion time of a process = tick after its last instruction retires (or the tick its final I/O completed). Waiting time = ticks spent READY. Turnaround = completion − arrival. Response = first execution tick − arrival.

## 3. Algorithms and their validation
| Algorithm | Validated against |
|---|---|
| FCFS | textbook (24,3,3): avg wait **17** |
| RR q=4 | same set: avg wait **5.67**, 3 context switches |
| SJF | (8@0,4@1,9@2,5@3): **7.75** |
| SRTF | same set: **6.5** |
| Priority | 5-process set: **8.2** |
| FIFO/LRU/Optimal | reference string 7 0 1 2 0 3 …: **15 / 12 / 9** faults (3 frames) |
| FIFO | Bélády's anomaly reproduced (9 faults @3 frames, 10 @4) |
| Optimal | never worse than FIFO/LRU/Clock on 200-reference random strings, 2–6 frames |
| Banker | textbook safe sequence ⟨P1,P3,P4,P0,P2⟩; request (1,0,2) granted; (0,2,0) denied as unsafe |

**Thrashing detector** (checked every 10 ms over a 50 ms window): fault rate ≥ 55 % **and** CPU < 50 % **and** memory > 85 % full **and** ≥ 3 active processes **and** ≥ 12 references in the window (thrashing *reduces* references, so a high minimum would hide it). Cleared when the fault rate < 40 %. *Load control*: every 20 ms while thrashing, swap out the READY/RUNNING process with the numerically highest priority (largest address space breaks ties); swap one back in after 30 calm ms.

**Deadlock**: Tarjan SCC over the wait-for graph; victim = highest priority number, then least CPU time, then highest pid. `TERMINATE` kills it; `ROLLBACK` releases its locks and restarts it from PC 0 (at most twice, then terminates).

**Starvation**: READY ≥ `starvationTicks` (120 ms) without a dispatch. Aging lowers the effective priority number by one every `agingInterval` ms of waiting and is reset on dispatch.

## 4. Optimal replacement inside the kernel
A real kernel cannot know the future. In the integrated simulator the “oracle” for a frame is the distance, in the *owner process's own instruction stream*, to its next reference to that page. It is a close approximation (it ignores scheduling interleavings) and is labelled as such; the standalone reference-string lab computes the exact optimum.

## 5. Health indicators
Documented in the Guide tab and `Kernel::health()`: CPU, MEMORY (virtual pages ÷ frames), I/O (longest queue / offline), SCHEDULER (starvation / ready-queue length), PAGE FAULTS (window rate), DEADLOCK RISK (cycle / hold-and-wait).

## 6. HTTP API (all JSON; server binds to 127.0.0.1)
| Method & path | Body → result |
|---|---|
| `GET /api/health` | liveness |
| `GET /api/state` | `{running, speed, pendingActions, state{…}}` full kernel snapshot |
| `GET /api/process/{pid}` | PCB, page table, program window |
| `GET /api/log?since=&cats=&pid=&limit=` | incremental event log |
| `GET /api/scenarios`, `POST /api/scenario {name}` | list / load a scenario |
| `POST /api/control {action: run|pause|step|reset|speed, steps, speed, config}` | simulation control |
| `POST /api/process {type,cpuBurst,ioBurst,priority,memoryKB,arrival,script}` | create a process → `{pid}` |
| `POST /api/process/{pid}/kill` | `terminate_process` via the trap layer |
| `POST /api/config {scheduler,quantum,replacement,aging,…}` | live policy changes (validated) |
| `GET /api/failures`, `POST /api/failure {action,…}` | Failure Lab |
| `POST /api/deadlock/recover` | recover all active deadlocks now |
| `POST /api/sync/setup`, `POST /api/ipc/send` | create resources / shell `send_message` |
| `POST /api/translate {pid,vaddr}` | address translation |
| `POST /api/experiment`, `POST /api/whatif` | Experiment Lab (private kernels) |
| `POST /api/replacement-lab {refs,frames}` | step-by-step results for 4 policies |
| `POST /api/banker {available,max,allocation,request}` | safety + request check |
| `GET /api/detective`, `GET /api/detective/{id}`, `POST /api/detective/{id}/diagnose` | OS Detective |

Errors return `{"error": "..."}` with 400/404/409. Inputs are range-checked (workloads ≤ 100 processes, ≤ 500 references, bodies ≤ 1 MB, scenario names restricted to `[A-Za-z0-9_-]`, static paths reject `..`).

## 7. Extending the system
* **New scheduler** — derive from `Scheduler` in `scheduler.hpp`, add to `SchedPolicy`, `SCHED_NAMES` and `makeScheduler`; the UI picks it up from the name list.
* **New replacement policy** — derive from `ReplacementPolicy`, add to `makeReplacement`.
* **New instruction** — add to `Op`/`OP_NAMES`, the assembler, `executeSyscallInstr` and `onSyscall`.
* **New scenario** — drop a JSON file into `scenarios/` (format: see any existing file; `config`, `setup`, `processes[]` with optional `script`, timed `actions[]`).
* **New failure** — add a function in `failure_lab.cpp` and a case in `failure::inject`.

## Limitations and assumptions
* Educational simulation: single simulated CPU; discrete time; no real hardware, drivers, file system, privilege isolation or security. All failures are inside the simulation.
* Memory sizes are in KB and small by default so the frame map is readable; page-fault service time is a constant.
* “CPU burst” for SJF/SRTF is the exact remaining instruction count (an oracle); the Adaptive scheduler is the realistic predictor.
* Backing store, swap and the keyboard are idealised devices (constant service time, no seek model, no disk scheduling).
* Semaphores have no owner, so deadlocks involving only semaphores are not detected (mutex / RW-lock waits are).
* Deadlock detection assumes single-instance resources; multi-instance analysis is provided separately by the Banker's lab.
* The Windows code path (Winsock) is present but **not tested**; use WSL or Linux/macOS.
* Multi-core scheduling, disk-arm scheduling and a file system are out of scope (listed as future work in the case study).

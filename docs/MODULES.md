# Module documentation

Each module lives in `kernel/<name>/` and is unit-tested in isolation where possible.

## common — `types.hpp`, `config.hpp`, `json.hpp`, `logger.hpp`, `rng.hpp`
* `Config` — every tunable (scheduler, quantum, aging, context-switch cost, memory/page size, fault latency, buffer sizes, recovery policy, seed…). One tick = 1 ms.
* `json::Value/Parser/Writer` — complete, strict, dependency-free JSON (rejects trailing commas / garbage; max depth 64).
* `Logger` — bounded ring (8 000 events) of `{id, tick, category, pid, message}`; queryable by id/category/pid. Categories: SYSTEM, PROCESS, SCHED, INTERRUPT, SYSCALL, MEMORY, IO, SYNC, IPC, ERROR, FAILURE.
* `Rng` — xorshift32; all randomness (workloads, locality) derives from the seed.

## process — `pcb.hpp`, `program.hpp`
**PCB**: pid/ppid/name/type, state + wait reason/target, PC, 4 registers (R0 accumulator, R1 last page, R2 last offset, R3 last syscall result), priority & aged priority, MLFQ level, quantum use, predicted burst τ, page-table geometry, timeline (arrival, first run, completion), counters (CPU/waiting/blocked time, faults, syscalls, I/O errors, rollbacks), IPC inbox, exit code/reason.

**Instruction set** (one tick each): `COMPUTE n`, `READ|WRITE dev ms [doc]`, `SLEEP n`, `LOCK|UNLOCK r`, `WAIT|SIGNAL s`, `RDLOCK|WRLOCK|RWUNLOCK l`, `SEND q text`, `RECV q`, `SHMR seg i`, `SHMW seg i delta`, `ALLOC|FREE pages`, `FORK n`, `CRASH [why]`. `assemble()` reports errors with line numbers.

**Generators** build CPU-bound / I/O-bound / interactive programs of *exactly* `cpuBurst` instructions with the requested total device time. **`linkProgram`** expands `COMPUTE n` into n page references using a locality model (92 % inside a sliding working-set window that moves every 25 references, 8 % anywhere).

## scheduler — `scheduler.hpp`
Strategy interface: `enqueue`, `pickNext`, `shouldPreempt`, `timeSlice`, `onQuantumExpired`, `onBurstEnd`, `tick`, `order`.

| Policy | Selection | Preemption | Notes |
|---|---|---|---|
| FCFS | arrival order | none | convoy effect |
| SJF | least remaining CPU demand | none | oracle burst = remaining instructions |
| SRTF | least remaining | when a strictly shorter job is ready | |
| RR | FIFO | time slice = quantum (timer interrupt) | |
| PRIORITY | lowest effective priority number | immediate when better process is ready | **aging**: −1 level per `agingInterval` ms READY, reset on dispatch |
| MLFQ | lowest level, FIFO inside | slices q, 2q, 4q; demote on full slice; I/O-block keeps level; periodic boost | 3 levels |
| ADAPTIVE | min(τ − wait/16) | slice = clamp(round τ, 2, 12) | τ ← ½·burst + ½·τ (exponential average) |

## memory — `memory_manager.hpp`, `replacement.hpp`
* Frames (`pid, page, loadedAt, lastUsed, ref, pinned`), per-process page tables, demand paging, address translation (`virtual → page/offset → frame → physical`).
* `ReplacementPolicy`: **FIFO** (oldest load), **LRU** (oldest use), **Clock** (second chance, hand sweeps clearing ref bits), **Optimal** (farthest next use; inside the kernel the oracle looks ahead in the *owner's* program, in the standalone lab it sees the exact future).
* `simulateReplacement()` — reference-string lab returning every step (fault, victim, frame contents).
* Pinned frames are never victims; if every frame is pinned the fault is retried 2 ms later.

## interrupts — `interrupt_controller.hpp`
Types by priority: ERROR (0) > PAGE_FAULT > IO_COMPLETE > TIMER > SYSCALL. `raise(i, sync)`: sync → dispatched immediately (trap semantics); async → queued, `service()` drains by priority. History ring (300) records source, handler name and the action each handler took.

## synchronization — `sync_manager.hpp`, `banker.hpp`
* Mutex (ownership + FIFO waiters), counting semaphore (FIFO), reader-writer lock (FIFO-fair: readers queue behind a waiting writer).
* **Deadlock detection**: build the wait-for graph (waiter → current holder) and run Tarjan's strongly-connected-components algorithm; any SCC with > 1 node or a self-wait is a deadlock.
* **Banker's algorithm**: `isSafe` (returns a safe sequence), `request` (checks need, availability, resulting safety).

## ipc — `ipc_manager.hpp`
Bounded message queues (blocking send when full, blocking receive when empty, direct hand-off to a waiting receiver, FIFO) and named shared-memory segments with an access history.

## io — `io_manager.hpp`
Devices **DISK, PRINTER, KEYBOARD, NETWORK**, each with a FIFO request queue and one request in service.
* **Blocking I/O**: process → WAITING until the completion interrupt.
* **Buffering (DISK, NETWORK)**: writes enter a bounded write-behind buffer and the process continues; a full buffer parks the producer (`BUFFER_FULL`) until a slot drains. Statistics: full events, producer blocks, consumer idle ticks.
* **Spooling (PRINTER)**: a bounded spool queue of named jobs; the process continues while the printer prints.
* **Failure**: `fail(duration)` aborts every request (blocked processes get `EIO`, buffered writes are counted as lost), new requests are rejected until repair.

## errors — `crash_dump.hpp` + `Kernel::onError`
On an ERROR interrupt (injected fault, illegal instruction, segmentation fault) the handler records a crash dump (PC, registers, last instruction, held resources, resident pages, pending I/O), then `finish()` releases frames, locks (waking waiters), queued I/O and IPC waits, and the scheduler continues.

## kernel — `kernel.hpp`, `kernel.cpp`, `kernel_json.cpp`
Dispatcher & context switching (SAVE → SELECT → LOAD logged, overhead ticks configurable), 19 system calls through the trap layer, interrupt handlers, deadlock recovery (**TERMINATE** or **ROLLBACK** the least important victim), thrashing detection and **load control** (swap out lowest-priority processes, swap back in when calm), starvation scan, six-indicator health monitor, metrics, and JSON export.

## simulation
* `Simulation` — kernel + timed actions; `loadScenario()` builds a kernel from a scenario document.
* `failure_lab` — eight fault-injection actions (see User Guide).
* `workload_generator` — seeded CPU/I-O/interactive mixes.
* `experiment_lab` — `runExperiment`, `compareSchedulers`, `compareReplacement`, `whatIf`.
* `detective` — six deterministic case files with hidden-cause scenarios.

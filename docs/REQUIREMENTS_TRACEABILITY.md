# Requirements traceability

Maps every section of the case study (`OS_War_Room_Complete_Case_Study.md`) to its implementation and verification.

| § | Requirement | Implementation | Verified by |
|---|---|---|---|
| 6 | Main dashboard (CPU, memory, processes, queues, I/O, events, health) | `dashboard/frontend/src/ControlRoom.tsx`, `App.tsx` | browser run; `state_process_and_log_json…` |
| 7 | Process management, states | `PCB`, `Kernel::createProcess/makeReady/block/wake/finish` | `process_state_machine…` |
| 8 | PCB (id, state, PC, registers, priority, memory, resources, IPC…) | `kernel/process/pcb.hpp`; PCB inspector | `state_process_and_log_json…` |
| 9 | Interactive process creation (CPU-bound, I/O-bound, memory-intensive, interactive) | Create Process presets + form + assembler | `generated_programs_have_exact_burst_length…`, `assembler_parses…` |
| 10, 11 | CPU scheduling; interactive scheduler lab | 7 policies; live policy panel; `Kernel::setScheduler` | 14 scheduler tests |
| 12 | Context switching (save → update PCB → select → load) | `Kernel::startRunning`, CPU panel chain, overhead ticks | `timer_interrupt_drives_round_robin…`, `context_switch_cost…` |
| 13 | Supervisor / system calls | 19 syscalls via trap interrupt (`Kernel::sys/onSyscall`) | `every_syscall_is_dispatched…`, `shell_terminate…` |
| 14 | Interrupt system + interrupt viewer | `InterruptController`, Interrupts panel | `interrupts_are_serviced_in_priority_order…`, `io_interrupt_moves…` |
| 15, 16 | Virtual memory, paging, replacement (FIFO, LRU, Optimal, Clock) | `MemoryManager`, `replacement.hpp`, Memory Lab | 16 memory tests |
| 17 | Memory Battle (compare algorithms) | Experiment Lab replacement comparison; reference-string lab | `experiment_lab_compares_page_replacement…` |
| 18 | Thrashing detection & mitigation | `Kernel::updateThrashing`, load control | `thrashing_is_detected…`, `thrashing_control_suspends…` |
| 19 | Synchronisation (mutex, semaphore) | `SyncManager` (+ reader-writer lock) | 8 sync tests incl. producer-consumer, race, dining philosophers |
| 20 | IPC: message queue, shared memory | `IpcManager`, Sync & IPC tab | `message_queue_*`, `shared_memory_*` |
| 21, 22 | Deadlock laboratory; detection & recovery (+ RAG) | Tarjan SCC on wait-for graph; TERMINATE/ROLLBACK; RAG; Banker's lab | `deadlock_*`, `three_way_*`, `banker_*` |
| 23, 24 | I/O management; I/O blocking | `io_manager.hpp`, I/O interrupts | `blocking_read_takes_exactly…`, `io_wait_lets_another…` |
| 25 | Buffering | write-behind buffers (DISK, NETWORK) | `write_behind_buffer_*`, `buffer_stress_*` |
| 26 | Spooling | printer spool queue | `printer_spooler_decouples…` |
| 27 | Failure Lab | `simulation/failure_lab.cpp`, Failure Lab panel | `failure_lab_actions_report…` |
| 28 | Scenario — process crash | `fault_recovery.json`; error handler + crash dumps | `process_crash_saves_dump…`, `scenario_process_crash_recovers` |
| 29 | Scenario — memory pressure | `memory_pressure.json` | `scenario_memory_meltdown…` |
| 30 | Scenario — I/O flood | `io_flood.json` | `scenario_io_storm…`, `io_flood_builds_long_device_queues` |
| 31 | OS Detective Mode | `simulation/detective.cpp`, Detective tab (6 cases) | `detective_cases_have_the_documented_symptoms…` |
| 32, 33 | Experiment Lab; algorithm comparison | `experiment_lab.cpp`; comparison table + bar chart | `experiment_lab_runs_all_schedulers…` |
| 34 | What-If analysis | `whatIf()` (processes, quantum, memory, page size, I/O rate) | `what_if_*` |
| 35 | Kernel event log (filterable, categories) | `Logger`; Event Log panel | log JSON test |
| 36 | System health monitor | `Kernel::health()`; Health panel | `health_monitor_reports_documented_levels` |
| 37 | High-level architecture | `docs/ARCHITECTURE.md` | — |
| 38 | Technology stack: C++ core + React/TypeScript | as specified | build + `tsc --noEmit` |
| 39 | Repository structure | followed (`kernel/…`, `simulation/`, `dashboard/`, `scenarios/`, `tests/`, `docs/`) | — |
| 41 | Roadmap phases 1–10 | all phases delivered | — |
| 42 | Testing strategy (unit + integration) | 87 tests + API smoke | `docs/TESTING.md` |
| 43 | Performance evaluation (CPU, processes, memory, I/O, sync) | `Metrics`; Experiment Lab; `docs/EXPERIMENTS.md` | — |
| 44 | Five predefined scenarios | `cpu_heavy`, `memory_pressure`, `io_flood`, `deadlock`, `fault_recovery` (+6 more) | scenario tests |
| 45 | Faculty demonstration flow (9 steps) | `docs/USER_GUIDE.md §5`, Guide tab | walkthrough exercised in browser |
| 46 | Syllabus mapping | see below | — |
| 48 | Scope: must-have / recommended / signature | all delivered; optional: shared memory ✔, Banker ✔, thrashing mitigation ✔, adaptive scheduler ✔ | — |
| 48 | Optional: multi-core, advanced animation | **not implemented** (single CPU; CSS flash animation only) | — |
| 49 | Limitations stated | `docs/TECHNICAL.md §Limitations` | — |
| 51 | Deliverables 1–12 | see below | — |

## Syllabus mapping (§46)
Multiprogramming OS → process engine & dispatcher · Supervisor call → trap layer · Interrupt / generation / servicing → `InterruptController` + handlers + device events · Paging, Virtual memory → `MemoryManager` · Error handling → `Kernel::onError` · Process data structure → `PCB` · Process scheduling → `scheduler.hpp` · Synchronisation → `SyncManager` · IPC → `IpcManager` · I/O handling → `IOManager` · Spooling → printer spool · Buffering → write-behind buffers · Novel extension → Failure Lab + OS Detective + Experiment Lab.

## Deliverables checklist (§51)
| # | Deliverable | Location |
|---|---|---|
| 1 | Source code | `kernel/`, `simulation/`, `dashboard/`, `tools/` |
| 2 | Interactive dashboard | `dashboard/frontend` (built in `dist/`), served by `owr_server` |
| 3 | OS architecture diagram | `docs/ARCHITECTURE.md` |
| 4 | Module documentation | `docs/MODULES.md` |
| 5 | Test cases | `tests/` + `docs/TESTING.md` |
| 6 | Performance experiments | `docs/EXPERIMENTS.md`, `docs/results/` |
| 7 | Predefined demo scenarios | `scenarios/` (11) |
| 8 | Final report | `docs/FINAL_REPORT.md` |
| 9 | Presentation | `docs/OS_War_Room_Presentation.pptx` |
| 10 | User guide | `docs/USER_GUIDE.md` |
| 11 | Technical documentation | `docs/TECHNICAL.md` |
| 12 | Git repository | this repository (history included) |

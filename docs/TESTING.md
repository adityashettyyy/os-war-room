# Testing

## Strategy
| Level | Where | Purpose |
|---|---|---|
| **Unit** | `tests/test_scheduler.cpp`, `test_memory.cpp`, `test_sync_ipc_io.cpp` | each module against textbook ground truth and edge cases |
| **Integration** | `tests/test_kernel.cpp` | interrupts + scheduler + memory + I/O + sync working together; every scenario file; Failure Lab; Experiment Lab; Detective; JSON export; determinism |
| **API end-to-end** | `tests/api_smoke.sh` | starts `owr_server`, drives 14 HTTP checks |
| **UI end-to-end (manual/automated during development)** | headless Chromium | loaded scenarios, opened PCB/Sync/Memory/Experiment/Detective tabs: no console errors |
| **Robustness** | ASan + UBSan builds | whole suite clean; server stress-tested with concurrent clients |

Run: `make test` (or `ctest`), `tests/api_smoke.sh`. The test framework is a 40-line self-registering harness (`tests/test_framework.hpp`), so no dependencies are needed.

## Latest result
```
87 tests, 2036 checks, 0 failed
```
Full listing: `docs/results/test_output.txt`.

## What is verified
**Scheduling (14 tests)** — FCFS avg wait 17; RR(q=4) 5.67 with exact per-process waits and 3 switches; SJF 7.75; SRTF 6.5; Priority 8.2; preemptive priority runs a new high-priority arrival the tick it arrives; context-switch cost adds exactly one tick per switch (`makespan = Σburst + switches`); MLFQ demotion to level 2 with slices q·2^level; interactive processes stay at level 0; Adaptive burst prediction converges; aging prevents and no-aging causes starvation; live scheduler switch preserves the ready queue; SJF/SRTF ≤ FCFS on simultaneous arrivals; process state machine NEW→READY→RUNNING→WAITING→TERMINATED.

**Memory (16 tests)** — textbook fault counts FIFO 15 / LRU 12 / Optimal 9; Bélády's anomaly; Optimal ≤ others on random strings; address translation (page 1, offset 904 ↔ physical); pinned frames are never evicted; process exit frees all frames; FIFO ignores recency while Clock gives a second chance; the full page-fault chain is logged in order; a fault costs exactly `1+latency` ticks and the retry is not a new reference; replacement never exceeds the frame count for all four policies; forced page fault; segmentation fault terminates with a dump; thrashing detected under memory pressure and *resolved* by load control (all 16 processes finish); ALLOC/FREE resize the address space.

**Synchronisation / IPC / I/O (25 tests)** — mutex hand-off, semaphore FIFO, fair RW lock; 3-process cycle found while a bystander waiting behind it is not reported; Banker safe sequence/request admission/unsafe denial; deadlock detected and persists without recovery; victim = lowest importance; ROLLBACK lets both finish; ordered locking avoids deadlock; producer–consumer restores semaphore counts and actually blocks; readers–writers; dining philosophers (naive deadlocks, ordered does not); **race condition** loses updates unsynchronised and counts exactly 20 with a mutex; message-queue blocking both ways; shared memory; blocking read takes exactly its duration; I/O wait lets another process run; sleep via timer interrupt; FIFO device queues; buffer fills and blocks the producer, drains; printer spool decouples the process; device failure → EIO then repair; I/O flood turns the I/O LED red.

**Kernel / integration (32 tests)** — interrupt priority order and immediate traps; timer-driven RR switch with logged SAVE/LOAD steps; I/O interrupt READY chain; all 15 syscall kinds counted through the trap layer; shell `terminate_process`; fork; crash dump + resource cleanup + waiter gets lock; CRASH instruction; crash of an I/O-waiting process leaves the device consistent; health levels; Failure Lab actions; buffer stress; state/process/log JSON validity; determinism (same seed ⇒ identical metrics, different seed ⇒ different); each scenario's documented behaviour; Experiment Lab (7 schedulers finish; what-if monotonicity); Detective (symptoms per case, correct/wrong answers, **no answer leak** in wrong replies or case JSON, no kernel-alert text in logs); assembler errors with line numbers; generated programs have exact burst length; JSON round-trip and rejection of malformed input; config validation; process-table limit.

## Bugs found and fixed by the tests during development
* The thrashing detector required ≥ 30 references per 50 ms window, but thrashing *reduces* references, so it never fired at 60–80 % fault rates → threshold lowered to 12 (found by `thrashing_is_detected…`).
* A detective log line (“DEVICE FAILURE”) leaked the answer → alert text filtered (found by the leak test).
* Assembler kept trailing whitespace in document names after a comment → trimmed (found by `assembler_parses_programs…`).
* Several tests initially encoded wrong expectations (e.g. blocked time counts the request tick; Clock is 14, not 12, faults on the textbook string) and were corrected after analysing the traces.

## Known gaps
No automated browser test is committed (UI was verified interactively with headless Chromium during development). The Winsock code path is untested.

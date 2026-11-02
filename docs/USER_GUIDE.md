# User guide

## 1. Starting the system

```bash
make && make run          # then open http://127.0.0.1:8080
```
Options: `./build/owr_server --port 9000 --static dashboard/frontend/dist --scenarios scenarios`. If the page says *Kernel offline*, the server is not running.

The header shows the kernel status (OPERATIONAL / DEGRADED / CRITICAL), the simulated clock `T+nnnnn` (1 tick = 1 ms), Run/Pause, Step (+1/+10/+100), the speed slider (ticks per real second), Reset, and a scenario loader.

## 2. Control Room panels

| Panel | What it shows / what you can do |
|---|---|
| **CPU** | running process, PC and progress, time-slice meter, utilisation (50 ms window and total), context-switch counter, busy/overhead/idle ms, and the last context switch as **SAVE Pa → SELECT → LOAD Pb**. Click the core to open the PCB. |
| **Processes** | all processes with state (and wait reason), priority (aged priority after `→`), progress, resident/virtual pages, page faults, waiting time, held locks. Click a row to open the **PCB inspector**. |
| **System Health** | six LEDs (CPU, MEMORY, I/O, SCHEDULER, PAGE FAULTS, DEADLOCK RISK) with hover tooltips giving the exact rule; banners for THRASHING and DEADLOCK. |
| **PCB inspector** | state, PC, registers, priorities, MLFQ level, timeline, per-process page table (green = resident), held/awaited resources, IPC inbox, the program around the PC, and **terminate_process**. |
| **Physical Memory** | frame map coloured by process (white outline = pinned: page-in in flight), replacement-algorithm selector, fault rate, load-control swap-outs. |
| **I/O Devices** | per device: state, request in service, queue, buffer/spool occupancy, utilisation, errors. |
| **Interrupts** | counts by type, pending queue, recent interrupts with handler and action; system-call counters. |
| **Ready Queue** | in the order the active policy will serve it (remaining time / priority / MLFQ level shown). |
| **Telemetry** | CPU, memory, page-fault rate over time. |
| **Kernel Event Log** | colour-coded, filterable by category and pid. `SYSCALL` is off by default (turn it on to see every trap). |
| **Create Process** | one-click presets (+CPU-bound, +I/O-bound, +Memory-intensive, +Interactive) or the full form, including a custom program. |
| **Scheduler & Kernel Policy** | algorithm, quantum, context-switch cost, aging, thrashing control, automatic deadlock recovery, recovery policy — all applied live. Physical memory and page size change on **Reset**. |
| **Failure Lab** | see below. |

### Custom programs (Create Process → Advanced)
```
COMPUTE 3              ; 3 ms of CPU, each ms references a page
LOCK R1                ; mutex
READ DISK 4            ; blocking read, 4 ms of device time
WRITE PRINTER 6 a.pdf  ; spooled write, process continues
SEND inbox hello       ; message queue (capacity 4 by default)
UNLOCK R1
```
Full instruction list: `COMPUTE READ WRITE SLEEP LOCK UNLOCK WAIT SIGNAL RDLOCK WRLOCK RWUNLOCK SEND RECV SHMR SHMW ALLOC FREE FORK CRASH`. Errors are reported with the line number.

## 3. Failure Lab

| Button | Effect | What to watch |
|---|---|---|
| **Force Page Fault** (selected process) | its next reference targets a non-resident page | log: PAGE FAULT → VICTIM SELECTION → PAGE LOAD → I/O COMPLETE (swap) → READY |
| **Exhaust Memory** | spawns *Count* memory-hungry processes | page-fault rate ↑, CPU ↓, THRASHING banner, swap-outs |
| **Create Deadlock** | 2–5 processes locking resources in a cycle | Sync & IPC → RAG turns red; DEADLOCK RISK; automatic recovery |
| **Crash Process** (selected) | error interrupt in the process | crash dump, lock released and handed to waiter, frames freed |
| **Flood I/O Queue** | *Count* processes issuing disk + printer requests | device queues, I/O LED, printer spool |
| **Fail DISK** (device selectable) | device offline for 30 ms | EIO to blocked processes, rejected requests, repair message |
| **Buffer Stress** | bursty producers vs. 4-slot buffers | buffer 4/4, producers in BUFFER_FULL |
| **Starvation** | switches to PRIORITY with aging **off**; one priority-9 victim vs five priority-1 hogs | STARVED badge, SCHEDULER LED; turn **priority aging** on to fix it |

## 4. Other tabs
* **Memory Lab** — type a reference string and frame count, *Run all four*; click an algorithm card for the step table (red = fault, green = hit, “out” row = victim). *Address Translation* converts a virtual address of a live process to page/offset/frame/physical.
* **Sync & IPC** — live Resource Allocation Graph (solid arrow resource→process = held; dashed process→resource = waiting; red = deadlocked cycle), **Recover now**, deadlock history, mutex/semaphore/RW-lock tables, message queues and shared memory, a *send_message* shell, and the Banker's algorithm lab (edit the JSON, *Analyse*).
* **Experiment Lab** — choose a workload and algorithms; *Run comparison* gives a metric table (best per row in green), a bar chart for any metric and a page-replacement comparison. *What-If* sweeps one variable and charts CPU utilisation, fault rate and waiting time.
* **OS Detective** — open a case, read symptoms and the four logs, reveal hints, pick the cause. Wrong answers are crossed out and never reveal the answer.

## 5. Suggested 10-minute evaluator flow (maps to the case study's 9 steps)
1. **Start** — status OPERATIONAL, CPU 0 %, memory 0 %, I/O idle.
2. **Create** four processes with the presets (CPU-bound, I/O-bound, Memory-intensive, Interactive).
3. **Run** — point out states, ready queue, CPU, frame map; switch scheduler live.
4. **Trigger a page fault** on a process → show the chain in the log and Interrupts panel.
5. **Create deadlock** → Sync & IPC tab: cycle, detection, recovery (or press *Recover now*).
6. **Flood I/O** → queues, buffer 4/4, spool, blocked producers.
7. **Crash a process** → crash dump and resource cleanup.
8. **Experiment Lab** → FCFS vs RR vs MLFQ on the same workload.
9. **OS Detective** → let the evaluator diagnose a case.

## 6. Headless use
```bash
./build/owr_cli scenario scenarios/deadlock.json --log     # replay a scenario with the kernel log
./build/owr_cli experiment --cpu 10 --io 10 --ui 5 --memory 256 --page 4 --quantum 4
./build/owr_cli whatif --var processes --values 10,20,30,40,50
./build/owr_cli replacement --refs "7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1" --frames 3
./build/owr_cli detective                                  # print all case files WITH answers (instructor view)
```

## 7. Troubleshooting
| Symptom | Fix |
|---|---|
| “Dashboard not built” page | `cd dashboard/frontend && npm install && npm run build` |
| Port in use | `--port 9000` |
| Create Process says “script: line N …” | fix the program line shown |
| Nothing moves | press **Run** (the kernel starts paused) |

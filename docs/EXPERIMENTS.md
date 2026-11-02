# Performance experiments

All numbers below were produced by this repository (`owr_cli`, seed 42) and are reproducible: the same command prints the same table. Time is in simulated milliseconds. Unless stated otherwise: workload = 10 CPU-bound + 10 I/O-bound + 5 interactive processes (arrivals spread over 20 ms, 24–48 KB address spaces), 256 KB memory, 4 KB pages (64 frames), quantum 4 ms, context-switch cost 1 ms, LRU replacement, thrashing control off, ~16–17 % of references fault for a default run.

Regenerate: `./build/owr_cli experiment`, `./build/owr_cli whatif --var <v> --values <list>`.

## 1. Scheduler comparison (identical workload)

Workload: 10 CPU-bound, 10 I/O-bound, 5 interactive; memory 256 KB, page 4 KB, quantum 4, replacement LRU, seed 42

| Metric | FCFS | SJF | SRTF | RR | PRIORITY | MLFQ | ADAPTIVE | 
|---|---:|---:|---:|---:|---:|---:|---:|
| Avg waiting (ms) | 797.6 | 411.3 | 438.0 | 887.2 | 663.6 | 884.1 | 882.2 | 
| Avg turnaround (ms) | 878.0 | 499.3 | 522.9 | 971.2 | 756.7 | 967.7 | 970.0 | 
| Avg response (ms) | 25.2 | 340.2 | 375.2 | 25.2 | 504.1 | 25.2 | 25.2 | 
| Makespan (ms) | 1125 | 1100 | 1162 | 1248 | 1480 | 1245 | 1231 | 
| CPU utilisation (%) | 71.3 | 72.9 | 69.0 | 64.3 | 54.2 | 64.4 | 65.2 | 
| Throughput (/100 ms) | 2.22 | 2.27 | 2.15 | 2.00 | 1.69 | 2.01 | 2.03 | 
| Context switches | 200 | 177 | 242 | 313 | 573 | 309 | 268 | 
| Page faults | 122 | 104 | 104 | 129 | 104 | 130 | 131 | 
| Page-fault rate (%) | 17.4 | 14.9 | 14.9 | 18.4 | 14.9 | 18.6 | 18.7 | 
| Replacements | 25 | 0 | 0 | 33 | 0 | 34 | 40 | 
| Avg I/O queue wait (ms) | 2.3 | 5.3 | 3.8 | 1.5 | 2.3 | 1.7 | 3.7 |

**Reading the table**
* **SJF / SRTF** give by far the lowest average waiting and turnaround time (≈ 411–438 ms vs 798 for FCFS and 887 for RR) — the classical result. The price is response time (≈ 340–375 ms): long jobs wait for all shorter ones.
* **RR, MLFQ and Adaptive** have the best *response* (25 ms — FCFS ties them here, see the next bullet), but the most waiting: with 25 competing processes a time slice just moves work around, and each switch costs 1 ms, so CPU utilisation (64–65 %) is below FCFS/SJF (71–73 %).
* **FCFS does not show a convoy effect here.** In this workload every process blocks often (device I/O and page faults), so FCFS is effectively cooperative multitasking. The classic convoy needs a long *non-blocking* job — see the dedicated `convoy` scenario, where one 130 ms batch job holds the CPU while eight I/O jobs queue behind it and the disk stays at 0 % utilisation (OS Detective case 6).
* **Preemptive Priority with aging is the worst** here (makespan 1480 ms, 573 context switches): every aging step can make a waiting process better than the running one, causing repeated preemptions. This is a genuine property of aging + preemption, and the reason MLFQ uses periodic boosts and demotion instead.
* MLFQ and Adaptive land close to RR on this mixed workload; their advantage appears when interactive and CPU-bound jobs coexist for long periods (the `mixed_workload` scenario: over 2 500 ms interactive processes never leave MLFQ level 0 while CPU-bound ones sink to level 2, and all 25 processes finish).

## 2. Page-replacement comparison (RR scheduler, same workload)

| Metric | FIFO | LRU | CLOCK | OPTIMAL | 
|---|---:|---:|---:|---:|
| Avg waiting (ms) | 935.0 | 887.2 | 890.4 | 862.0 | 
| Avg turnaround (ms) | 1022.3 | 971.2 | 974.8 | 943.2 | 
| Avg response (ms) | 25.2 | 25.2 | 25.2 | 25.2 | 
| Makespan (ms) | 1288 | 1248 | 1245 | 1205 | 
| CPU utilisation (%) | 62.3 | 64.3 | 64.4 | 66.6 | 
| Throughput (/100 ms) | 1.94 | 2.00 | 2.01 | 2.07 | 
| Context switches | 337 | 313 | 315 | 296 | 
| Page faults | 148 | 129 | 127 | 106 | 
| Page-fault rate (%) | 21.1 | 18.4 | 18.1 | 15.1 | 
| Replacements | 53 | 33 | 31 | 16 | 
| Avg I/O queue wait (ms) | 1.5 | 1.5 | 1.9 | 2.1 |

FIFO takes ≈ 40 % more faults than the (approximate, in-kernel) optimum and ≈ 15 % more than LRU; Clock tracks LRU closely at lower bookkeeping cost, as theory predicts. The standalone lab on the textbook string gives the exact result:

Reference string (20 refs), 3 frames

| Algorithm | Faults | Hits | Fault rate |
|---|---:|---:|---:|
| FIFO | 15 | 5 | 75.0% |
| LRU | 12 | 8 | 60.0% |
| CLOCK | 14 | 6 | 70.0% |
| OPTIMAL | 9 | 11 | 45.0% |

(Clock = second-chance. Optimal is exact there; inside the kernel it uses an owner-local look-ahead, see Technical §4.)

## 3. What-if analysis

### 3.1 Number of processes (10 → 50)
| processes | CPU util (%) | Page-fault rate (%) | Avg waiting (ms) | Throughput (/100 ms) | Makespan (ms) | Finished |
|---:|---:|---:|---:|---:|---:|:--:|
| 10 | 63.2 | 16.6 | 313.2 | 1.95 | 512 | yes |
| 20 | 65.8 | 16.2 | 656.5 | 2.11 | 949 | yes |
| 30 | 59.8 | 25.7 | 1223.0 | 1.86 | 1615 | yes |
| 40 | 54.6 | 36.0 | 1860.7 | 1.66 | 2416 | yes |
| 50 | 47.4 | 54.2 | 2633.2 | 1.47 | 3406 | yes |

CPU utilisation falls from 63 % to 47 % and the page-fault rate more than triples (16.6 % → 54.2 %) as the combined working sets outgrow 64 frames; average waiting time grows ~8×. Throughput peaks at 20 processes and then declines — the textbook “degree of multiprogramming” curve.

### 3.2 Time quantum — CPU-bound workload only (8 processes)
| quantum | CPU util (%) | Page-fault rate (%) | Avg waiting (ms) | Throughput (/100 ms) | Makespan (ms) | Finished |
|---:|---:|---:|---:|---:|---:|:--:|
| 1 | 44.3 | 13.7 | 612.0 | 1.02 | 788 | yes |
| 2 | 57.4 | 13.7 | 463.0 | 1.32 | 608 | yes |
| 4 | 67.4 | 13.7 | 380.6 | 1.54 | 518 | yes |
| 8 | 73.2 | 13.7 | 333.0 | 1.68 | 477 | yes |
| 16 | 75.7 | 13.7 | 302.2 | 1.74 | 461 | yes |
| 32 | 77.2 | 13.7 | 275.5 | 1.77 | 452 | yes |

A 1 ms quantum spends so much time switching that CPU utilisation is 44 %; utilisation and throughput rise monotonically with the quantum (77 % at 32 ms) and average waiting time falls. The cost of a long quantum (not visible in these averages) is responsiveness for interactive jobs — compare the mixed-workload table below.

### 3.3 Time quantum — mixed workload
| quantum | CPU util (%) | Page-fault rate (%) | Avg waiting (ms) | Throughput (/100 ms) | Makespan (ms) | Finished |
|---:|---:|---:|---:|---:|---:|:--:|
| 1 | 43.7 | 16.9 | 1337.9 | 1.36 | 1835 | yes |
| 2 | 55.9 | 17.6 | 1050.0 | 1.74 | 1434 | yes |
| 4 | 64.3 | 18.4 | 887.2 | 2.00 | 1248 | yes |
| 8 | 69.0 | 18.0 | 834.1 | 2.15 | 1162 | yes |
| 16 | 70.1 | 18.1 | 821.6 | 2.19 | 1144 | yes |
| 32 | 71.3 | 17.4 | 797.6 | 2.22 | 1125 | yes |

### 3.4 Physical memory (64 KB → 1 MB)
| memoryKB | CPU util (%) | Page-fault rate (%) | Avg waiting (ms) | Throughput (/100 ms) | Makespan (ms) | Finished |
|---:|---:|---:|---:|---:|---:|:--:|
| 64 | 40.2 | 78.3 | 1163.5 | 1.25 | 1997 | yes |
| 128 | 48.6 | 50.7 | 1210.2 | 1.51 | 1651 | yes |
| 256 | 64.3 | 18.4 | 887.2 | 2.00 | 1248 | yes |
| 512 | 66.8 | 14.9 | 857.1 | 2.08 | 1201 | yes |
| 1024 | 66.8 | 14.9 | 857.1 | 2.08 | 1201 | yes |

Below the combined working set the system degrades sharply (78 % faults at 64 KB). Beyond ~512 KB there is no further gain: the remaining faults are compulsory first-touch faults and working-set moves.

### 3.5 Page size (1 KB → 16 KB, memory fixed at 256 KB)
| pageSizeKB | CPU util (%) | Page-fault rate (%) | Avg waiting (ms) | Throughput (/100 ms) | Makespan (ms) | Finished |
|---:|---:|---:|---:|---:|---:|:--:|
| 1 | 52.8 | 40.6 | 1080.4 | 1.65 | 1518 | yes |
| 2 | 60.3 | 25.3 | 963.0 | 1.88 | 1329 | yes |
| 4 | 64.3 | 18.4 | 887.2 | 2.00 | 1248 | yes |
| 8 | 49.8 | 47.0 | 1221.4 | 1.55 | 1609 | yes |
| 16 | 40.5 | 76.6 | 1157.3 | 1.26 | 1979 | yes |

A U-shaped curve with the optimum at 4 KB: tiny pages exploit locality poorly (more faults), huge pages leave too few frames for 25 processes (fault rate 76.6 % at 16 KB). The shape depends on the locality model (92 % of references in a moving window), which is stated in the Technical docs.

### 3.6 I/O intensity (× device time)
| ioRate | CPU util (%) | Page-fault rate (%) | Avg waiting (ms) | Throughput (/100 ms) | Makespan (ms) | Finished |
|---:|---:|---:|---:|---:|---:|:--:|
| 0.5 | 64.3 | 18.4 | 891.4 | 2.00 | 1248 | yes |
| 1 | 64.3 | 18.4 | 887.2 | 2.00 | 1248 | yes |
| 2 | 64.3 | 18.3 | 861.7 | 2.00 | 1248 | yes |
| 4 | 36.7 | 18.6 | 739.5 | 1.14 | 2183 | yes |

## 4. Scenario outcomes (`owr_cli scenario …`, suggested duration)
| Scenario | Result |
|---|---|
| cpu_heavy (450 ms) | 10/10 complete, 78 context switches, 35 page faults |
| memory_pressure (600 ms) | thrashing detected twice, 12 swap-outs by load control, page-fault rate 54 %, CPU 45 % |
| io_flood (320 ms) | disk and spool queues build up, buffer-full events block producers, 21/24 complete |
| deadlock (240 ms) | 1 deadlock detected at T+63, victim terminated, survivor and workers finish (4/5, 1 killed) |
| fault_recovery (260 ms) | crash at T+15, dump saved, waiter gets the lock, 4/5 complete |
| dining_philosophers (200 ms) | 5-cycle deadlock, nobody completes (recovery disabled by design) |
| race_condition (120 ms) | unsynchronised counter ends < 20 (lost updates) |
| starvation (320 ms) | priority-9 process starved ≥ 120 ms (aging off) |

## 5. Validity and limits
Results come from a deterministic model with explicit assumptions (Technical §Limitations): constant fault latency, synthetic locality, oracle burst lengths for SJF/SRTF. They demonstrate *relative* behaviour of algorithms, not absolute performance of any real machine. Different seeds change numbers slightly but not the qualitative ordering (try `--seed 7`).

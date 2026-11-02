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

Page replacement (scheduler RR):

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


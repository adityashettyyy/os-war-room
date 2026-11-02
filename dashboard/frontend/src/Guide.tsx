import { Panel } from './ui';

const STEPS: [string, string][] = [
  ['Load “Scenario 1 — CPU Battle”', 'Press Run. The CPU panel shows the running process, a time-slice meter and the SAVE → SELECT → LOAD context-switch chain. The ready queue rotates; timer interrupts appear in the Interrupts panel and the log.'],
  ['Create processes in one click', 'In Create Process use the + CPU-bound / I/O-bound / Memory-intensive / Interactive presets (or build a custom program), then Run and watch NEW → READY → RUNNING.'],
  ['Click any process row', 'The PCB inspector opens: registers, program counter, scheduling counters, page table and the instructions around the PC. “terminate_process” kills it through the system-call layer.'],
  ['Switch the scheduler live', 'In Scheduler & Kernel Policy choose FCFS, SJF, SRTF, RR, PRIORITY, MLFQ or ADAPTIVE and watch waiting times and switches change immediately. Try FCFS on the “convoy” scenario.'],
  ['Load “Scenario 2 — Memory Meltdown”', 'Frames fill, the page-fault rate soars, CPU utilisation collapses and a THRASHING banner appears. With thrashing control on, low-priority processes are swapped out until the system recovers. Change the replacement algorithm in the Physical Memory panel.'],
  ['Load “Scenario 3 — I/O Storm”', 'Device queues grow, processes sit in WAITING, completion interrupts wake them. Disk/network buffers fill (producers block) and the printer spool backs up.'],
  ['Load “Scenario 4 — Deadlock Trap”, open Sync & IPC', 'The Resource Allocation Graph closes into a cycle, DEADLOCK RISK turns red, and after the recovery delay the less important process is terminated (or rolled back) and the survivor finishes.'],
  ['Failure Lab', 'Force a page fault on a selected process, exhaust memory, create a deadlock, crash a process, flood the I/O queue, fail a device, stress the buffers or start a starvation scenario. Watch Health, the log and the Crash Dumps.'],
  ['Experiment Lab', 'Run the same seeded workload under several schedulers and replacement policies, then run a what-if sweep (10 → 50 processes, quantum 1 → 32, memory 64 → 1024 KB, page size, I/O intensity).'],
  ['OS Detective', 'Open a case file, read the symptoms and raw logs, and diagnose the hidden fault.'],
];

export function GuideTab() {
  return (
    <div className="grid two">
      <Panel title="Evaluator Walk-through (≈10 minutes)" className="wide">
        <ol className="steps-list">{STEPS.map(([t, d]) => <li key={t}><b>{t}</b><span>{d}</span></li>)}</ol>
      </Panel>
      <Panel title="Reading the Dashboard">
        <dl className="defs">
          <dt>Tick</dt><dd>One simulated millisecond. “Speed” is ticks per real second.</dd>
          <dt>Process states</dt><dd>NEW → READY ⇄ RUNNING → TERMINATED; RUNNING → WAITING (I/O, page fault, lock, message, sleep) → READY; READY → SUSPENDED (swapped out by thrashing control).</dd>
          <dt>CPU utilisation</dt><dd>Share of ticks in which a process retired an instruction. Context-switch ticks and faulting ticks are overhead, not useful work.</dd>
          <dt>Page-fault rate</dt><dd>Faults ÷ memory references over the last 50 ticks (the re-execution after a fault is not counted as a new reference).</dd>
          <dt>Pinned frame</dt><dd>A frame whose page-in is in flight; it cannot be chosen as a victim until the faulting process re-runs its reference.</dd>
          <dt>Buffer / spool</dt><dd>Writes to DISK and NETWORK go into a bounded write-behind buffer and the process continues; a full buffer blocks the producer. PRINTER writes go to a spool queue.</dd>
        </dl>
      </Panel>
      <Panel title="Health Thresholds">
        <dl className="defs">
          <dt>CPU</dt><dd>GREEN ≥ 60% busy · YELLOW ≥ 30% · RED below, when work exists.</dd>
          <dt>MEMORY</dt><dd>Virtual pages ÷ frames: GREEN &lt; 100% · YELLOW &lt; 150% · RED above.</dd>
          <dt>I/O</dt><dd>Longest device queue: GREEN &lt; 4 · YELLOW &lt; 8 · RED ≥ 8 or any device offline.</dd>
          <dt>SCHEDULER</dt><dd>RED on starvation (READY ≥ 120 ms) or &gt; 16 ready; YELLOW &gt; 10 ready.</dd>
          <dt>PAGE FAULTS</dt><dd>GREEN &lt; 20% · YELLOW &lt; 50% · RED ≥ 50%.</dd>
          <dt>DEADLOCK RISK</dt><dd>RED: cycle in the wait-for graph · YELLOW: a process holds a lock while waiting for another.</dd>
          <dt>THRASHING</dt><dd>Fault rate ≥ 55%, CPU &lt; 50%, memory &gt; 85% full and ≥ 3 active processes.</dd>
        </dl>
      </Panel>
    </div>
  );
}

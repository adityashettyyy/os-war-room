// Shapes of the JSON served by the C++ API (see kernel/kernel_json.cpp).
export interface Proc {
  pid: number; ppid: number; name: string; type: string; state: string; wait: string; waitTarget: string;
  priority: number; effPriority: number; arrival: number; cpuBurst: number; remaining: number; pc: number;
  vpages: number; resident: number; memoryKB: number; pageFaults: number; waiting: number; cpuTime: number; blocked: number;
  level: number; starved: boolean; exitCode: number; exitReason: string; completion: number; firstRun: number; held: string[];
}
export interface FrameInfo { pid: number; page: number; pinned: boolean; ref: boolean; loadedAt: number; lastUsed: number }
export interface IoReq { pid: number; write: boolean; duration: number; doc: string }
export interface Device {
  name: string; failed: boolean; failedUntil: number; busy: boolean; util: number; completed: number; errors: number; maxQueue: number;
  queueLength: number; buffered: boolean; spooled: boolean; capacity: number; occupancy: number; fullEvents: number; producerBlocks: number;
  consumerIdle: number; lostWrites: number;
  current: { pid: number; write: boolean; remaining: number; duration: number; doc: string } | null;
  queue: IoReq[]; parked: { pid: number; doc: string }[];
}
export interface Intr { id: number; type: string; pid: number; tick: number; page: number; device: string; cause: string; handler: string; action: string; quiet: boolean; sync: boolean }
export interface Health { name: string; level: 'GREEN' | 'YELLOW' | 'RED'; value: string; rule: string }
export interface Metrics {
  ticks: number; processes: number; completed: number; failed: number; makespan: number; cpuUtil: number; avgWaiting: number; avgTurnaround: number;
  avgResponse: number; avgCompletion: number; throughput: number; ctxSwitches: number; pageFaults: number; memRefs: number; replacements: number;
  pfRate: number; memUtil: number; ioRequests: number; avgIoWait: number; avgIoQueue: number; lockWaitTicks: number; blockEvents: number;
  deadlocks: number; syscalls: number; interrupts: number; starvation: number; peakBlocked: number; deviceUtil: Record<string, number>;
}
export interface Mutex { name: string; owner: number; acquisitions: number; contentions: number; waiters: number[] }
export interface Semaphore { name: string; value: number; initial: number; blocks: number; waiters: number[] }
export interface RwLock { name: string; writer: number; readers: number[]; waiters: { pid: number; write: boolean }[] }
export interface DeadlockEv { id: number; detectedAt: number; resolvedAt: number; recoverAt: number; resolved: boolean; pids: number[]; description: string; victim: number; action: string }
export interface MsgQueue { name: string; capacity: number; sent: number; received: number; sendBlocks: number; recvBlocks: number; recvWaiters: number[]; messages: { from: number; text: string }[]; parked: { from: number; text: string }[] }
export interface Segment { name: string; reads: number; writes: number; data: number[]; history: { tick: number; pid: number; index: number; value: number; write: boolean }[] }
export interface Point { t: number; cpu: number; mem: number; pf: number; ready: number; active: number; ioq: number }
export interface KState {
  tick: number; status: 'OPERATIONAL' | 'DEGRADED' | 'CRITICAL'; allDone: boolean;
  config: { scheduler: string; quantum: number; aging: boolean; replacement: string; physicalKB: number; pageSizeKB: number; frames: number; ctxSwitchCost: number;
    thrashingControl: boolean; autoDeadlockRecovery: boolean; recovery: string; pageFaultLatency: number; seed: number };
  cpu: { running: number; util: number; utilTotal: number; ctxSwitches: number; busyTicks: number; idleTicks: number; overheadTicks: number; switchLeft: number; slice: number; quantumUsed: number;
    lastSwitch: { tick: number; from: number; to: number; fromPc: number; toPc: number } };
  processes: Proc[]; readyQueue: number[];
  memory: { total: number; used: number; pageSizeKB: number; policy: string; faults: number; refs: number; replacements: number; pfRate: number; pfRateTotal: number; util: number;
    thrashing: boolean; thrashEvents: number; suspensions: number; frames: (FrameInfo | null)[] };
  io: { totalQueue: number; devices: Device[] };
  interrupts: { total: number; counts: Record<string, number>; pending: Intr[]; recent: Intr[] };
  sync: { blockEvents: number; mutexes: Mutex[]; semaphores: Semaphore[]; rwlocks: RwLock[]; edges: [number, number][]; deadlocks: DeadlockEv[] };
  ipc: { queues: MsgQueue[]; segments: Segment[] };
  health: Health[]; syscalls: Record<string, number>; series: Point[];
  failures: { tick: number; id: string; summary: string }[];
  crashDumps: { tick: number; pid: number; reason: string; state: string; pc: number; lastInstr: string; resident: number; pendingIo: number; held: string[] }[];
  lastLogId: number; metrics: Metrics;
}
export interface Envelope { running: boolean; speed: number; pendingActions: number; state: KState }
export interface LogEvent { id: number; tick: number; t: string; cat: string; pid: number; msg: string }
export interface PcbDetail {
  pid: number; ppid: number; name: string; type: string; state: string; wait: string; waitTarget: string; pc: number; regs: number[]; priority: number; effPriority: number;
  arrival: number; cpuBurst: number; ioBurst: number; remaining: number; level: number; tau: number; memoryKB: number; vpages: number; firstRun: number; completion: number;
  waiting: number; cpuTime: number; blocked: number; pageFaults: number; memRefs: number; dispatches: number; syscalls: number; ioRequests: number; ioErrors: number;
  rollbacks: number; starved: boolean; exitCode: number; exitReason: string; held: string[]; waitingOn: string;
  ipc: { sent: number; received: number; inbox: string };
  pageTable: { page: number; frame: number; present: boolean }[];
  program: { i: number; text: string; current: boolean }[];
}
export interface ScenarioInfo { name: string; title: string; description: string; demonstrates: string[]; suggestedTicks: number }
export interface FailureReport { id: string; title: string; summary: string; ok: boolean; pids: number[] }
export interface ExpResult { label: string; finished: boolean; metrics: Metrics }
// Runs an API call, surfaces errors as toasts and refreshes the dashboard state.
export type Act = (fn: () => Promise<unknown>, successMessage?: string) => Promise<void>;

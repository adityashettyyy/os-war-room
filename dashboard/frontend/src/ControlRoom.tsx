import { useEffect, useMemo, useRef, useState } from 'react';
import { get, post } from './api';
import { Act, Device, FailureReport, Intr, KState, LogEvent, PcbDetail, Proc } from './types';
import { Bar, Empty, Led, Panel, StateBadge, pct, pidColor, pname } from './ui';

type Sel = { selected: number | null; onSelect: (pid: number | null) => void };

// ───────────────────────────── CPU ─────────────────────────────
export function CpuPanel({ s, onSelect }: { s: KState; onSelect: Sel['onSelect'] }) {
  const run = s.processes.find((p) => p.pid === s.cpu.running);
  const sw = s.cpu.lastSwitch;
  const done = run ? 1 - run.remaining / Math.max(1, run.cpuBurst) : 0;
  return (
    <Panel title="CPU" right={s.cpu.switchLeft > 0 ? <span className="badge st-switching">SWITCHING</span> : <span className="dim">{s.config.scheduler} · q={s.config.quantum}</span>}>
      <div className="cpu-core" style={{ borderColor: run ? pidColor(run.pid) : 'var(--line)' }} onClick={() => run && onSelect(run.pid)}>
        {run ? (
          <>
            <div className="cpu-pid" style={{ color: pidColor(run.pid) }}>{pname(run.pid)}</div>
            <div className="dim">{run.name} · PC {run.pc}/{run.pc + run.remaining} · pri {run.priority}</div>
            <Bar value={done} tone={pidColor(run.pid)} label={`${(done * 100).toFixed(0)}% done`} />
            {s.cpu.slice > 0 && <div className="dim small">time slice {s.cpu.quantumUsed}/{s.cpu.slice} ms</div>}
          </>
        ) : <div className="cpu-idle">IDLE</div>}
      </div>
      {sw.tick >= 0 && (
        <div className="ctxsw" key={sw.tick} title="Most recent context switch">
          <span className="step s1">SAVE {pname(sw.from)} <small>PC={sw.fromPc}</small></span>
          <span className="arrow">▸</span>
          <span className="step s2">SELECT <small>{s.config.scheduler}</small></span>
          <span className="arrow">▸</span>
          <span className="step s3">LOAD {pname(sw.to)} <small>PC={sw.toPc}</small></span>
          <span className="dim small">@T+{String(sw.tick).padStart(4, '0')}</span>
        </div>
      )}
      <div className="kv">
        <span>Utilisation (50 ms)</span><Bar value={s.cpu.util} tone="var(--cyan)" label={pct(s.cpu.util)} />
        <span>Utilisation (total)</span><Bar value={s.cpu.utilTotal} tone="var(--cyan)" label={pct(s.cpu.utilTotal)} />
        <span>Context switches</span><b>{s.cpu.ctxSwitches}</b>
        <span>Busy / overhead / idle</span><b>{s.cpu.busyTicks} / {s.cpu.overheadTicks} / {s.cpu.idleTicks} ms</b>
      </div>
    </Panel>
  );
}

// ───────────────────────────── Process table ─────────────────────────────
export function ProcessTable({ s, selected, onSelect }: { s: KState } & Sel) {
  const [hideDone, setHideDone] = useState(true);
  const rows = s.processes.filter((p) => !hideDone || p.state !== 'TERMINATED');
  const counts = useMemo(() => {
    const c: Record<string, number> = {};
    s.processes.forEach((p) => (c[p.state] = (c[p.state] ?? 0) + 1));
    return c;
  }, [s.processes]);
  return (
    <Panel title="Processes" className="wide"
      right={<label className="chk"><input type="checkbox" checked={hideDone} onChange={(e) => setHideDone(e.target.checked)} /> hide terminated</label>}>
      <div className="chips">
        {['NEW', 'READY', 'RUNNING', 'WAITING', 'SUSPENDED', 'TERMINATED'].map((k) => <span key={k} className={`badge st-${k.toLowerCase()}`}>{k} {counts[k] ?? 0}</span>)}
      </div>
      <div className="scroll tall">
        <table>
          <thead><tr><th>PID</th><th>Name</th><th>Type</th><th>State</th><th>Pri</th><th>Progress</th><th>Pages</th><th>PF</th><th>Wait</th><th>Holds</th></tr></thead>
          <tbody>
            {rows.length === 0 && <tr><td colSpan={10}><Empty>No processes yet — create one or load a scenario.</Empty></td></tr>}
            {rows.map((p) => <ProcRow key={p.pid} p={p} sel={selected === p.pid} onSelect={onSelect} />)}
          </tbody>
        </table>
      </div>
    </Panel>
  );
}
function ProcRow({ p, sel, onSelect }: { p: Proc; sel: boolean; onSelect: Sel['onSelect'] }) {
  const done = 1 - p.remaining / Math.max(1, p.cpuBurst);
  return (
    <tr className={sel ? 'sel' : ''} onClick={() => onSelect(sel ? null : p.pid)}>
      <td><b style={{ color: pidColor(p.pid) }}>{pname(p.pid)}</b></td>
      <td className="trunc" title={p.name}>{p.name}</td>
      <td className="dim">{p.type.replace('_BOUND', '')}</td>
      <td>
        <StateBadge state={p.state} />
        {p.state === 'WAITING' && <span className="dim small"> {p.wait.toLowerCase()}{p.waitTarget ? ` ${p.waitTarget}` : ''}</span>}
        {p.starved && <span className="badge st-starved">STARVED</span>}
      </td>
      <td>{p.priority}{p.effPriority !== p.priority ? <span className="dim">→{p.effPriority}</span> : null}</td>
      <td style={{ width: 90 }}><Bar value={done} tone={pidColor(p.pid)} /></td>
      <td>{p.resident}/{p.vpages}</td><td>{p.pageFaults}</td><td>{p.waiting}</td>
      <td className="dim small">{p.held.join(' ')}</td>
    </tr>
  );
}

// ───────────────────────────── Ready queue ─────────────────────────────
export function ReadyQueue({ s, onSelect }: { s: KState; onSelect: Sel['onSelect'] }) {
  const byPid = new Map(s.processes.map((p) => [p.pid, p]));
  return (
    <Panel title="Ready Queue" right={<span className="dim">{s.readyQueue.length} waiting · {s.config.scheduler} order</span>}>
      {s.readyQueue.length === 0 ? <Empty>Ready queue empty</Empty> : (
        <div className="queue">
          <span className="dim small">next ▸</span>
          {s.readyQueue.map((pid) => {
            const p = byPid.get(pid);
            return (
              <button key={pid} className="qitem" style={{ borderColor: pidColor(pid), color: pidColor(pid) }} onClick={() => onSelect(pid)}
                title={p ? `${p.name}: remaining ${p.remaining} ms, priority ${p.effPriority}, level ${p.level}` : ''}>
                {pname(pid)}
                {p && <small>{s.config.scheduler === 'MLFQ' ? `L${p.level}` : s.config.scheduler === 'PRIORITY' ? `p${p.effPriority}` : `${p.remaining}`}</small>}
              </button>
            );
          })}
        </div>
      )}
    </Panel>
  );
}

// ───────────────────────────── Memory ─────────────────────────────
export function MemoryMap({ s, act }: { s: KState; act: Act }) {
  const m = s.memory;
  const pids = Array.from(new Set(m.frames.filter(Boolean).map((f) => f!.pid))).sort((a, b) => a - b);
  return (
    <Panel title="Physical Memory" tone={m.thrashing ? 'bad' : undefined}
      right={<select value={s.config.replacement} onChange={(e) => act(() => post('/api/config', { replacement: e.target.value }), `Replacement → ${e.target.value}`)} aria-label="Page replacement algorithm">
        {['FIFO', 'LRU', 'CLOCK', 'OPTIMAL'].map((p) => <option key={p}>{p}</option>)}</select>}>
      {m.thrashing && <div className="banner bad">⚠ THRASHING — page-fault rate {pct(m.pfRate)} · CPU {pct(s.cpu.util)} · memory {pct(m.util)}</div>}
      <div className="frames" style={{ gridTemplateColumns: `repeat(${m.total > 128 ? 32 : 16}, 1fr)` }}>
        {m.frames.map((f, i) => (
          <div key={i} className={`frame ${f ? 'used' : ''} ${f?.pinned ? 'pinned' : ''}`}
            style={f ? { background: pidColor(f.pid) } : undefined}
            title={f ? `Frame ${i}: ${pname(f.pid)} page ${f.page}${f.pinned ? ' (pinned: page-in in flight)' : ''} · loaded T+${f.loadedAt} · last use T+${f.lastUsed}` : `Frame ${i}: free`}>
            {f && m.total <= 64 ? f.page : ''}
          </div>
        ))}
      </div>
      <div className="chips">{pids.slice(0, 16).map((pid) => <span key={pid} className="chip"><i style={{ background: pidColor(pid) }} />{pname(pid)}</span>)}</div>
      <div className="kv">
        <span>Frames used</span><Bar value={m.util} label={`${m.used}/${m.total} (${s.config.pageSizeKB} KB each)`} />
        <span>Page-fault rate</span><Bar value={m.pfRate} label={pct(m.pfRate)} />
        <span>Faults / refs / replaced</span><b>{m.faults} / {m.refs} / {m.replacements}</b>
        <span>Swapped-out (load control)</span><b>{m.suspensions}</b>
      </div>
    </Panel>
  );
}

// ───────────────────────────── I/O ─────────────────────────────
export function IoPanel({ s }: { s: KState }) {
  return (
    <Panel title="I/O Devices" right={<span className="dim">{s.io.totalQueue} requests in system</span>}>
      <div className="devices">{s.io.devices.map((d) => <DeviceCard key={d.name} d={d} />)}</div>
    </Panel>
  );
}
function DeviceCard({ d }: { d: Device }) {
  const state = d.failed ? 'OFFLINE' : d.busy ? 'BUSY' : 'IDLE';
  return (
    <div className={`device ${d.failed ? 'failed' : ''}`}>
      <div className="dhead"><b>{d.name}</b><span className={`badge dev-${state.toLowerCase()}`}>{state}</span></div>
      {d.current ? (
        <div className="small"><span style={{ color: pidColor(d.current.pid) }}>{pname(d.current.pid)}</span> {d.current.write ? 'writing' : 'reading'} {d.current.doc || ''}
          <Bar value={1 - d.current.remaining / Math.max(1, d.current.duration)} tone="var(--cyan)" label={`${d.current.remaining} ms left`} /></div>
      ) : <div className="dim small">{d.failed ? `repair until T+${d.failedUntil}` : 'no request in service'}</div>}
      <div className="queue small">
        <span className="dim">queue</span>
        {d.queue.slice(0, 8).map((r, i) => <span key={i} className="qdot" style={{ background: pidColor(r.pid) }} title={`${pname(r.pid)} ${r.write ? 'write' : 'read'} ${r.doc}`}>{r.pid}</span>)}
        {d.queue.length > 8 && <span className="dim">+{d.queue.length - 8}</span>}
        {d.parked.length > 0 && <span className="badge st-waiting" title="producers blocked because the buffer is full">{d.parked.length} blocked</span>}
      </div>
      {(d.buffered || d.spooled) && <Bar value={d.occupancy / Math.max(1, d.capacity)} label={`${d.spooled ? 'spool' : 'buffer'} ${d.occupancy}/${d.capacity}`} />}
      <div className="dim tiny">util {pct(d.util)} · done {d.completed} · errors {d.errors}{d.fullEvents ? ` · full ×${d.fullEvents}` : ''}{d.lostWrites ? ` · lost ${d.lostWrites}` : ''}</div>
    </div>
  );
}

// ───────────────────────────── Interrupts ─────────────────────────────
export function InterruptPanel({ s }: { s: KState }) {
  const [quiet, setQuiet] = useState(false);
  const rows = s.interrupts.recent.filter((i) => quiet || !i.quiet).slice(-14).reverse();
  return (
    <Panel title="Interrupts" right={<label className="chk"><input type="checkbox" checked={quiet} onChange={(e) => setQuiet(e.target.checked)} /> show periodic timer</label>}>
      <div className="chips">{Object.entries(s.interrupts.counts).map(([k, v]) => <span key={k} className="chip intr">{k} <b>{v}</b></span>)}</div>
      <div className="dim tiny" title="System calls dispatched through the trap layer">system calls: {Object.entries(s.syscalls).filter(([, v]) => v > 0).map(([k, v]) => `${k} ${v}`).join(' · ') || 'none yet'}</div>
      {s.interrupts.pending.length > 0 && <div className="pending">pending: {s.interrupts.pending.map((i) => `${i.type}${i.pid > 0 ? ` (${pname(i.pid)})` : ''}`).join(', ')}</div>}
      <div className="scroll">
        <table className="compact">
          <thead><tr><th>T</th><th>Type</th><th>Source</th><th>Handler → action</th></tr></thead>
          <tbody>
            {rows.length === 0 && <tr><td colSpan={4}><Empty>No interrupts yet</Empty></td></tr>}
            {rows.map((i) => <IntrRow key={i.id} i={i} />)}
          </tbody>
        </table>
      </div>
    </Panel>
  );
}
function IntrRow({ i }: { i: Intr }) {
  return (
    <tr>
      <td className="dim">{i.tick}</td><td><span className={`badge it-${i.type.toLowerCase()}`}>{i.type}</span></td>
      <td>{i.device ? i.device : i.pid > 0 ? pname(i.pid) : 'kernel'}{i.type === 'PAGE_FAULT' ? ` pg ${i.page}` : ''}<div className="dim tiny">{i.cause}</div></td>
      <td><b>{i.handler}</b><div className="dim tiny">{i.action}</div></td>
    </tr>
  );
}

// ───────────────────────────── Health ─────────────────────────────
export function HealthPanel({ s }: { s: KState }) {
  return (
    <Panel title="System Health" right={<span className={`badge status-${s.status.toLowerCase()}`}>{s.status}</span>}
      tone={s.status === 'CRITICAL' ? 'bad' : s.status === 'DEGRADED' ? 'warn' : 'ok'}>
      <div className="health">
        {s.health.map((h) => (
          <div key={h.name} className="hrow" title={h.rule}><Led level={h.level} /><span>{h.name}</span><b className={`lv-${h.level.toLowerCase()}`}>{h.value}</b></div>
        ))}
      </div>
      {s.memory.thrashing && <div className="banner bad">THRASHING · load control {s.config.thrashingControl ? 'active' : 'disabled'}</div>}
      {s.sync.deadlocks.some((d) => !d.resolved) && <div className="banner bad">DEADLOCK · {s.sync.deadlocks.filter((d) => !d.resolved).map((d) => d.pids.map(pname).join('↔')).join(', ')}</div>}
    </Panel>
  );
}

// ───────────────────────────── Event log ─────────────────────────────
const CATS = ['PROCESS', 'SCHED', 'INTERRUPT', 'SYSCALL', 'MEMORY', 'IO', 'SYNC', 'IPC', 'ERROR', 'FAILURE', 'SYSTEM'];
export function EventLog({ logs, onClear }: { logs: LogEvent[]; onClear: () => void }) {
  const [on, setOn] = useState<Set<string>>(new Set(CATS.filter((c) => c !== 'SYSCALL')));
  const [pid, setPid] = useState('');
  const [follow, setFollow] = useState(true);
  const box = useRef<HTMLDivElement>(null);
  const shown = useMemo(() => logs.filter((l) => on.has(l.cat) && (pid === '' || String(l.pid) === pid.replace(/^p/i, ''))).slice(-400), [logs, on, pid]);
  useEffect(() => { if (follow && box.current) box.current.scrollTop = box.current.scrollHeight; }, [shown, follow]);
  const toggle = (c: string) => setOn((prev) => { const n = new Set(prev); n.has(c) ? n.delete(c) : n.add(c); return n; });
  return (
    <Panel title="Kernel Event Log" className="wide"
      right={<>
        <input className="mini" placeholder="pid" value={pid} onChange={(e) => setPid(e.target.value)} aria-label="Filter by pid" />
        <label className="chk"><input type="checkbox" checked={follow} onChange={(e) => setFollow(e.target.checked)} /> follow</label>
        <button className="ghost" onClick={onClear}>clear</button>
      </>}>
      <div className="chips">{CATS.map((c) => <button key={c} className={`catchip c-${c.toLowerCase()} ${on.has(c) ? 'on' : ''}`} onClick={() => toggle(c)}>{c}</button>)}</div>
      <div className="log" ref={box}>
        {shown.length === 0 && <Empty>No events match the filters.</Empty>}
        {shown.map((l) => <div key={l.id} className={`ll c-${l.cat.toLowerCase()}`}><span className="lt">{l.t}</span><span className="lc">{l.cat}</span><span className="lm">{l.msg}</span></div>)}
      </div>
    </Panel>
  );
}

// ───────────────────────────── PCB inspector ─────────────────────────────
export function PcbInspector({ pid, onClose, act, tick }: { pid: number; onClose: () => void; act: Act; tick: number }) {
  const [d, setD] = useState<PcbDetail | null>(null);
  useEffect(() => { get<PcbDetail>(`/api/process/${pid}`).then(setD).catch(() => setD(null)); }, [pid, tick]);
  if (!d || (d as unknown as { error?: string }).error) return <Panel title={`PCB — ${pname(pid)}`} right={<button className="ghost" onClick={onClose}>close</button>}><Empty>Process not found.</Empty></Panel>;
  return (
    <Panel title={`PCB — ${pname(d.pid)} “${d.name}”`} className="wide pcb"
      right={<>{d.state !== 'TERMINATED' && <button className="danger" onClick={() => act(() => post(`/api/process/${d.pid}/kill`), `${pname(d.pid)} terminated via terminate_process`)}>terminate_process</button>}
        <button className="ghost" onClick={onClose}>close</button></>}>
      <div className="pcb-grid">
        <div>
          <h3>Execution context</h3>
          <div className="kv2"><span>State</span><b><StateBadge state={d.state} /> {d.wait !== 'NONE' && <span className="dim">{d.wait} {d.waitTarget}</span>}</b>
            <span>PC</span><b>{d.pc}</b><span>R0 acc</span><b>{d.regs[0]}</b><span>R1 last page</span><b>{d.regs[1]}</b><span>R2 offset</span><b>{d.regs[2]}</b><span>R3 syscall rc</span><b>{d.regs[3]}</b>
            <span>Parent</span><b>{d.ppid > 0 ? pname(d.ppid) : '—'}</b><span>Exit</span><b>{d.state === 'TERMINATED' ? `${d.exitCode} (${d.exitReason})` : '—'}</b></div>
          <h3>Scheduling</h3>
          <div className="kv2"><span>Priority</span><b>{d.priority} (eff {d.effPriority})</b><span>MLFQ level</span><b>{d.level}</b><span>Burst τ</span><b>{d.tau.toFixed(1)}</b>
            <span>Arrival / first run / end</span><b>{d.arrival} / {d.firstRun} / {d.completion}</b><span>CPU / waiting / blocked</span><b>{d.cpuTime} / {d.waiting} / {d.blocked} ms</b>
            <span>Dispatches / syscalls</span><b>{d.dispatches} / {d.syscalls}</b></div>
        </div>
        <div>
          <h3>Memory — {d.memoryKB} KB, {d.vpages} pages</h3>
          <div className="pt">{d.pageTable.map((e) => <span key={e.page} className={`pte ${e.present ? 'on' : ''}`} title={`page ${e.page} → ${e.present ? `frame ${e.frame}` : 'not present'}`}>{e.page}{e.present && <small>f{e.frame}</small>}</span>)}</div>
          <div className="kv2"><span>Page faults / refs</span><b>{d.pageFaults} / {d.memRefs}</b><span>I/O requests / errors</span><b>{d.ioRequests} / {d.ioErrors}</b>
            <span>Holds</span><b>{d.held.join(', ') || '—'}</b><span>Waiting on</span><b>{d.waitingOn || '—'}</b>
            <span>IPC sent / recv</span><b>{d.ipc.sent} / {d.ipc.received}</b><span>Inbox</span><b>{d.ipc.inbox || '—'}</b></div>
        </div>
        <div>
          <h3>Program (around PC)</h3>
          <div className="prog">{d.program.map((i) => <div key={i.i} className={i.current ? 'cur' : ''}><span className="dim">{String(i.i).padStart(3, '0')}</span> {i.current ? '▶' : ' '} {i.text}</div>)}</div>
        </div>
      </div>
    </Panel>
  );
}

// ───────────────────────────── Create process ─────────────────────────────
export function CreateProcess({ act }: { act: Act }) {
  const [f, setF] = useState({ name: '', type: 'CPU_BOUND', cpuBurst: 20, ioBurst: 0, priority: 5, memoryKB: 32, arrival: '' as string, script: '' });
  const set = <K extends keyof typeof f>(k: K, v: (typeof f)[K]) => setF((p) => ({ ...p, [k]: v }));
  const num = (k: 'cpuBurst' | 'ioBurst' | 'priority' | 'memoryKB') => (e: React.ChangeEvent<HTMLInputElement>) => set(k, Number(e.target.value));
  const submit = () => act(() => post('/api/process', {
    name: f.name || undefined, type: f.type, cpuBurst: f.cpuBurst, ioBurst: f.ioBurst, priority: f.priority, memoryKB: f.memoryKB,
    arrival: f.arrival === '' ? undefined : Number(f.arrival), script: f.script.trim() ? f.script : undefined,
  }), 'Process created');
  const custom = f.script.trim().length > 0;
  const PRESETS: { label: string; hint: string; spec: Record<string, unknown> }[] = [
    { label: 'CPU-bound', hint: 'long compute, little I/O', spec: { type: 'CPU_BOUND', cpuBurst: 40, ioBurst: 0, priority: 6, memoryKB: 32 } },
    { label: 'I/O-bound', hint: 'short compute bursts between disk/printer/network I/O', spec: { type: 'IO_BOUND', cpuBurst: 24, ioBurst: 60, priority: 4, memoryKB: 32 } },
    { label: 'Memory-intensive', hint: 'large address space with a wide working set', spec: { type: 'CPU_BOUND', cpuBurst: 50, ioBurst: 0, priority: 5, memoryKB: 96, localityPages: 12 } },
    { label: 'Interactive', hint: 'tiny compute bursts between keyboard waits', spec: { type: 'INTERACTIVE', cpuBurst: 15, ioBurst: 30, priority: 2, memoryKB: 24 } },
  ];
  return (
    <Panel title="Create Process">
      <div className="fgrid">
        {PRESETS.map((pr) => <button key={pr.label} title={pr.hint} onClick={() => act(() => post('/api/process', { ...pr.spec, name: pr.label.toLowerCase().replace(/[^a-z]+/g, '-') }), `${pr.label} process created`)}>+ {pr.label}</button>)}
      </div>
      <div className="form">
        <label>Name<input value={f.name} placeholder="auto" onChange={(e) => set('name', e.target.value)} /></label>
        <label>Type<select value={f.type} disabled={custom} onChange={(e) => set('type', e.target.value)}><option value="CPU_BOUND">CPU-bound</option><option value="IO_BOUND">I/O-bound</option><option value="INTERACTIVE">Interactive</option></select></label>
        <label>CPU burst (ms)<input type="number" min={1} max={5000} value={f.cpuBurst} disabled={custom} onChange={num('cpuBurst')} /></label>
        <label>I/O burst (ms)<input type="number" min={0} value={f.ioBurst} disabled={custom} onChange={num('ioBurst')} /></label>
        <label>Priority (0 = highest)<input type="number" min={0} max={9} value={f.priority} onChange={num('priority')} /></label>
        <label>Memory (KB)<input type="number" min={1} value={f.memoryKB} onChange={num('memoryKB')} /></label>
        <label>Arrival tick<input type="number" min={0} value={f.arrival} placeholder="now" onChange={(e) => set('arrival', e.target.value)} /></label>
      </div>
      <details><summary>Advanced: custom program</summary>
        <textarea rows={6} spellCheck={false} value={f.script} onChange={(e) => set('script', e.target.value)}
          placeholder={'COMPUTE 3\nLOCK R1\nREAD DISK 4\nUNLOCK R1\nSEND inbox hello\n\nOps: COMPUTE n · READ/WRITE dev ms [doc] · SLEEP n · LOCK/UNLOCK r · WAIT/SIGNAL sem · RDLOCK/WRLOCK/RWUNLOCK l · SEND q text · RECV q · SHMR seg i · SHMW seg i delta · ALLOC/FREE pages · FORK n · CRASH [why]'} />
      </details>
      <button className="primary" onClick={submit}>Create process</button>
    </Panel>
  );
}

// ───────────────────────────── Scheduler / kernel policy lab ─────────────────────────────
export function SchedulerLab({ s, act }: { s: KState; act: Act }) {
  const [q, setQ] = useState(s.config.quantum);
  useEffect(() => setQ(s.config.quantum), [s.config.quantum]);
  const c = s.config;
  const cfg = (patch: Record<string, unknown>, msg: string) => act(() => post('/api/config', patch), msg);
  return (
    <Panel title="Scheduler & Kernel Policy">
      <div className="form">
        <label>Algorithm<select value={c.scheduler} onChange={(e) => cfg({ scheduler: e.target.value }, `Scheduler → ${e.target.value}`)}>
          {['FCFS', 'SJF', 'SRTF', 'RR', 'PRIORITY', 'MLFQ', 'ADAPTIVE'].map((p) => <option key={p}>{p}</option>)}</select></label>
        <label>Quantum (ms)<input type="number" min={1} max={100} value={q} onChange={(e) => setQ(Number(e.target.value))} onBlur={() => q !== c.quantum && cfg({ quantum: q }, `Quantum → ${q} ms`)} /></label>
        <label>Context-switch cost (ms)<input type="number" min={0} max={10} value={c.ctxSwitchCost} onChange={(e) => cfg({ ctxSwitchCost: Number(e.target.value) }, 'Context-switch cost updated')} /></label>
        <label>Deadlock recovery<select value={c.recovery} onChange={(e) => cfg({ recovery: e.target.value }, `Recovery → ${e.target.value}`)}><option>TERMINATE</option><option>ROLLBACK</option></select></label>
      </div>
      <div className="toggles">
        <label className="chk"><input type="checkbox" checked={c.aging} onChange={(e) => cfg({ aging: e.target.checked }, `Aging ${e.target.checked ? 'on' : 'off'}`)} /> priority aging</label>
        <label className="chk"><input type="checkbox" checked={c.thrashingControl} onChange={(e) => cfg({ thrashingControl: e.target.checked }, `Thrashing control ${e.target.checked ? 'on' : 'off'}`)} /> thrashing control</label>
        <label className="chk"><input type="checkbox" checked={c.autoDeadlockRecovery} onChange={(e) => cfg({ autoDeadlockRecovery: e.target.checked }, `Auto deadlock recovery ${e.target.checked ? 'on' : 'off'}`)} /> auto deadlock recovery</label>
      </div>
      <div className="dim small">Memory: {c.physicalKB} KB / {c.pageSizeKB} KB pages ({c.frames} frames) — change on Reset.</div>
    </Panel>
  );
}

// ───────────────────────────── Failure lab ─────────────────────────────
export function FailureLab({ s, act, selected }: { s: KState; act: Act; selected: number | null }) {
  const [count, setCount] = useState(12);
  const [device, setDevice] = useState('DISK');
  const [last, setLast] = useState<FailureReport | null>(null);
  const run = (action: string, extra: Record<string, unknown> = {}) =>
    act(async () => { const r = await post<FailureReport>('/api/failure', { action, count, ...extra }); setLast(r); if (!r.ok) throw new Error(r.summary); }, undefined);
  const target = selected ?? undefined;
  return (
    <Panel title="Failure Lab" tone="warn" right={<span className="dim small">fault injection · simulator only</span>}>
      <div className="form inline">
        <label>Count<input type="number" min={1} max={40} value={count} onChange={(e) => setCount(Number(e.target.value))} /></label>
        <label>Device<select value={device} onChange={(e) => setDevice(e.target.value)}>{['DISK', 'PRINTER', 'KEYBOARD', 'NETWORK'].map((d) => <option key={d}>{d}</option>)}</select></label>
      </div>
      <div className="fgrid">
        <button onClick={() => run('page_fault', { pid: target })}>Force Page Fault{selected ? ` (${pname(selected)})` : ''}</button>
        <button onClick={() => run('memory_pressure')}>Exhaust Memory</button>
        <button onClick={() => run('deadlock', { count: Math.min(count, 5) })}>Create Deadlock</button>
        <button onClick={() => run('crash', { pid: target })}>Crash Process{selected ? ` (${pname(selected)})` : ''}</button>
        <button onClick={() => run('io_flood')}>Flood I/O Queue</button>
        <button onClick={() => run('device_failure', { device })}>Fail {device}</button>
        <button onClick={() => run('buffer_stress', { count: Math.min(count, 12) })}>Buffer Stress</button>
        <button onClick={() => run('starvation')}>Starvation</button>
      </div>
      {last && <div className={`banner ${last.ok ? 'warn' : 'bad'}`}><b>{last.title}</b> — {last.summary}</div>}
      {s.crashDumps.length > 0 && <details><summary>Crash dumps ({s.crashDumps.length})</summary>
        {s.crashDumps.slice(-3).reverse().map((c, i) => <div key={i} className="dump">T+{c.tick} {pname(c.pid)} <b>{c.reason}</b><div className="dim tiny">PC={c.pc} · last “{c.lastInstr}” · {c.resident} pages resident · held: {c.held.join(',') || 'none'}</div></div>)}
      </details>}
    </Panel>
  );
}

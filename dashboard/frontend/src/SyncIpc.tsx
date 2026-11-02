import { useState } from 'react';
import { post } from './api';
import { Act, KState } from './types';
import { Empty, Panel, pidColor, pname } from './ui';

// Resource Allocation Graph: processes (circles) and resources (squares).
// Solid arrow resource→process = "held by"; dashed process→resource = "waiting for". Red = deadlocked cycle.
function Rag({ s }: { s: KState }) {
  const dead = new Set<number>();
  s.sync.deadlocks.filter((d) => !d.resolved).forEach((d) => d.pids.forEach((p) => dead.add(p)));
  const held: [string, number][] = [], wait: [number, string][] = [];
  const procs = new Set<number>(), res = new Set<string>();
  for (const m of s.sync.mutexes) {
    if (m.owner >= 0) { held.push([m.name, m.owner]); procs.add(m.owner); res.add(m.name); }
    m.waiters.forEach((w) => { wait.push([w, m.name]); procs.add(w); res.add(m.name); });
  }
  for (const l of s.sync.rwlocks) {
    if (l.writer >= 0) { held.push([l.name, l.writer]); procs.add(l.writer); res.add(l.name); }
    l.readers.forEach((r) => { held.push([l.name, r]); procs.add(r); res.add(l.name); });
    l.waiters.forEach((w) => { wait.push([w.pid, l.name]); procs.add(w.pid); res.add(l.name); });
  }
  if (res.size === 0) return <Empty>No locks held or awaited. Create a deadlock in the Failure Lab or load the “deadlock” scenario.</Empty>;
  const P = [...procs].sort((a, b) => a - b), R = [...res].sort();
  const W = 560, H = 210;
  const px = (i: number) => (W / (P.length + 1)) * (i + 1), rx = (i: number) => (W / (R.length + 1)) * (i + 1);
  const pPos = new Map(P.map((p, i) => [p, { x: px(i), y: 52 }])), rPos = new Map(R.map((r, i) => [r, { x: rx(i), y: 158 }]));
  const line = (a: { x: number; y: number }, b: { x: number; y: number }, r: number) => {
    const dx = b.x - a.x, dy = b.y - a.y, d = Math.hypot(dx, dy) || 1;
    return { x1: a.x + (dx / d) * r, y1: a.y + (dy / d) * r, x2: b.x - (dx / d) * (r + 3), y2: b.y - (dy / d) * (r + 3) };
  };
  return (
    <svg viewBox={`0 0 ${W} ${H}`} className="rag" role="img" aria-label="Resource allocation graph">
      <defs>
        {['ok', 'bad'].map((k) => <marker key={k} id={`ah-${k}`} viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0L10 5L0 10z" fill={k === 'bad' ? 'var(--red)' : 'var(--dim)'} /></marker>)}
      </defs>
      {held.map(([r, p], i) => { const l = line(rPos.get(r)!, pPos.get(p)!, 14); const bad = dead.has(p); return <line key={`h${i}`} {...l} className={bad ? 'e bad' : 'e'} markerEnd={`url(#ah-${bad ? 'bad' : 'ok'})`} />; })}
      {wait.map(([p, r], i) => { const l = line(pPos.get(p)!, rPos.get(r)!, 17); const bad = dead.has(p); return <line key={`w${i}`} {...l} className={bad ? 'e dashed bad' : 'e dashed'} markerEnd={`url(#ah-${bad ? 'bad' : 'ok'})`} />; })}
      {P.map((p) => <g key={p}><circle cx={pPos.get(p)!.x} cy={52} r="17" fill="var(--panel2)" stroke={dead.has(p) ? 'var(--red)' : pidColor(p)} strokeWidth="2.5" /><text x={pPos.get(p)!.x} y={56} textAnchor="middle" className="nodet">{pname(p)}</text></g>)}
      {R.map((r) => <g key={r}><rect x={rPos.get(r)!.x - 22} y={158 - 14} width="44" height="28" rx="4" fill="var(--panel2)" stroke="var(--cyan)" strokeWidth="2" /><text x={rPos.get(r)!.x} y={162} textAnchor="middle" className="nodet">{r}</text></g>)}
    </svg>
  );
}

const BANKER_DEFAULT = JSON.stringify({
  available: [3, 3, 2],
  max: [[7, 5, 3], [3, 2, 2], [9, 0, 2], [2, 2, 2], [4, 3, 3]],
  allocation: [[0, 1, 0], [2, 0, 0], [3, 0, 2], [2, 1, 1], [0, 0, 2]],
  request: { process: 1, amounts: [1, 0, 2] },
}, null, 1);

function BankerLab() {
  const [text, setText] = useState(BANKER_DEFAULT);
  const [out, setOut] = useState<{ safe: boolean; sequence: number[]; need: number[][]; request?: { granted: boolean; reason: string } } | null>(null);
  const [err, setErr] = useState('');
  const run = async () => {
    setErr('');
    try { setOut(await post('/api/banker', JSON.parse(text))); } catch (e) { setOut(null); setErr((e as Error).message); }
  };
  return (
    <Panel title="Banker's Algorithm Lab" right={<button className="primary" onClick={run}>Analyse</button>}>
      <textarea rows={9} spellCheck={false} value={text} onChange={(e) => setText(e.target.value)} aria-label="Banker's algorithm input" />
      {err && <div className="banner bad">{err}</div>}
      {out && <div className="kv2"><span>State</span><b className={out.safe ? 'lv-green' : 'lv-red'}>{out.safe ? 'SAFE' : 'UNSAFE'}</b>
        <span>Safe sequence</span><b>{out.safe ? out.sequence.map((p) => `P${p}`).join(' → ') : '—'}</b>
        <span>Need matrix</span><b className="mono">{out.need.map((r) => `[${r.join(' ')}]`).join(' ')}</b>
        {out.request && <><span>Request</span><b className={out.request.granted ? 'lv-green' : 'lv-red'}>{out.request.granted ? 'GRANTED' : 'DENIED'} — {out.request.reason}</b></>}</div>}
    </Panel>
  );
}

export function SyncIpcTab({ s, act }: { s: KState; act: Act }) {
  const [res, setRes] = useState({ kind: 'semaphore', name: '', value: 1 });
  const [msg, setMsg] = useState({ queue: 'inbox', text: 'hello' });
  const open = s.sync.deadlocks.filter((d) => !d.resolved);
  const create = () => act(() => post('/api/sync/setup', res.kind === 'semaphore' ? { semaphores: [{ name: res.name, value: res.value }] } : res.kind === 'mutex' ? { mutexes: [res.name] } : { queues: [{ name: res.name, capacity: res.value }] }), `${res.kind} “${res.name}” created`);
  return (
    <div className="grid two">
      <Panel title="Resource Allocation Graph" className="wide" tone={open.length ? 'bad' : undefined}
        right={open.length > 0 ? <button className="danger" onClick={() => act(() => post('/api/deadlock/recover'), 'Recovery executed')}>Recover now</button> : <span className="dim small">no deadlock</span>}>
        <Rag s={s} />
        {s.sync.deadlocks.length > 0 && (
          <table className="compact"><thead><tr><th>#</th><th>Detected</th><th>Processes</th><th>Cycle</th><th>Status</th></tr></thead><tbody>
            {s.sync.deadlocks.slice().reverse().map((d) => (
              <tr key={d.id}><td>{d.id}</td><td>T+{d.detectedAt}</td><td>{d.pids.map(pname).join(', ')}</td><td className="small">{d.description}</td>
                <td>{d.resolved ? <span className="lv-green">resolved T+{d.resolvedAt} · {d.action}</span> : <span className="lv-red">ACTIVE{d.recoverAt >= 0 ? ` · recovery at T+${d.recoverAt}` : ''}</span>}</td></tr>))}
          </tbody></table>)}
      </Panel>
      <Panel title="Mutexes · Semaphores · RW Locks">
        <table className="compact"><thead><tr><th>Resource</th><th>Kind</th><th>State</th><th>Waiting</th></tr></thead><tbody>
          {s.sync.mutexes.length + s.sync.semaphores.length + s.sync.rwlocks.length === 0 && <tr><td colSpan={4}><Empty>No synchronisation objects yet.</Empty></td></tr>}
          {s.sync.mutexes.map((m) => <tr key={m.name}><td><b>{m.name}</b></td><td>mutex</td><td>{m.owner >= 0 ? <>owned by <b style={{ color: pidColor(m.owner) }}>{pname(m.owner)}</b></> : 'free'}</td><td>{m.waiters.map(pname).join(' ') || '—'}</td></tr>)}
          {s.sync.semaphores.map((m) => <tr key={m.name}><td><b>{m.name}</b></td><td>semaphore</td><td>value {m.value} / {m.initial}</td><td>{m.waiters.map(pname).join(' ') || '—'}</td></tr>)}
          {s.sync.rwlocks.map((l) => <tr key={l.name}><td><b>{l.name}</b></td><td>rwlock</td><td>{l.writer >= 0 ? `writer ${pname(l.writer)}` : `${l.readers.length} reader(s)`}</td><td>{l.waiters.map((w) => `${pname(w.pid)}${w.write ? '(w)' : '(r)'}`).join(' ') || '—'}</td></tr>)}
        </tbody></table>
        <div className="form inline">
          <label>New<select value={res.kind} onChange={(e) => setRes({ ...res, kind: e.target.value })}><option>semaphore</option><option>mutex</option><option>queue</option></select></label>
          <label>Name<input value={res.name} onChange={(e) => setRes({ ...res, name: e.target.value })} /></label>
          {res.kind !== 'mutex' && <label>{res.kind === 'queue' ? 'Capacity' : 'Initial'}<input type="number" min={0} value={res.value} onChange={(e) => setRes({ ...res, value: Number(e.target.value) })} /></label>}
          <button disabled={!res.name} onClick={create}>Create</button>
        </div>
      </Panel>
      <Panel title="Inter-Process Communication">
        {s.ipc.queues.length === 0 && s.ipc.segments.length === 0 && <Empty>No queues or shared memory yet. Load “producer_consumer” or “race_condition”, or send a message below.</Empty>}
        {s.ipc.queues.map((q) => (
          <div key={q.name} className="ipcq"><b>{q.name}</b> <span className="dim small">{q.messages.length}/{q.capacity} · sent {q.sent} · received {q.received}{q.recvWaiters.length ? ` · ${q.recvWaiters.map(pname).join(',')} waiting to receive` : ''}{q.parked.length ? ` · ${q.parked.length} sender(s) blocked` : ''}</span>
            <div className="slots">{Array.from({ length: q.capacity }, (_, i) => <span key={i} className={`slot ${q.messages[i] ? 'full' : ''}`} title={q.messages[i] ? `from ${pname(q.messages[i].from)}` : 'empty'}>{q.messages[i]?.text.slice(0, 8) ?? ''}</span>)}</div></div>))}
        {s.ipc.segments.map((g) => (
          <div key={g.name} className="ipcq"><b>shm:{g.name}</b> <span className="dim small">reads {g.reads} · writes {g.writes}</span>
            <div className="slots">{g.data.map((v, i) => <span key={i} className={`slot ${v ? 'full' : ''}`} title={`word ${i}`}>{v}</span>)}</div>
            <div className="dim tiny">{g.history.slice(-4).map((h) => `T+${h.tick} ${pname(h.pid)} ${h.write ? 'wrote' : 'read'} [${h.index}]=${h.value}`).join(' · ')}</div></div>))}
        <div className="form inline">
          <label>Queue<input value={msg.queue} onChange={(e) => setMsg({ ...msg, queue: e.target.value })} /></label>
          <label>Message<input value={msg.text} onChange={(e) => setMsg({ ...msg, text: e.target.value })} /></label>
          <button onClick={() => act(() => post('/api/ipc/send', msg), 'send_message issued by shell')}>send_message</button>
        </div>
      </Panel>
      <BankerLab />
    </div>
  );
}

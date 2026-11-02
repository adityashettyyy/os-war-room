import { useEffect, useState } from 'react';
import { get, post } from './api';
import { Empty, Panel, pct } from './ui';

interface CaseInfo { id: number; title: string; difficulty: string }
interface CaseFile { id: number; title: string; difficulty: string; symptoms: Record<string, number>; hints: string[]; logs: Record<string, string[]> }
interface Verdict { correct: boolean; answer: string; explanation: string }

export function DetectiveTab() {
  const [list, setList] = useState<{ choices: string[]; cases: CaseInfo[] } | null>(null);
  const [open, setOpen] = useState<CaseFile | null>(null);
  const [tab, setTab] = useState('process');
  const [hints, setHints] = useState(0);
  const [verdict, setVerdict] = useState<Verdict | null>(null);
  const [wrong, setWrong] = useState<string[]>([]);
  const [solved, setSolved] = useState<Set<number>>(new Set());
  const [err, setErr] = useState('');
  useEffect(() => { get<{ choices: string[]; cases: CaseInfo[] }>('/api/detective').then(setList).catch((e) => setErr(e.message)); }, []);
  const load = async (id: number) => { setVerdict(null); setHints(0); setWrong([]); setTab('process'); setOpen(await get<CaseFile>(`/api/detective/${id}`)); };
  const diagnose = async (answer: string) => {
    if (!open) return;
    const v = await post<Verdict>(`/api/detective/${open.id}/diagnose`, { answer });
    setVerdict(v);
    if (v.correct) setSolved((s) => new Set(s).add(open.id)); else setWrong((w) => [...w, answer]);
  };
  if (err) return <div className="banner bad">{err}</div>;
  if (!list) return <Empty>Loading case files…</Empty>;
  const sym = open?.symptoms;
  return (
    <div className="grid detective">
      <Panel title="Case Files" right={<span className="dim">{solved.size}/{list.cases.length} solved</span>}>
        {list.cases.map((c) => (
          <button key={c.id} className={`case ${open?.id === c.id ? 'on' : ''} ${solved.has(c.id) ? 'solved' : ''}`} onClick={() => load(c.id)}>
            <b>CASE #{String(c.id).padStart(2, '0')}</b><span>{c.title}</span><small>{c.difficulty}{solved.has(c.id) ? ' · SOLVED ✓' : ''}</small>
          </button>))}
        <div className="dim small">Each case runs a hidden fault on a fresh kernel. Study the symptoms and raw logs, then name the cause. Kernel alerts are hidden — you have to reason like an engineer.</div>
      </Panel>
      {!open || !sym ? <Panel title="Investigation"><Empty>Open a case file to begin.</Empty></Panel> : (
        <>
          <Panel title={`CASE #${String(open.id).padStart(2, '0')} — ${open.title}`} right={<span className="badge">{open.difficulty}</span>}>
            <div className="symptoms">
              <div><span>CPU utilisation</span><b>{pct(sym.cpuUtil)}</b></div><div><span>Memory in use</span><b>{pct(sym.memoryUtil)}</b></div>
              <div><span>Page-fault rate</span><b>{pct(sym.pageFaultRate)}</b></div><div><span>Active processes</span><b>{sym.activeProcesses}</b></div>
              <div><span>Ready queue</span><b>{sym.readyQueue}</b></div><div><span>Blocked processes</span><b>{sym.blockedProcesses}</b></div>
              <div><span>Longest device queue</span><b>{sym.longestDeviceQueue}</b></div><div><span>Disk utilisation</span><b>{pct(sym.diskUtilisation)}</b></div>
              <div><span>Completed jobs</span><b>{sym.completed}</b></div><div><span>Uptime</span><b>{sym.tick} ms</b></div>
            </div>
            <div className="chips">{Object.keys(open.logs).map((k) => <button key={k} className={`catchip ${tab === k ? 'on' : ''}`} onClick={() => setTab(k)}>{k} log</button>)}</div>
            <div className="log tall">{(open.logs[tab] ?? []).length === 0 ? <Empty>Nothing logged in this category.</Empty> : open.logs[tab].map((l, i) => <div key={i} className="ll"><span className="lm">{l}</span></div>)}</div>
          </Panel>
          <Panel title="Diagnosis" tone={verdict?.correct ? 'ok' : undefined}>
            <div className="choices">{list.choices.map((c) => <button key={c} disabled={verdict?.correct || wrong.includes(c)} className={wrong.includes(c) ? 'wrong' : ''} onClick={() => diagnose(c)}>{c}</button>)}</div>
            {verdict && <div className={`banner ${verdict.correct ? 'ok' : 'bad'}`}>{verdict.correct ? <><b>Correct — {verdict.answer}.</b> {verdict.explanation}</> : verdict.explanation}</div>}
            <div className="hints">
              {open.hints.slice(0, hints).map((h, i) => <div key={i} className="hint">💡 {h}</div>)}
              {hints < open.hints.length && !verdict?.correct && <button className="ghost" onClick={() => setHints(hints + 1)}>Reveal hint ({hints + 1}/{open.hints.length})</button>}
            </div>
          </Panel>
        </>)}
    </div>
  );
}

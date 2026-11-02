import { useState } from 'react';
import { post } from './api';
import { Act, KState } from './types';
import { Bar, Empty, Panel, pct, pname } from './ui';

interface LabStep { ref: number; fault: boolean; victim: number; frames: number[] }
interface LabRes { policy: string; faults: number; hits: number; steps: LabStep[] }

export function MemoryLabTab({ s }: { s: KState; act: Act }) {
  const [refs, setRefs] = useState('7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1');
  const [frames, setFrames] = useState(3);
  const [res, setRes] = useState<LabRes[]>([]);
  const [show, setShow] = useState('LRU');
  const [err, setErr] = useState('');
  const [tr, setTr] = useState({ pid: 0, vaddr: 5000 });
  const [trOut, setTrOut] = useState<string>('');
  const run = async () => {
    setErr('');
    try {
      const list = refs.split(/[\s,]+/).filter(Boolean).map(Number);
      if (list.some((n) => !Number.isInteger(n) || n < 0)) throw new Error('References must be non-negative integers');
      const r = await post<{ results: LabRes[] }>('/api/replacement-lab', { refs: list, frames });
      setRes(r.results);
    } catch (e) { setErr((e as Error).message); }
  };
  const translate = async () => {
    try {
      const t = await post<{ valid: boolean; page: number; offset: number; present: boolean; frame: number; physical: number }>('/api/translate', tr);
      setTrOut(!t.valid ? 'Invalid address: outside the process’s virtual address space (segmentation fault).'
        : `virtual ${tr.vaddr} → page ${t.page}, offset ${t.offset} → ${t.present ? `frame ${t.frame} → physical address ${t.physical}` : 'page NOT PRESENT → would raise a page fault'}`);
    } catch (e) { setTrOut((e as Error).message); }
  };
  const cur = res.find((r) => r.policy === show);
  const best = res.length ? Math.min(...res.map((r) => r.faults)) : 0;
  const byProc = s.processes.filter((p) => p.state !== 'TERMINATED');
  return (
    <div className="grid two">
      <Panel title="Page Replacement Lab" className="wide" right={<button className="primary" onClick={run}>Run all four</button>}>
        <div className="form inline">
          <label className="grow">Reference string<input value={refs} onChange={(e) => setRefs(e.target.value)} /></label>
          <label>Frames<input type="number" min={1} max={16} value={frames} onChange={(e) => setFrames(Number(e.target.value))} /></label>
        </div>
        {err && <div className="banner bad">{err}</div>}
        {res.length === 0 ? <Empty>Press “Run all four” — the default string is the textbook example (FIFO 15, LRU 12, Optimal 9 faults with 3 frames).</Empty> : (
          <>
            <div className="algos">{res.map((r) => (
              <button key={r.policy} className={`algo ${show === r.policy ? 'on' : ''}`} onClick={() => setShow(r.policy)}>
                <b>{r.policy}</b><span>{r.faults} faults</span><small>{r.hits} hits · {pct(r.faults / (r.faults + r.hits))}</small>{r.faults === best && <em>best</em>}
              </button>))}</div>
            {cur && <div className="scroll"><table className="steps"><tbody>
              <tr><th>ref</th>{cur.steps.map((st, i) => <td key={i} className={st.fault ? 'fault' : 'hit'}>{st.ref}</td>)}</tr>
              {Array.from({ length: frames }, (_, f) => <tr key={f}><th>F{f}</th>{cur.steps.map((st, i) => <td key={i} className={st.fault && st.frames[f] === st.ref ? 'new' : ''}>{st.frames[f] >= 0 ? st.frames[f] : ''}</td>)}</tr>)}
              <tr><th>out</th>{cur.steps.map((st, i) => <td key={i} className="dim">{st.victim >= 0 ? st.victim : ''}</td>)}</tr>
              <tr><th></th>{cur.steps.map((st, i) => <td key={i} className={st.fault ? 'lv-red' : 'lv-green'}>{st.fault ? '✗' : '✓'}</td>)}</tr>
            </tbody></table></div>}
          </>)}
      </Panel>
      <Panel title="Address Translation" right={<button className="primary" onClick={translate}>Translate</button>}>
        <div className="form inline">
          <label>Process<select value={tr.pid} onChange={(e) => setTr({ ...tr, pid: Number(e.target.value) })}><option value={0}>choose…</option>{byProc.map((p) => <option key={p.pid} value={p.pid}>{pname(p.pid)} — {p.name}</option>)}</select></label>
          <label>Virtual address<input type="number" min={0} value={tr.vaddr} onChange={(e) => setTr({ ...tr, vaddr: Number(e.target.value) })} /></label>
        </div>
        <div className="dim small">Page size {s.config.pageSizeKB} KB = {s.config.pageSizeKB * 1024} bytes → page = address ÷ {s.config.pageSizeKB * 1024}, offset = address mod {s.config.pageSizeKB * 1024}.</div>
        {trOut && <div className="banner warn">{trOut}</div>}
      </Panel>
      <Panel title="Residency by Process">
        {byProc.length === 0 ? <Empty>No live processes.</Empty> : (
          <table className="compact"><thead><tr><th>Process</th><th>Resident / virtual</th><th>Faults</th></tr></thead><tbody>
            {byProc.map((p) => <tr key={p.pid}><td>{pname(p.pid)} <span className="dim">{p.name}</span></td><td><Bar value={p.resident / Math.max(1, p.vpages)} label={`${p.resident}/${p.vpages} pages`} /></td><td>{p.pageFaults}</td></tr>)}
          </tbody></table>)}
      </Panel>
    </div>
  );
}

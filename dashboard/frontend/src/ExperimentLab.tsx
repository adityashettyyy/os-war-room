import { useState } from 'react';
import { post } from './api';
import { Act, ExpResult, Metrics } from './types';
import { BarChart, Empty, Legend, LineChart, Panel, pct } from './ui';

interface Row { key: keyof Metrics; label: string; fmt: (v: number) => string; lower: boolean | null }
const ROWS: Row[] = [
  { key: 'avgWaiting', label: 'Avg waiting time (ms)', fmt: (v) => v.toFixed(1), lower: true },
  { key: 'avgTurnaround', label: 'Avg turnaround time (ms)', fmt: (v) => v.toFixed(1), lower: true },
  { key: 'avgResponse', label: 'Avg response time (ms)', fmt: (v) => v.toFixed(1), lower: true },
  { key: 'makespan', label: 'Makespan (ms)', fmt: (v) => v.toFixed(0), lower: true },
  { key: 'cpuUtil', label: 'CPU utilisation', fmt: (v) => pct(v, 1), lower: false },
  { key: 'throughput', label: 'Throughput (jobs / 100 ms)', fmt: (v) => v.toFixed(2), lower: false },
  { key: 'ctxSwitches', label: 'Context switches', fmt: (v) => v.toFixed(0), lower: true },
  { key: 'pageFaults', label: 'Page faults', fmt: (v) => v.toFixed(0), lower: true },
  { key: 'pfRate', label: 'Page-fault rate', fmt: (v) => pct(v, 1), lower: true },
  { key: 'replacements', label: 'Page replacements', fmt: (v) => v.toFixed(0), lower: true },
  { key: 'memUtil', label: 'Avg memory utilisation', fmt: (v) => pct(v, 1), lower: null },
  { key: 'avgIoWait', label: 'Avg I/O queue wait (ms)', fmt: (v) => v.toFixed(1), lower: true },
];
const COLORS = ['#38e1ff', '#ffb020', '#3ddc84', '#a78bfa', '#ff4d5e', '#f472b6', '#94a3b8'];
const SCHEDS = ['FCFS', 'SJF', 'SRTF', 'RR', 'PRIORITY', 'MLFQ', 'ADAPTIVE'];

function CompareTable({ results }: { results: ExpResult[] }) {
  return (
    <div className="scroll"><table className="compact cmp">
      <thead><tr><th>Metric</th>{results.map((r) => <th key={r.label}>{r.label}{!r.finished && <span className="lv-red" title="hit the tick limit"> *</span>}</th>)}</tr></thead>
      <tbody>{ROWS.map((row) => {
        const vals = results.map((r) => r.metrics[row.key] as number);
        const best = row.lower === null ? NaN : row.lower ? Math.min(...vals) : Math.max(...vals);
        return <tr key={row.key}><td>{row.label}</td>{vals.map((v, i) => <td key={i} className={v === best ? 'best' : ''}>{row.fmt(v)}</td>)}</tr>;
      })}</tbody>
    </table></div>
  );
}

export function ExperimentTab({ act }: { act: Act }) {
  const [w, setW] = useState({ cpu: 10, io: 10, ui: 5, seed: 42 });
  const [c, setC] = useState({ quantum: 4, memoryKB: 256, pageSizeKB: 4, replacement: 'LRU' });
  const [scheds, setScheds] = useState<string[]>(['FCFS', 'RR', 'MLFQ', 'ADAPTIVE']);
  const [results, setResults] = useState<{ schedulers: ExpResult[]; replacements: ExpResult[] } | null>(null);
  const [metric, setMetric] = useState<keyof Metrics>('avgWaiting');
  const [busy, setBusy] = useState(false);
  const [wi, setWi] = useState({ variable: 'processes', values: '10,20,30,40,50' });
  const [wiRes, setWiRes] = useState<{ value: number; result: ExpResult }[] | null>(null);
  const num = (obj: object, set: (o: never) => void, k: string) => (e: React.ChangeEvent<HTMLInputElement>) => set({ ...obj, [k]: Number(e.target.value) } as never);
  const payload = () => ({ workload: { cpu: w.cpu, io: w.io, ui: w.ui, seed: w.seed }, config: { quantum: c.quantum, memoryKB: c.memoryKB, pageSizeKB: c.pageSizeKB, replacement: c.replacement } });

  const run = () => act(async () => {
    setBusy(true);
    try { setResults(await post('/api/experiment', { ...payload(), schedulers: scheds, replacements: ['FIFO', 'LRU', 'CLOCK', 'OPTIMAL'] })); } finally { setBusy(false); }
  });
  const runWi = () => act(async () => {
    setBusy(true);
    try {
      const r = await post<{ points: { value: number; result: ExpResult }[] }>('/api/whatif', { ...payload(), variable: wi.variable, values: wi.values.split(',').map(Number).filter((n) => !Number.isNaN(n)) });
      setWiRes(r.points);
    } finally { setBusy(false); }
  });
  const toggle = (p: string) => setScheds((a) => (a.includes(p) ? a.filter((x) => x !== p) : [...a, p]));
  const row = ROWS.find((r) => r.key === metric)!;
  const defaults: Record<string, string> = { processes: '10,20,30,40,50', quantum: '1,2,4,8,16,32', memoryKB: '64,128,256,512,1024', pageSizeKB: '1,2,4,8,16', ioRate: '0.5,1,2,4' };

  return (
    <div className="grid two">
      <Panel title="Experiment Workload" right={<button className="primary" disabled={busy || scheds.length === 0} onClick={run}>{busy ? 'running…' : 'Run comparison'}</button>}>
        <div className="form">
          <label>CPU-bound<input type="number" min={0} max={60} value={w.cpu} onChange={num(w, setW as never, 'cpu')} /></label>
          <label>I/O-bound<input type="number" min={0} max={60} value={w.io} onChange={num(w, setW as never, 'io')} /></label>
          <label>Interactive<input type="number" min={0} max={60} value={w.ui} onChange={num(w, setW as never, 'ui')} /></label>
          <label>Random seed<input type="number" value={w.seed} onChange={num(w, setW as never, 'seed')} /></label>
          <label>Quantum (ms)<input type="number" min={1} value={c.quantum} onChange={num(c, setC as never, 'quantum')} /></label>
          <label>Physical memory (KB)<input type="number" min={4} value={c.memoryKB} onChange={num(c, setC as never, 'memoryKB')} /></label>
          <label>Page size (KB)<input type="number" min={1} value={c.pageSizeKB} onChange={num(c, setC as never, 'pageSizeKB')} /></label>
          <label>Replacement<select value={c.replacement} onChange={(e) => setC({ ...c, replacement: e.target.value })}>{['FIFO', 'LRU', 'CLOCK', 'OPTIMAL'].map((p) => <option key={p}>{p}</option>)}</select></label>
        </div>
        <div className="chips">{SCHEDS.map((p) => <button key={p} className={`catchip ${scheds.includes(p) ? 'on' : ''}`} onClick={() => toggle(p)}>{p}</button>)}</div>
        <div className="dim small">Each algorithm runs the identical, seeded workload on a fresh kernel (thrashing control off), so differences are due to the algorithm alone.</div>
      </Panel>

      <Panel title="What-If Analysis" right={<button className="primary" disabled={busy} onClick={runWi}>Run sweep</button>}>
        <div className="form inline">
          <label>Vary<select value={wi.variable} onChange={(e) => setWi({ variable: e.target.value, values: defaults[e.target.value] })}>
            <option value="processes">number of processes</option><option value="quantum">time quantum</option><option value="memoryKB">physical memory (KB)</option>
            <option value="pageSizeKB">page size (KB)</option><option value="ioRate">I/O intensity (×)</option></select></label>
          <label className="grow">Values<input value={wi.values} onChange={(e) => setWi({ ...wi, values: e.target.value })} /></label>
        </div>
        {!wiRes ? <Empty>“What happens if…” — e.g. 10 → 50 processes: watch CPU utilisation fall as the page-fault rate climbs.</Empty> : (
          <>
            {(() => {
              const s1 = [{ name: 'CPU utilisation', color: COLORS[0], values: wiRes.map((p) => p.result.metrics.cpuUtil) }, { name: 'Page-fault rate', color: COLORS[1], values: wiRes.map((p) => p.result.metrics.pfRate) }];
              const s2 = [{ name: 'Avg waiting (ms)', color: COLORS[2], values: wiRes.map((p) => p.result.metrics.avgWaiting) }];
              const xs = wiRes.map((p) => p.value);
              return <><LineChart series={s1} xs={xs} max={1} fmt={(v) => pct(v)} /><Legend series={s1} /><LineChart series={s2} xs={xs} /><Legend series={s2} /></>;
            })()}
            <div className="scroll"><table className="compact"><thead><tr><th>{wi.variable}</th><th>CPU</th><th>PF rate</th><th>Wait</th><th>Throughput</th><th>Makespan</th></tr></thead><tbody>
              {wiRes.map((p) => <tr key={p.value}><td>{p.value}</td><td>{pct(p.result.metrics.cpuUtil, 1)}</td><td>{pct(p.result.metrics.pfRate, 1)}</td><td>{p.result.metrics.avgWaiting.toFixed(1)}</td><td>{p.result.metrics.throughput.toFixed(2)}</td><td>{p.result.metrics.makespan}</td></tr>)}
            </tbody></table></div>
          </>)}
      </Panel>

      <Panel title="Scheduler Comparison" className="wide">
        {!results ? <Empty>Choose a workload and press “Run comparison”.</Empty> : (
          <>
            <CompareTable results={results.schedulers} />
            <div className="form inline"><label>Chart<select value={metric as string} onChange={(e) => setMetric(e.target.value as keyof Metrics)}>{ROWS.map((r) => <option key={r.key} value={r.key}>{r.label}</option>)}</select></label>
              <span className="dim small">green = best{row.lower === null ? ' (not ranked)' : row.lower ? ' (lower is better)' : ' (higher is better)'}</span></div>
            <BarChart rows={results.schedulers.map((r, i) => ({ label: r.label, value: r.metrics[metric] as number, color: COLORS[i % COLORS.length] }))} fmt={row.fmt} lowerIsBetter={row.lower ?? false} />
          </>)}
      </Panel>
      <Panel title="Page Replacement Comparison" className="wide">
        {!results ? <Empty>Runs together with the scheduler comparison (same workload, scheduler {`RR`}).</Empty> : <CompareTable results={results.replacements} />}
      </Panel>
    </div>
  );
}

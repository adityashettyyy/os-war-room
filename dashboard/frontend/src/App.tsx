import { useCallback, useEffect, useRef, useState } from 'react';
import { get, post } from './api';
import { CpuPanel, CreateProcess, EventLog, FailureLab, HealthPanel, InterruptPanel, IoPanel, MemoryMap, PcbInspector, ProcessTable, ReadyQueue, SchedulerLab } from './ControlRoom';
import { DetectiveTab } from './Detective';
import { ExperimentTab } from './ExperimentLab';
import { GuideTab } from './Guide';
import { MemoryLabTab } from './MemoryLab';
import { SyncIpcTab } from './SyncIpc';
import { Act, Envelope, LogEvent, ScenarioInfo } from './types';
import { LineChart, Legend, Panel } from './ui';

const TABS = ['Control Room', 'Memory Lab', 'Sync & IPC', 'Experiment Lab', 'OS Detective', 'Guide'] as const;
type Tab = (typeof TABS)[number];
interface Toast { id: number; text: string; tone: 'ok' | 'bad' }

export default function App() {
  const [env, setEnv] = useState<Envelope | null>(null);
  const [offline, setOffline] = useState(false);
  const [tab, setTab] = useState<Tab>('Control Room');
  const [selected, setSelected] = useState<number | null>(null);
  const [logs, setLogs] = useState<LogEvent[]>([]);
  const [scenarios, setScenarios] = useState<ScenarioInfo[]>([]);
  const [scenario, setScenario] = useState('');
  const [toasts, setToasts] = useState<Toast[]>([]);
  const lastLog = useRef(0);
  const lastTick = useRef(0);
  const toastId = useRef(0);

  const toast = useCallback((text: string, tone: 'ok' | 'bad') => {
    const id = ++toastId.current;
    setToasts((t) => [...t.slice(-3), { id, text, tone }]);
    setTimeout(() => setToasts((t) => t.filter((x) => x.id !== id)), tone === 'bad' ? 6000 : 2500);
  }, []);

  const refresh = useCallback(async () => {
    try {
      const e = await get<Envelope>('/api/state');
      setOffline(false);
      if (e.state.tick < lastTick.current || e.state.lastLogId < lastLog.current) { lastLog.current = 0; setLogs([]); }   // kernel was reset
      lastTick.current = e.state.tick;
      setEnv(e);
      if (e.state.lastLogId > lastLog.current) {
        const r = await get<{ last: number; events: LogEvent[] }>(`/api/log?since=${lastLog.current}&limit=600`);
        lastLog.current = r.last;
        setLogs((old) => [...old, ...r.events].slice(-3000));
      }
    } catch { setOffline(true); }
  }, []);

  useEffect(() => {
    let stop = false, timer: number;
    const loop = async () => { await refresh(); if (!stop) timer = window.setTimeout(loop, 300); };
    loop();
    return () => { stop = true; clearTimeout(timer); };
  }, [refresh]);
  useEffect(() => { get<{ scenarios: ScenarioInfo[] }>('/api/scenarios').then((r) => setScenarios(r.scenarios)).catch(() => undefined); }, []);

  const act: Act = useCallback(async (fn, ok) => {
    try { await fn(); if (ok) toast(ok, 'ok'); await refresh(); } catch (e) { toast((e as Error).message, 'bad'); }
  }, [refresh, toast]);

  if (!env) return <div className="boot">{offline ? <><h1>Kernel offline</h1><p>The dashboard cannot reach the C++ API. Start it with <code>./build/owr_server</code> and reload.</p></> : <p>Connecting to kernel…</p>}</div>;
  const s = env.state;
  const ctl = (action: string, extra: object = {}) => act(() => post('/api/control', { action, ...extra }));
  const picked = scenarios.find((x) => x.name === scenario);
  const series = s.series;
  const xs = series.map((p) => p.t);
  const chart = [
    { name: 'CPU util', color: '#38e1ff', values: series.map((p) => p.cpu) },
    { name: 'Memory', color: '#3ddc84', values: series.map((p) => p.mem) },
    { name: 'Page-fault rate', color: '#ffb020', values: series.map((p) => p.pf) },
  ];

  return (
    <div className="app">
      <header className="top">
        <div className="brand"><span className="logo">◢</span><div><h1>OS WAR ROOM</h1><small>kernel control room</small></div></div>
        <div className={`status status-${s.status.toLowerCase()}`}><i />{s.status}</div>
        <div className="clock" title="simulated milliseconds">T+{String(s.tick).padStart(5, '0')}</div>
        <div className="controls">
          {env.running ? <button className="primary" onClick={() => ctl('pause')}>⏸ Pause</button> : <button className="primary" onClick={() => ctl('run')}>▶ Run</button>}
          <button onClick={() => ctl('step', { steps: 1 })}>Step</button>
          <button onClick={() => ctl('step', { steps: 10 })}>+10</button>
          <button onClick={() => ctl('step', { steps: 100 })}>+100</button>
          <label className="speed">speed<input type="range" min={1} max={200} value={env.speed} onChange={(e) => ctl('speed', { speed: Number(e.target.value) })} /><b>{env.speed}/s</b></label>
          <button className="danger" onClick={() => { if (window.confirm('Reset the kernel? All processes and logs are cleared.')) { setSelected(null); ctl('reset'); } }}>Reset</button>
        </div>
        <div className="scen">
          <select value={scenario} onChange={(e) => setScenario(e.target.value)} aria-label="Scenario"><option value="">Load scenario…</option>{scenarios.map((x) => <option key={x.name} value={x.name}>{x.title}</option>)}</select>
          <button disabled={!scenario} onClick={() => act(async () => { await post('/api/scenario', { name: scenario }); setSelected(null); }, `Scenario loaded — press Run`)}>Load</button>
        </div>
      </header>
      {picked && <div className="scen-desc"><b>{picked.title}.</b> {picked.description} <span className="dim">Demonstrates: {picked.demonstrates.join(' · ')}.</span></div>}
      {offline && <div className="banner bad">Lost connection to the kernel API — retrying…</div>}
      <nav className="tabs" role="tablist">{TABS.map((t) => <button key={t} role="tab" aria-selected={tab === t} className={tab === t ? 'on' : ''} onClick={() => setTab(t)}>{t}</button>)}</nav>

      <main>
        {tab === 'Control Room' && (
          <div className="grid control">
            <CpuPanel s={s} onSelect={setSelected} />
            <ProcessTable s={s} selected={selected} onSelect={setSelected} />
            <HealthPanel s={s} />
            {selected !== null && <PcbInspector pid={selected} tick={s.tick} act={act} onClose={() => setSelected(null)} />}
            <MemoryMap s={s} act={act} />
            <IoPanel s={s} />
            <InterruptPanel s={s} />
            <ReadyQueue s={s} onSelect={setSelected} />
            <Panel title="Telemetry" right={<Legend series={chart} />}><LineChart series={chart} xs={xs} max={1} fmt={(v) => `${Math.round(v * 100)}%`} height={150} /></Panel>
            <SchedulerLab s={s} act={act} />
            <EventLog logs={logs} onClear={() => setLogs([])} />
            <CreateProcess act={act} />
            <FailureLab s={s} act={act} selected={selected} />
          </div>
        )}
        {tab === 'Memory Lab' && <MemoryLabTab s={s} act={act} />}
        {tab === 'Sync & IPC' && <SyncIpcTab s={s} act={act} />}
        {tab === 'Experiment Lab' && <ExperimentTab act={act} />}
        {tab === 'OS Detective' && <DetectiveTab />}
        {tab === 'Guide' && <GuideTab />}
      </main>
      <footer>OS War Room · C++17 kernel simulator + React/TypeScript dashboard · all faults are injected into a simulation, never the host.</footer>
      <div className="toasts" aria-live="polite">{toasts.map((t) => <div key={t.id} className={`toast ${t.tone}`}>{t.text}</div>)}</div>
    </div>
  );
}

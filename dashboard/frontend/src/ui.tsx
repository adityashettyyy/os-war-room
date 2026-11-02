// Shared presentational building blocks: panels, LEDs, bars, charts, colours.
import { ReactNode } from 'react';

export const pidColor = (pid: number) => (pid < 0 ? 'var(--dim)' : `hsl(${(pid * 47) % 360} 68% 58%)`);
export const pct = (x: number, d = 0) => `${(x * 100).toFixed(d)}%`;
export const pname = (pid: number) => (pid < 0 ? '—' : `P${pid}`);

export function Panel(props: { title: string; right?: ReactNode; children: ReactNode; className?: string; tone?: 'ok' | 'warn' | 'bad' }) {
  return (
    <section className={`panel ${props.tone ?? ''} ${props.className ?? ''}`}>
      <header><h2>{props.title}</h2><div className="hdr-right">{props.right}</div></header>
      <div className="body">{props.children}</div>
    </section>
  );
}

export function Led({ level }: { level: string }) {
  return <span className={`led ${level.toLowerCase()}`} aria-label={level} />;
}

export function Bar({ value, tone, label }: { value: number; tone?: string; label?: string }) {
  const v = Math.max(0, Math.min(1, value));
  const color = tone ?? (v > 0.85 ? 'var(--red)' : v > 0.6 ? 'var(--amber)' : 'var(--green)');
  return (
    <div className="bar" title={label}>
      <i style={{ width: `${v * 100}%`, background: color }} />
      {label && <span>{label}</span>}
    </div>
  );
}

export function StateBadge({ state }: { state: string }) {
  return <span className={`badge st-${state.toLowerCase()}`}>{state}</span>;
}

export function Empty({ children }: { children: ReactNode }) {
  return <div className="empty">{children}</div>;
}

export interface Series { name: string; color: string; values: number[] }

// Minimal dependency-free line chart (SVG). `xs` are x labels, y is auto-scaled unless max is given.
export function LineChart(props: { series: Series[]; xs?: (string | number)[]; height?: number; max?: number; unit?: string; fmt?: (v: number) => string }) {
  const W = 560, H = props.height ?? 150, L = 40, R = 8, T = 8, B = 22;
  const all = props.series.flatMap((s) => s.values);
  const max = props.max ?? Math.max(1e-9, ...all) * 1.1;
  const n = Math.max(1, ...props.series.map((s) => s.values.length));
  const x = (i: number) => L + (n <= 1 ? 0 : (i * (W - L - R)) / (n - 1));
  const y = (v: number) => T + (1 - Math.min(1, v / max)) * (H - T - B);
  const fmt = props.fmt ?? ((v: number) => (max <= 1.5 ? v.toFixed(2) : v.toFixed(0)));
  return (
    <svg viewBox={`0 0 ${W} ${H}`} className="chart" role="img">
      {[0, 0.5, 1].map((g) => (
        <g key={g}>
          <line x1={L} x2={W - R} y1={y(max * g)} y2={y(max * g)} className="grid" />
          <text x={L - 4} y={y(max * g) + 3} textAnchor="end" className="axis">{fmt(max * g)}</text>
        </g>
      ))}
      {props.xs && [0, Math.floor((n - 1) / 2), n - 1].filter((i, k, a) => a.indexOf(i) === k && props.xs![i] !== undefined).map((i) => (
        <text key={i} x={x(i)} y={H - 6} textAnchor="middle" className="axis">{props.xs![i]}</text>
      ))}
      {props.series.map((s) => (
        <polyline key={s.name} fill="none" stroke={s.color} strokeWidth="1.8" strokeLinejoin="round"
          points={s.values.map((v, i) => `${x(i).toFixed(1)},${y(v).toFixed(1)}`).join(' ')} />
      ))}
      {props.series.map((s) => s.values.length === 1 && <circle key={s.name} cx={x(0)} cy={y(s.values[0])} r="3" fill={s.color} />)}
    </svg>
  );
}

export function Legend({ series }: { series: Series[] }) {
  return <div className="legend">{series.map((s) => <span key={s.name}><i style={{ background: s.color }} />{s.name}</span>)}</div>;
}

// Grouped horizontal bars: one row per label, value scaled to the max in the set.
export function BarChart(props: { rows: { label: string; value: number; color?: string }[]; fmt?: (v: number) => string; lowerIsBetter?: boolean }) {
  const max = Math.max(1e-9, ...props.rows.map((r) => r.value));
  const best = props.rows.length ? (props.lowerIsBetter ? Math.min(...props.rows.map((r) => r.value)) : Math.max(...props.rows.map((r) => r.value))) : 0;
  return (
    <div className="bars">
      {props.rows.map((r) => (
        <div key={r.label} className={`brow ${r.value === best ? 'best' : ''}`}>
          <span className="blabel">{r.label}</span>
          <div className="btrack"><i style={{ width: `${(r.value / max) * 100}%`, background: r.color ?? 'var(--cyan)' }} /></div>
          <span className="bval">{(props.fmt ?? ((v: number) => v.toFixed(1)))(r.value)}</span>
        </div>
      ))}
    </div>
  );
}

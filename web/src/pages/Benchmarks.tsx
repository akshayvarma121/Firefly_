import { useState } from 'react';
import {
  BarChart, Bar, XAxis, YAxis, Tooltip, ResponsiveContainer,
} from 'recharts';
import { Skeleton } from '@/components/ui/skeleton';
import { EmptyState } from '../components/EmptyState';

// Shape of one row from GET /benchmark
type BenchmarkRow = {
  problem: string;
  status: string;
  solver_used?: string;
  objective: number | null;
  reference: number | null;
  difference: number | null;
  passed: boolean | null;
  wall_time_ms: number;
  cpu_time_ms?: number;   // present only if API exposes CPU baseline
  iterations: number;
  error_message?: string;
  // derived on the client
  variables?: number;
  constraints?: number;
};

type BenchmarkResponse = {
  benchmark_results: BenchmarkRow[];
};

// Known metadata for bundled instances (variables/constraints from MPS parsing is async;
// we hard-code from the reference values rather than re-parse during benchmarking)
const KNOWN_META: Record<string, { variables: number; constraints: number }> = {
  'test_problem1.mps': { variables: 4, constraints: 3 },
  'test_problem2.mps': { variables: 6, constraints: 4 },
};

function fmtMs(ms: number | undefined) {
  if (ms === undefined || ms === null) return '—';
  if (ms < 1000) return `${ms.toFixed(1)} ms`;
  return `${(ms / 1000).toFixed(3)} s`;
}

function fmtSpeedup(gpu?: number, cpu?: number) {
  if (!gpu || !cpu) return '—';
  const ratio = cpu / gpu;
  return `${ratio.toFixed(1)}×`;
}

export function Benchmarks() {
  const [rows, setRows] = useState<BenchmarkRow[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const runBenchmark = async () => {
    setLoading(true);
    setError(null);
    setRows([]);
    try {
      const res = await fetch('http://127.0.0.1:8000/benchmark');
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data: BenchmarkResponse = await res.json();
      const enriched = data.benchmark_results.map(r => ({
        ...r,
        ...(KNOWN_META[r.problem] ?? {}),
      }));
      setRows(enriched);
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="flex flex-col gap-8 max-w-5xl w-full">
      {/* Header */}
      <div className="flex items-start justify-between border-b border-border pb-4 mb-2">
        <div>
          <h2 className="text-xl font-mono text-text-primary uppercase tracking-widest mb-1">
            Benchmarks
          </h2>
          <p className="text-sm font-sans text-text-muted max-w-2xl">
            End-to-end wall-clock solve time across bundled MPS instances. Objective
            match is validated against pre-recorded HiGHS reference values.
          </p>
        </div>
        <button
          onClick={runBenchmark}
          disabled={loading}
          className={`shrink-0 px-6 py-2.5 font-mono text-sm uppercase tracking-wider border transition-colors duration-100
            ${loading
              ? 'border-border text-text-muted cursor-not-allowed opacity-50'
              : 'border-signal text-signal hover:bg-signal hover:text-background active:opacity-75 cursor-pointer'
            }`}
        >
          {loading ? 'Running…' : 'Run Benchmark'}
        </button>
      </div>

      {error && (
        <div className="border border-border bg-panel px-5 py-3 font-mono text-sm text-text-muted">
          Error: {error}. Make sure the API is running on port 8000.
        </div>
      )}

      {/* Loading state — skeleton chart + table */}
      {loading && (
        <div className="flex flex-col gap-8">
          {/* Skeleton chart */}
          <div className="flex flex-col gap-3">
            <Skeleton className="h-3 w-48" />
            <div className="border border-border bg-panel p-4 h-56 flex items-end gap-3 pb-6 px-6">
              {[65, 40, 80, 30, 55].map((h, i) => (
                <Skeleton key={i} className="flex-1" style={{ height: `${h}%` }} />
              ))}
            </div>
          </div>
          {/* Skeleton table */}
          <div className="flex flex-col gap-3">
            <Skeleton className="h-3 w-24" />
            <div className="border border-border overflow-hidden">
              <div className="border-b border-border bg-background px-4 py-2.5 flex gap-8">
                {[120, 60, 80, 70, 70, 60, 100].map((w, i) => (
                  <Skeleton key={i} className="h-2" style={{ width: w }} />
                ))}
              </div>
              {[...Array(3)].map((_, r) => (
                <div key={r} className="border-b border-border bg-panel px-4 py-3.5 flex gap-8">
                  {[120, 60, 80, 70, 70, 60, 100].map((w, i) => (
                    <Skeleton key={i} className="h-2.5" style={{ width: w, opacity: 0.6 }} />
                  ))}
                </div>
              ))}
            </div>
          </div>
        </div>
      )}

      {/* Results List */}
      {!loading && rows.length > 0 && (
        <div className="flex flex-col gap-8">
          {rows.map((row) => {
            if (row.status === 'ERROR') {
              return (
                <div key={row.problem} className="border border-border bg-panel p-5 flex flex-col gap-2">
                  <h3 className="font-mono text-base text-signal">{row.problem}</h3>
                  <div className="text-sm font-mono text-text-muted">Error: {row.error_message}</div>
                </div>
              );
            }

            const gpuTimeS = (row.wall_time_ms / 1000).toFixed(3);
            const hasCpu = row.cpu_time_ms != null;
            
            let headline = '';
            if (hasCpu) {
              const cpuTimeS = (row.cpu_time_ms! / 1000).toFixed(3);
              const speedup = (row.cpu_time_ms! / row.wall_time_ms).toFixed(1);
              headline = `On this problem, our GPU engine finished in ${gpuTimeS}s versus ${cpuTimeS}s on the classical method — about ${speedup}x faster.`;
            } else {
              headline = `On this problem, our GPU engine finished in ${gpuTimeS}s.`;
            }

            const instanceChartData = [{
              name: row.problem.replace('.mps', ''),
              'GPU (PDLP)': row.wall_time_ms,
              ...(hasCpu ? { 'CPU (Simplex)': row.cpu_time_ms } : {}),
            }];

            const matchLabel = row.passed === null ? '—' : row.passed ? 'PASS' : 'FAIL';
            const matchColor = row.passed === null
              ? 'text-text-muted'
              : row.passed
              ? 'text-trace-secondary'
              : 'text-signal';

            return (
              <div key={row.problem} className="border border-border bg-background flex flex-col">
                <div className="p-5 border-b border-border bg-panel flex flex-col gap-4">
                  <div className="flex flex-col gap-1">
                    <span className="font-mono text-xs text-text-muted uppercase tracking-widest">{row.problem}</span>
                    <h3 className="font-sans text-base text-text-primary leading-relaxed max-w-3xl">
                      {headline}
                    </h3>
                  </div>

                  <div className="h-24 w-full max-w-sm mt-2">
                    <ResponsiveContainer width="100%" height="100%">
                      <BarChart data={instanceChartData} layout="vertical" barGap={4} margin={{ top: 0, right: 20, left: 0, bottom: 0 }}>
                        <XAxis type="number" hide />
                        <YAxis type="category" dataKey="name" hide />
                        <Tooltip
                          cursor={{ fill: '#1B1D18' }}
                          contentStyle={{ backgroundColor: '#1B1D18', borderColor: '#33362E', borderRadius: '2px' }}
                          itemStyle={{ fontFamily: 'IBM Plex Mono', fontSize: 12 }}
                          formatter={(v: number) => [`${v.toFixed(1)} ms`]}
                        />
                        <Bar dataKey="GPU (PDLP)" fill="#E8A33D" maxBarSize={16} radius={[0, 2, 2, 0]} />
                        {hasCpu && (
                          <Bar dataKey="CPU (Simplex)" fill="#6B8F71" maxBarSize={16} radius={[0, 2, 2, 0]} />
                        )}
                      </BarChart>
                    </ResponsiveContainer>
                  </div>
                </div>

                <details className="group [&_summary::-webkit-details-marker]:hidden">
                  <summary className="flex items-center gap-2 px-5 py-3 cursor-pointer select-none border-b border-transparent group-open:border-border hover:bg-panel transition-colors text-xs font-mono text-text-muted uppercase tracking-widest">
                    <span className="transition-transform duration-200 group-open:rotate-90">▶</span>
                    See full data
                  </summary>
                  <div className="p-5 bg-panel">
                    <div className="grid grid-cols-2 md:grid-cols-4 lg:grid-cols-7 gap-6">
                      <div className="flex flex-col gap-1">
                        <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider">Variables</span>
                        <span className="font-mono text-sm text-text-primary">{row.variables ?? '—'}</span>
                      </div>
                      <div className="flex flex-col gap-1">
                        <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider">Constraints</span>
                        <span className="font-mono text-sm text-text-primary">{row.constraints ?? '—'}</span>
                      </div>
                      <div className="flex flex-col gap-1">
                        <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider">GPU Time</span>
                        <span className="font-mono text-sm text-text-primary">{fmtMs(row.wall_time_ms)}</span>
                      </div>
                      <div className="flex flex-col gap-1">
                        <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider">CPU Time</span>
                        <span className="font-mono text-sm text-text-primary">{fmtMs(row.cpu_time_ms)}</span>
                      </div>
                      <div className="flex flex-col gap-1">
                        <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider">Speedup</span>
                        <span className="font-mono text-sm text-text-primary">{fmtSpeedup(row.wall_time_ms, row.cpu_time_ms)}</span>
                      </div>
                      <div className="flex flex-col gap-1">
                        <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider">HiGHS Reference</span>
                        <span className="font-mono text-sm text-text-primary">{row.reference !== null ? row.reference.toExponential(6) : '—'}</span>
                      </div>
                      <div className="flex flex-col gap-1">
                        <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider">Engine</span>
                        <span className="font-mono text-sm text-text-primary">{row.solver_used || 'pdlp'}</span>
                      </div>
                      <div className="flex flex-col gap-1">
                        <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider">Objective Gap</span>
                        <span className={`font-mono text-sm ${matchColor}`}>
                          {matchLabel} {row.difference != null && `(Δ${row.difference.toExponential(2)})`}
                        </span>
                      </div>
                    </div>
                  </div>
                </details>
              </div>
            );
          })}

          {/* Caption */}
          <p className="text-xs font-sans text-text-muted leading-relaxed max-w-3xl border-l-2 border-border pl-4 mt-4">
            <strong className="text-text-primary font-sans">Comparison baseline:</strong> Reference
            objective values are from HiGHS (open-source LP/MILP solver), not a commercial solver
            such as Gurobi or CPLEX. HiGHS was chosen as the accuracy baseline because it is freely
            verifiable, non-proprietary, and produces certified optimal solutions for the LP/MILP
            classes Firefly targets. CPU Simplex timing will be populated when the API exposes a
            parallel CPU solve path; until then that column is intentionally blank rather than
            estimated.
          </p>
        </div>
      )}

      {!loading && rows.length === 0 && !error && (
        <EmptyState
          headline="No benchmark data yet."
          body="Click Run Benchmark to execute all bundled instances against the solver. Wall-clock times and objective accuracy will be reported here."
          action={
            <button
              onClick={runBenchmark}
              className="px-5 py-2 border border-signal text-signal hover:bg-signal hover:text-background active:opacity-75 transition-colors duration-100 font-mono text-xs uppercase tracking-wider"
            >
              Run Benchmark
            </button>
          }
        />
      )}
    </div>
  );
}


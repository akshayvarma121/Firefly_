import { useState, useRef } from 'react';
import type { ChangeEvent, DragEvent } from 'react';
import { Upload, Download } from 'lucide-react';
import { motion, AnimatePresence } from 'framer-motion';
import { useSolverStore } from '../store/solverStore';
import { AnnunciatorPanel } from '../components/SolverStatus';
import { LineChart, Line, XAxis, YAxis, Tooltip, ResponsiveContainer, CartesianGrid, Legend } from 'recharts';
import { Skeleton } from '@/components/ui/skeleton';
import { useToast } from '@/hooks/use-toast';
import { EmptyState } from '../components/EmptyState';

type ParsedStats = {
  variables: number;
  constraints: number;
  type: 'LP' | 'MILP' | 'QP';
};

type FilePreview = {
  name: string;
  size: number;
  stats: ParsedStats | null;
  file?: File;
};

const BUNDLED_SAMPLES = [
  { id: 'test_problem1.mps', label: 'Test Problem 1', vars: 100, cons: 50, type: 'LP' },
  { id: 'test_problem2.mps', label: 'Test Problem 2', vars: 500, cons: 200, type: 'MILP' },
];

type SolverResult = {
  status: string;
  objective: number | null;
  solution: number[] | null;
  wall_time_ms: number;
  iterations: number;
  mock?: boolean;
};

export function Solve() {
  const [selectedSample, setSelectedSample] = useState<string>('');
  const [uploadedFile, setUploadedFile] = useState<FilePreview | null>(null);
  const [isDragging, setIsDragging] = useState(false);
  const [isSolving, setIsSolving] = useState(false);
  const [wsConnecting, setWsConnecting] = useState(false); // true between click and first WS message
  const [solveResult, setSolveResult] = useState<SolverResult | null>(null);
  const [problemLabel, setProblemLabel] = useState<string>('');
  const [isMock, setIsMock] = useState(false);
  
  const fileInputRef = useRef<HTMLInputElement>(null);
  const { toast } = useToast();
  
  const solverState = useSolverStore(s => s.state);
  const setSolverState = useSolverStore(s => s.setState);
  const addUpdate = useSolverStore(s => s.addUpdate);
  const resetStore = useSolverStore(s => s.reset);
  const chartData = useSolverStore(s => s.chartData);
  const elapsedMs = useSolverStore(s => s.elapsedMs);
  const ips = useSolverStore(s => s.iterationsPerSec);

  // Parse MPS file heuristics
  const parseMpsFile = async (file: File) => {
    try {
      const text = await file.text();
      const lines = text.split('\n');
      
      let inRows = false;
      let inCols = false;
      let rowCount = 0;
      const colSet = new Set<string>();
      let isMilp = false;

      const limit = Math.min(lines.length, 10000);
      for (let i = 0; i < limit; i++) {
        const line = lines[i];
        if (line.startsWith('ROWS')) { inRows = true; inCols = false; continue; }
        if (line.startsWith('COLUMNS')) { inRows = false; inCols = true; continue; }
        if (line.startsWith('RHS') || line.startsWith('BOUNDS')) { inCols = false; }

        if (inRows && line.startsWith(' ')) {
          rowCount++;
        }
        if (inCols && line.startsWith(' ')) {
          const parts = line.trim().split(/\s+/);
          if (parts.length >= 1) colSet.add(parts[0]);
          if (line.includes("'MARKER'") && line.includes("'INTORG'")) isMilp = true;
        }
      }

      const constraints = Math.max(0, rowCount - 1);
      const variables = colSet.size;

      setUploadedFile({
        name: file.name,
        size: file.size,
        file: file, // Keep the actual file object for solving
        stats: {
          variables: variables,
          constraints: constraints,
          type: isMilp ? 'MILP' : 'LP'
        }
      });
    } catch (e) {
      setUploadedFile({
        name: file.name,
        size: file.size,
        file: file,
        stats: null
      });
      toast({
        variant: 'warning',
        title: 'Parse notice',
        description: `Could not extract statistics from ${file.name}. The file will still be sent to the solver.`,
      });
    }
  };

  const handleFileChange = (e: ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (file) {
      setSelectedSample('');
      parseMpsFile(file);
    }
  };

  const handleDrop = (e: DragEvent<HTMLDivElement>) => {
    e.preventDefault();
    setIsDragging(false);
    const file = e.dataTransfer.files?.[0];
    if (file && (file.name.endsWith('.mps') || file.name.endsWith('.mps.gz'))) {
      setSelectedSample('');
      parseMpsFile(file);
    }
  };

  const handleDragOver = (e: DragEvent<HTMLDivElement>) => {
    e.preventDefault();
    setIsDragging(true);
  };

  const handleDragLeave = () => {
    setIsDragging(false);
  };

  const downloadJSON = () => {
    if (!solveResult) return;
    const payload = {
      problem: problemLabel,
      status: solveResult.status,
      objective: solveResult.objective,
      wall_time_ms: solveResult.wall_time_ms,
      iterations: solveResult.iterations,
      solution: solveResult.solution,
    };
    const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `firefly_result_${Date.now()}.json`;
    a.click();
    URL.revokeObjectURL(url);
  };

  const downloadCSV = () => {
    if (!solveResult) return;
    const rows: string[][] = [
      ['field', 'value'],
      ['problem', problemLabel],
      ['status', solveResult.status],
      ['objective', String(solveResult.objective ?? '')],
      ['wall_time_ms', String(solveResult.wall_time_ms)],
      ['iterations', String(solveResult.iterations)],
    ];
    if (solveResult.solution) {
      solveResult.solution.forEach((v, i) => rows.push([`x_${i}`, String(v)]));
    }
    const csv = rows.map(r => r.join(',')).join('\n');
    const blob = new Blob([csv], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `firefly_result_${Date.now()}.csv`;
    a.click();
    URL.revokeObjectURL(url);
  };

  const executeSolve = async () => {
    setIsSolving(true);
    setWsConnecting(true);
    setSolveResult(null);
    setIsMock(false);
    resetStore();
    setSolverState('running');
    setProblemLabel(uploadedFile ? uploadedFile.name : selectedSample);

    let mpsContent = "";

    if (uploadedFile && uploadedFile.file) {
      mpsContent = await uploadedFile.file.text();
    } else if (selectedSample) {
      try {
        const res = await fetch(`/samples/${selectedSample}`);
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        mpsContent = await res.text();
      } catch (err) {
        console.error("Failed to fetch sample:", err);
        toast({
          variant: 'warning',
          title: 'Backend unreachable',
          description: 'Could not load the sample problem. Make sure the API server is running on port 8000.',
        });
        setSolverState('idle');
        setWsConnecting(false);
        setIsSolving(false);
        return;
      }
    }

    const ws = new WebSocket('ws://127.0.0.1:8000/ws/solve-stream');

    let hasConnected = false;
    let isFinished = false;

    ws.onopen = () => {
      hasConnected = true;
      ws.send(JSON.stringify({
        mps_content: mpsContent,
        method: "auto",
        gpu: true
      }));
    };

    ws.onmessage = (event) => {
      setWsConnecting(false); // first message — handshake complete
      const data = JSON.parse(event.data);
      if (data.type === 'update') {
        if (data.mock) setIsMock(true);
        addUpdate({
          iteration: data.iteration,
          primal_obj: data.primal_obj,
          dual_obj: data.dual_obj,
          elapsed_ms: data.elapsed_ms
        });
      } else if (data.type === 'result') {
        isFinished = true;
        setSolverState('converged');
        if (data.mock) setIsMock(true);
        setSolveResult({
          status: data.status,
          objective: data.objective ?? null,
          solution: data.solution ?? null,
          wall_time_ms: data.wall_time_ms ?? 0,
          iterations: data.iterations ?? 0,
          mock: data.mock ?? false,
        });
        // Notify on non-optimal outcomes
        if (data.status === 'INFEASIBLE') {
          toast({
            variant: 'warning',
            title: 'Infeasible',
            description: 'The solver determined that no feasible solution exists for this problem. Check your constraint bounds.',
          });
        } else if (data.status === 'UNBOUNDED') {
          toast({
            variant: 'warning',
            title: 'Unbounded',
            description: 'The objective can decrease without limit. A missing bound or incorrect sign on a constraint may be the cause.',
          });
        }
        ws.close(1000, "Solve completed");
      } else if (data.type === 'error') {
        isFinished = true;
        setSolverState('idle');
        console.error("Solver error:", data.message);
        toast({
          variant: 'warning',
          title: 'Solver error',
          description: data.message ?? 'An error occurred during the solve. See console for details.',
        });
        ws.close(1000, "Error handled");
      }
    };

    ws.onerror = (err) => {
      console.error("WebSocket error:", err);
      // The actual UI notification is handled by onclose to avoid duplicate toasts,
      // as an error is always followed by a close event.
    };

    ws.onclose = () => {
      setWsConnecting(false);
      
      // If we finished normally, nothing to do
      if (isFinished) return;
      
      setSolverState('idle');
      setIsSolving(false);

      if (!hasConnected) {
        toast({
          variant: 'warning',
          title: 'Backend unreachable',
          description: 'Could not connect to the solver. Make sure the API server is running on port 8000.',
        });
      } else {
        toast({
          variant: 'warning',
          title: 'Connection lost',
          description: 'The WebSocket disconnected before the solve completed. The solver may have crashed on the backend.',
        });
      }
    };
  };

  const canSolve = selectedSample !== '' || uploadedFile !== null;

  if (isSolving) {
    let annunciatorCaption = "Solver idle";
    if (solverState === 'running') {
      const currentIter = chartData.length > 0 ? chartData[chartData.length - 1].iteration : 0;
      annunciatorCaption = `Solving now — iteration ${currentIter}`;
    } else if (solverState === 'converged') {
      const timeS = solveResult ? (solveResult.wall_time_ms / 1000).toFixed(3) : (elapsedMs / 1000).toFixed(3);
      annunciatorCaption = `Solved in ${timeS}s`;
    }

    return (
      // This div fills its parent (AppShell's flex column in fill mode)
      <div className="flex flex-col gap-0 w-full h-full min-h-0">

        {/* Header strip */}
        <div className="flex items-center justify-between border-b border-border pb-3 mb-3 shrink-0 flex-wrap gap-y-2">
          <div>
            <h2 className="text-base font-mono text-text-primary uppercase tracking-widest flex items-center gap-3">
              Live Telemetry
              {isMock && (
                <span className="px-2 py-0.5 bg-yellow-500/20 text-yellow-500 text-[10px] border border-yellow-500/50">
                  MOCK DATA
                </span>
              )}
            </h2>
            <div className="text-[10px] font-mono text-text-muted mt-0.5 flex flex-col gap-1">
              <span className="truncate max-w-[240px] text-text-primary">{uploadedFile ? uploadedFile.name : selectedSample}</span>
              <span className="opacity-75">Tracking Primal & Dual objective convergence. Optimal solution is found when the duality gap closes (traces intersect).</span>
            </div>
          </div>
          <div className="flex items-center gap-4 flex-wrap">
            <div className="flex flex-col items-end">
              <span className="text-[10px] font-mono text-text-muted uppercase tracking-widest">Elapsed</span>
              <span className="font-mono text-base text-text-primary">{(elapsedMs / 1000).toFixed(2)}s</span>
            </div>
            <div className="flex flex-col items-end">
              <span className="text-[10px] font-mono text-text-muted uppercase tracking-widest">Speed</span>
              <span className="font-mono text-base text-text-primary">{ips.toFixed(0)} it/s</span>
            </div>
            <div className="flex flex-col items-end">
              <span className="text-[10px] font-mono text-text-muted uppercase tracking-widest mb-1">GPU Lanes</span>
              {/* Fewer lanes on narrow viewports to prevent overflow */}
              <div className="hidden lg:block"><AnnunciatorPanel state={solverState} count={32} /></div>
              <div className="hidden md:block lg:hidden"><AnnunciatorPanel state={solverState} count={16} /></div>
              <div className="block md:hidden"><AnnunciatorPanel state={solverState} count={8} /></div>
              <span className="text-[10px] font-mono text-text-muted mt-1 tracking-wider opacity-80 text-right w-full">{annunciatorCaption}</span>
            </div>
          </div>
        </div>

        {wsConnecting ? (
          /* Handshake skeleton */
          <div className="flex-1 min-h-0 border border-border bg-panel p-5 flex flex-col gap-4">
            <div className="flex gap-3 mb-2">
              <Skeleton className="h-3 w-24" />
              <Skeleton className="h-3 w-16" />
            </div>
            <div className="flex flex-col gap-2 flex-1">
              {[...Array(6)].map((_, i) => (
                <Skeleton key={i} className="h-px w-full opacity-40" />
              ))}
            </div>
            <div className="flex items-center gap-2">
              <Skeleton className="h-3 w-32" />
              <span className="font-mono text-xs text-text-muted">Connecting to solver…</span>
            </div>
          </div>
        ) : (
          /* Chart — flex-1 min-h-0 means it fills exactly the remaining space */
          <div className="flex-1 min-h-0 bg-panel border border-border p-3">
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={chartData} margin={{ top: 20, right: 20, left: 20, bottom: 20 }}>
                <CartesianGrid strokeDasharray="3 3" stroke="#33362E" vertical={true} horizontal={true} />
                <XAxis 
                  dataKey="iteration" 
                  stroke="#33362E" 
                  tick={{ fontFamily: 'IBM Plex Mono', fill: '#8C8B80', fontSize: 10 }}
                  tickFormatter={(val) => val.toLocaleString()}
                  tickMargin={10}
                />
                <YAxis 
                  domain={['auto', 'auto']} 
                  stroke="#33362E"
                  tick={{ fontFamily: 'IBM Plex Mono', fill: '#8C8B80', fontSize: 10 }}
                  tickFormatter={(val) => val.toExponential(2)}
                  tickMargin={10}
                  width={60}
                />
                <Tooltip
                  contentStyle={{ backgroundColor: '#1B1D18', borderColor: '#33362E', borderRadius: '2px' }}
                  itemStyle={{ fontFamily: 'IBM Plex Mono', fontSize: '12px' }}
                  labelStyle={{ fontFamily: 'IBM Plex Mono', fontSize: '12px', color: '#8C8B80' }}
                  formatter={(value: number, name: string) => {
                    let displayName = name;
                    if (name === 'Current best answer') displayName = 'Current best answer (primal)';
                    if (name === 'Confidence bound') displayName = 'Confidence bound (dual)';
                    return [value.toExponential(4), displayName];
                  }}
                  labelFormatter={(label) => `Iteration ${label}`}
                />
                <Legend 
                  wrapperStyle={{ fontFamily: 'IBM Plex Mono', fontSize: '10px', paddingTop: '10px', color: '#8C8B80' }}
                  iconType="plainline"
                  formatter={(value) => {
                    if (value === 'Current best answer') {
                      return <span style={{ color: '#E8E6DE' }}>Current best answer <span style={{ color: '#8C8B80', fontSize: '9px', opacity: 0.8 }}>(primal)</span></span>;
                    }
                    if (value === 'Confidence bound') {
                      return <span style={{ color: '#E8E6DE' }}>Confidence bound <span style={{ color: '#8C8B80', fontSize: '9px', opacity: 0.8 }}>(dual)</span></span>;
                    }
                    return value;
                  }}
                />
                <Line type="linear" dataKey="primal_obj" name="Current best answer" stroke="#E8A33D" dot={false} strokeWidth={1.5} isAnimationActive={true} animationDuration={300} />
                <Line type="linear" dataKey="dual_obj" name="Confidence bound" stroke="#6B8F71" dot={false} strokeWidth={1.5} isAnimationActive={true} animationDuration={300} />
              </LineChart>
            </ResponsiveContainer>
          </div>
        )}

        <AnimatePresence>
          {solverState === 'converged' && solveResult && (
            <motion.div
              initial={{ opacity: 0, y: 8 }}
              animate={{ opacity: 1, y: 0 }}
              exit={{ opacity: 0 }}
              transition={{ duration: 0.18, ease: 'easeOut' }}
              className="border border-border bg-panel shrink-0 mt-3"
            >
              {/* Summary header */}
              <div className="flex items-center justify-between px-5 py-3 border-b border-border">
                <div className="flex items-center gap-3">
                  <span className="text-xs font-mono text-text-muted uppercase tracking-widest">Result</span>
                  <span className={`text-xs font-mono uppercase tracking-widest px-2 py-0.5 border ${
                    solveResult.status === 'OPTIMAL'
                      ? 'border-signal text-signal'
                      : 'border-text-muted text-text-muted'
                  }`}>
                    {solveResult.status}
                  </span>
                </div>
                <div className="flex items-center gap-2">
                  <button
                    onClick={downloadCSV}
                    className="flex items-center gap-2 px-3 py-1.5 border border-border text-text-muted hover:text-text-primary hover:border-text-muted active:opacity-75 transition-colors duration-100 font-mono text-xs uppercase tracking-wider"
                  >
                    <Download className="w-3 h-3" />
                    CSV
                  </button>
                  <button
                    onClick={downloadJSON}
                    className="flex items-center gap-2 px-3 py-1.5 border border-border text-text-muted hover:text-text-primary hover:border-text-muted active:opacity-75 transition-colors duration-100 font-mono text-xs uppercase tracking-wider"
                  >
                    <Download className="w-3 h-3" />
                    JSON
                  </button>
                </div>
              </div>

              {/* Data readouts — compact py on small screens */}
              <div className="grid grid-cols-2 md:grid-cols-4 divide-x divide-border">
                <div className="flex flex-col px-4 py-2">
                  <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider mb-0.5">Objective</span>
                  <span className="font-mono text-sm text-signal">
                    {solveResult.objective !== null ? solveResult.objective.toExponential(6) : '—'}
                  </span>
                </div>
                <div className="flex flex-col px-4 py-2">
                  <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider mb-0.5">Total Time</span>
                  <span className="font-mono text-sm text-text-primary">
                    {(solveResult.wall_time_ms / 1000).toFixed(3)}s
                  </span>
                </div>
                <div className="flex flex-col px-4 py-2">
                  <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider mb-0.5">Iterations</span>
                  <span className="font-mono text-sm text-text-primary">
                    {solveResult.iterations.toLocaleString()}
                  </span>
                </div>
                <div className="flex flex-col px-4 py-2">
                  <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider mb-0.5">Variables</span>
                  <span className="font-mono text-sm text-text-primary">
                    {solveResult.solution ? solveResult.solution.length.toLocaleString() : '—'}
                  </span>
                </div>
              </div>
            </motion.div>
          )}
        </AnimatePresence>

        {solverState === 'converged' && (
          <div className="flex justify-end pt-2 shrink-0">
            <button
              onClick={() => setIsSolving(false)}
              className="px-6 py-2 bg-panel border border-border text-text-primary hover:text-signal hover:border-signal transition-colors font-mono text-sm uppercase tracking-wider"
            >
              New Solve
            </button>
          </div>
        )}
      </div>
    );
  }

  return (
    <div className="flex flex-col gap-8 max-w-5xl w-full">
      <div>
        <h2 className="text-xl font-mono text-text-primary uppercase tracking-widest mb-1">Solve Instance</h2>
        <p className="text-sm font-sans text-text-muted">
          Select a bundled benchmark or upload a custom MPS file for optimization.
        </p>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-8">
        <div className="flex flex-col gap-4">
          <label className="text-xs font-mono text-text-muted uppercase tracking-wider">
            Bundled Samples
          </label>
          <div className="relative">
            <select
              className="w-full bg-panel border border-border text-text-primary text-sm p-3 hover:border-text-muted focus:outline-none focus:border-signal transition-colors duration-100 font-mono appearance-none cursor-pointer"
              value={selectedSample}
              onChange={(e) => {
                setSelectedSample(e.target.value);
                setUploadedFile(null);
              }}
            >
              <option value="" disabled>-- Select a sample problem --</option>
              {BUNDLED_SAMPLES.map(sample => (
                <option key={sample.id} value={sample.id}>
                  {sample.label} [{sample.vars} vars, {sample.cons} cons]
                </option>
              ))}
            </select>
            <div className="pointer-events-none absolute inset-y-0 right-0 flex items-center px-3 text-text-muted">
              ▼
            </div>
          </div>
          <div className="text-xs text-text-muted font-mono mt-1">
            Standard MPS instances for quick validation.
          </div>
        </div>

        <div className="flex flex-col gap-4">
          <label className="text-xs font-mono text-text-muted uppercase tracking-wider">
            Custom MPS Upload
          </label>
          <div
            role="button"
            tabIndex={0}
            className={`border border-dashed transition-colors duration-100 p-8 flex flex-col items-center justify-center gap-4 cursor-pointer
              ${isDragging ? 'border-signal' : 'border-border bg-panel hover:border-text-muted'}`}
            onDragOver={handleDragOver}
            onDragLeave={handleDragLeave}
            onDrop={handleDrop}
            onClick={() => fileInputRef.current?.click()}
            onKeyDown={(e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); fileInputRef.current?.click(); } }}
          >
            <Upload className={`w-6 h-6 ${isDragging ? 'text-signal' : 'text-text-muted'}`} />
            <div className="text-center">
              <span className="text-sm font-sans text-text-primary block mb-1">
                Drag & drop .mps file here
              </span>
              <span className="text-xs font-sans text-text-muted">
                or click to browse
              </span>
            </div>
            <input 
              type="file" 
              accept=".mps" 
              className="hidden" 
              ref={fileInputRef}
              onChange={handleFileChange}
            />
          </div>
        </div>
      </div>

      {uploadedFile && (
        <div className="border border-border bg-panel p-4 flex flex-col gap-3 animate-in fade-in slide-in-from-bottom-2">
          <div className="flex items-baseline justify-between border-b border-border pb-2">
            <span className="font-mono text-sm text-text-primary">{uploadedFile.name}</span>
            <span className="font-mono text-xs text-text-muted">
              {(uploadedFile.size / 1024).toFixed(1)} KB
            </span>
          </div>
          {uploadedFile.stats ? (
            <div className="grid grid-cols-3 gap-4 pt-1">
              <div className="flex flex-col">
                <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider">Variables</span>
                <span className="font-mono text-sm text-text-primary">{uploadedFile.stats.variables}</span>
              </div>
              <div className="flex flex-col">
                <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider">Constraints</span>
                <span className="font-mono text-sm text-text-primary">{uploadedFile.stats.constraints}</span>
              </div>
              <div className="flex flex-col">
                <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider">Type</span>
                <span className="font-mono text-sm text-signal">{uploadedFile.stats.type}</span>
              </div>
            </div>
          ) : (
            <div className="pt-1 text-xs font-mono text-text-muted">
              Parsing stats unavailable.
            </div>
          )}
        </div>
      )}

      {/* Empty state — shown while no problem is selected */}
      {!canSolve && (
        <EmptyState
          headline="No problem loaded."
          body="Pick a bundled sample from the dropdown or drop an .mps file into the upload zone above to get started."
        />
      )}

      <div className="border-t border-border pt-6 flex justify-end">
        <button
          onClick={executeSolve}
          disabled={!canSolve}
          className={`px-8 py-3 font-mono text-sm uppercase tracking-wider transition-colors duration-100 border
            ${canSolve
              ? 'bg-panel border-signal text-signal hover:bg-signal hover:text-background active:opacity-75 cursor-pointer'
              : 'bg-background border-border text-text-muted cursor-not-allowed opacity-50'
            }
          `}
        >
          Execute Solve
        </button>
      </div>
    </div>
  );
}

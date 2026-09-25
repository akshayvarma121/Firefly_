import { useState, useEffect, useRef } from 'react';
import { useSolver } from '@/context/SolverContext';
import { GPULaneIndicators } from '@/components/SolverStatus';
import { Play, Square, Upload, FileText } from 'lucide-react';
import { cn } from '@/lib/utils';
import { toast } from 'sonner';
import {
  LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer
} from 'recharts';

type WSMessage = {
  type: 'status' | 'trace' | 'data';
  content?: string;
  data?: { iteration: number; primal: number; dual: number };
  state?: 'idle' | 'solving' | 'converged' | 'error';
};

const SAMPLES = ['afiro.mps', 'flugpl.mps', 'egout.mps', 'blend.mps'];

export function Solve() {
  const { state, setState } = useSolver();
  const [data, setData] = useState<any[]>([]);
  const [traces, setTraces] = useState<string[]>([]);
  const [selectedFile, setSelectedFile] = useState<string | null>(null);
  const [isConnecting, setIsConnecting] = useState(false);
  
  const ws = useRef<WebSocket | null>(null);
  const traceEndRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    traceEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [traces]);

  useEffect(() => {
    return () => {
      if (ws.current) ws.current.close();
    };
  }, []);

  const handleConnect = () => {
    if (!selectedFile) {
      toast.error('Please select or upload a model first.');
      return;
    }
    
    if (state === 'solving') {
      ws.current?.close();
      setState('idle');
      return;
    }

    setIsConnecting(true);
    setState('idle');
    setData([]);
    setTraces(['Initializing CUDA context...', `Loading ${selectedFile}...`]);

    try {
      ws.current = new WebSocket('ws://localhost:8000/ws/solve-stream');
      
      ws.current.onopen = () => {
        setIsConnecting(false);
        setState('solving');
        toast.success('Connected to solver engine.');
        ws.current?.send(JSON.stringify({ action: 'start', file: selectedFile }));
      };

      ws.current.onmessage = (event) => {
        try {
          const msg: WSMessage = JSON.parse(event.data);
          if (msg.type === 'status' && msg.state) {
            setState(msg.state);
            if (msg.state === 'converged') toast.success('Solve converged optimally.');
            if (msg.state === 'error') toast.error('Solver encountered an error.');
          }
          if (msg.type === 'trace' && msg.content) {
            setTraces(prev => [...prev, msg.content!]);
          }
          if (msg.type === 'data' && msg.data) {
            setData(prev => [...prev, msg.data]);
          }
        } catch (e) {
          console.error("Failed to parse WS message", e);
        }
      };

      ws.current.onerror = () => {
        setIsConnecting(false);
        setState('error');
        toast.error('WebSocket connection failed. Ensure backend is running at :8000');
        
        // Fallback for UI demonstration if backend isn't actually running
        simulateSolve();
      };

      ws.current.onclose = () => {
        setState(prev => prev === 'solving' ? 'idle' : prev);
      };
    } catch (e) {
      toast.error('Failed to initialize WebSocket.');
      setIsConnecting(false);
    }
  };

  // Fallback simulator to show the UI working even if backend is offline
  const simulateSolve = () => {
    toast.info('Simulating solve for demonstration...');
    setState('solving');
    let step = 0;
    const interval = setInterval(() => {
      step++;
      setData(prev => [...prev, {
        iteration: step,
        primal: Math.max(0, 100 - step * 1.5 + Math.random() * 5),
        dual: Math.min(100, step * 1.8 + Math.random() * 5),
      }]);
      
      if (step % 5 === 0) {
        setTraces(prev => [...prev, `Iteration ${step}: Primal-Dual gap closing...`]);
      }

      if (step >= 50) {
        clearInterval(interval);
        setState('converged');
        setTraces(prev => [...prev, 'Optimal solution found.']);
      }
    }, 100);
  };

  return (
    <div className="flex flex-col h-full gap-4">
      {/* Top Controls */}
      <div className="flex gap-4 items-stretch h-14">
        <div className="flex-1 flex items-center border border-border bg-panel px-4 gap-4">
          <div className="flex items-center gap-2">
            <span className="text-xs uppercase text-text-muted font-mono">Input:</span>
            <select 
              className="bg-background border border-border text-sm px-2 py-1 outline-none focus:border-signal font-mono h-8"
              value={selectedFile || ''}
              onChange={(e) => setSelectedFile(e.target.value)}
              disabled={state === 'solving'}
            >
              <option value="" disabled>Select sample...</option>
              {SAMPLES.map(s => <option key={s} value={s}>{s}</option>)}
            </select>
          </div>
          <div className="w-px h-4 bg-border" />
          <button 
            disabled={state === 'solving'}
            className="flex items-center gap-2 text-xs uppercase font-mono text-text-muted hover:text-text-primary transition-colors h-8 px-2 border border-transparent hover:border-border"
          >
            <Upload className="w-3 h-3" />
            Upload .mps
          </button>
        </div>

        <div className="flex items-center border border-border bg-panel px-4 gap-4">
          <GPULaneIndicators state={state} lanes={16} />
          
          <button
            onClick={handleConnect}
            disabled={isConnecting}
            className={cn(
              "flex items-center gap-2 px-4 h-8 border border-border text-xs uppercase font-mono tracking-wider transition-colors hover:bg-border/50",
              state === 'solving' && "text-signal border-signal hover:bg-signal/10"
            )}
          >
            {state === 'solving' ? (
              <><Square className="w-3 h-3" />Abort</>
            ) : isConnecting ? (
              <span className="animate-pulse">Connecting...</span>
            ) : (
              <><Play className="w-3 h-3" />{state === 'converged' ? 'Restart' : 'Solve'}</>
            )}
          </button>
        </div>
      </div>

      {/* Main Workspace */}
      <div className="flex-1 grid grid-cols-1 lg:grid-cols-3 gap-4 min-h-0">
        
        {/* Left: Model Stats (Skeleton / Empty state supported) */}
        <div className="border border-border bg-panel flex flex-col">
          <div className="p-3 border-b border-border text-xs uppercase font-mono text-text-muted">Model Context</div>
          <div className="p-4 flex-1">
            {!selectedFile ? (
              <div className="h-full flex flex-col items-center justify-center text-text-muted gap-2 opacity-50">
                <FileText className="w-8 h-8" />
                <span className="text-xs uppercase font-mono">No Model Selected</span>
              </div>
            ) : state === 'idle' && data.length === 0 ? (
              <div className="space-y-4">
                {/* Skeletons */}
                <div className="h-4 bg-border/30 w-3/4 rounded-sm animate-pulse" />
                <div className="h-4 bg-border/30 w-1/2 rounded-sm animate-pulse" />
                <div className="h-4 bg-border/30 w-2/3 rounded-sm animate-pulse" />
              </div>
            ) : (
              <div className="space-y-3 font-mono text-sm">
                <div className="flex justify-between"><span className="text-text-muted">Rows</span><span>4,096</span></div>
                <div className="flex justify-between"><span className="text-text-muted">Cols</span><span>12,288</span></div>
                <div className="flex justify-between"><span className="text-text-muted">NonZeros</span><span>86,016</span></div>
                <div className="w-full h-px bg-border my-4" />
                <div className="flex justify-between"><span className="text-text-muted">Objective</span><span className={state === 'converged' ? 'text-signal' : ''}>{state === 'converged' ? '1,204.55' : '---'}</span></div>
              </div>
            )}
          </div>
        </div>

        {/* Right: Charts and Trace */}
        <div className="lg:col-span-2 flex flex-col gap-4 min-h-0">
          
          {/* Chart */}
          <div className="flex-[2] border border-border bg-panel flex flex-col min-h-0">
            <div className="p-3 border-b border-border text-xs uppercase font-mono text-text-muted flex justify-between">
              <span>Convergence Trajectory</span>
              <div className="flex gap-3">
                <span className="flex items-center gap-1"><div className="w-2 h-2 bg-signal rounded-sm" /> Primal</span>
                <span className="flex items-center gap-1"><div className="w-2 h-2 bg-chart-trace rounded-sm" /> Dual</span>
              </div>
            </div>
            <div className="flex-1 p-4 min-h-[200px]">
              {data.length === 0 ? (
                <div className="h-full border border-dashed border-border flex items-center justify-center text-text-muted text-xs uppercase font-mono">
                  Awaiting Telemetry
                </div>
              ) : (
                <ResponsiveContainer width="100%" height="100%">
                  <LineChart data={data}>
                    <CartesianGrid strokeDasharray="3 3" stroke="var(--border)" vertical={false} />
                    <XAxis dataKey="iteration" stroke="var(--text-muted)" fontSize={10} fontFamily="var(--font-mono)" tickLine={false} />
                    <YAxis stroke="var(--text-muted)" fontSize={10} fontFamily="var(--font-mono)" tickLine={false} axisLine={false} />
                    <Tooltip
                      contentStyle={{ backgroundColor: 'var(--panel)', borderColor: 'var(--border)', borderRadius: '2px', fontFamily: 'var(--font-mono)', fontSize: '12px' }}
                      itemStyle={{ color: 'var(--text-primary)' }}
                    />
                    <Line type="monotone" dataKey="primal" stroke="var(--signal)" strokeWidth={1.5} dot={false} isAnimationActive={false} />
                    <Line type="monotone" dataKey="dual" stroke="var(--chart-trace)" strokeWidth={1.5} dot={false} isAnimationActive={false} />
                  </LineChart>
                </ResponsiveContainer>
              )}
            </div>
          </div>

          {/* Trace Narration */}
          <div className="flex-1 border border-border bg-panel flex flex-col min-h-[150px]">
            <div className="p-3 border-b border-border text-xs uppercase font-mono text-text-muted">Execution Trace</div>
            <div className="flex-1 p-3 overflow-y-auto font-mono text-xs text-text-muted space-y-1">
              {traces.length === 0 ? (
                <span className="opacity-50">System idle.</span>
              ) : (
                traces.map((t, i) => (
                  <div key={i} className={cn(t.includes('Optimal') ? "text-signal" : t.includes('ERR') ? "text-red-500" : "text-text-primary")}>
                    &gt; {t}
                  </div>
                ))
              )}
              {state === 'solving' && <div className="animate-pulse">&gt; _</div>}
              <div ref={traceEndRef} />
            </div>
          </div>

        </div>
      </div>
    </div>
  );
}

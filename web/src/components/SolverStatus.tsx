import { useEffect, useState } from 'react';
import { cn } from '@/lib/utils';
import { useSolverStore } from '../store/solverStore';

export type SolverState = 'idle' | 'running' | 'converged';

interface SolverStatusLightProps {
  state: SolverState;
}

export function SolverStatusLight({ state }: SolverStatusLightProps) {
  return (
    <div
      className={cn(
        "w-2.5 h-2.5 rotate-45 transition-colors duration-200",
        state === 'idle' ? "bg-text-muted" : "bg-signal",
        state === 'running' && "animate-pulse"
      )}
    />
  );
}

interface AnnunciatorPanelProps {
  state: SolverState;
  count: number;
}

export function AnnunciatorPanel({ state, count }: AnnunciatorPanelProps) {
  const [lights, setLights] = useState<boolean[]>(Array(count).fill(false));
  const tick = useSolverStore(s => s.iterationTick);

  useEffect(() => {
    if (state === 'idle') {
      setLights(Array(count).fill(false));
      return;
    }

    if (state === 'converged') {
      // Initial sync state when entering converged
      setLights(Array(count).fill(true));
      
      // All squares snap to the same on/off rhythm together
      const interval = setInterval(() => {
        setLights(prev => {
          const isOn = !prev[0]; // just toggle based on the first element
          return Array(count).fill(isOn);
        });
      }, 500);
      
      return () => clearInterval(interval);
    }

    // Running state is now driven purely by the iterationTick below,
    // so we don't need a setInterval here for 'running' anymore.
  }, [state, count]);

  useEffect(() => {
    if (state === 'running') {
      // Toggle a random subset of lights on each tick
      setLights(prev => 
        prev.map(light => (Math.random() > 0.5 ? !light : light))
      );
    }
  }, [tick, state]);

  return (
    <div className="flex items-center gap-[2px]">
      {lights.map((isOn, i) => (
        <div
          key={i}
          className={cn(
            "w-2 h-2 transition-colors duration-75",
            isOn ? "bg-signal" : "bg-border"
          )}
        />
      ))}
    </div>
  );
}

import { create } from 'zustand';
import type { SolverState } from '../components/SolverStatus';

export type ChartDataPoint = {
  iteration: number;
  primal_obj: number;
  dual_obj: number;
  elapsed_ms: number;
};

interface SolverStore {
  state: SolverState;
  iterationTick: number;
  chartData: ChartDataPoint[];
  elapsedMs: number;
  iterationsPerSec: number;
  
  setState: (state: SolverState) => void;
  addUpdate: (update: ChartDataPoint) => void;
  reset: () => void;
}

export const useSolverStore = create<SolverStore>((set) => ({
  state: 'idle',
  iterationTick: 0,
  chartData: [],
  elapsedMs: 0,
  iterationsPerSec: 0,

  setState: (state) => set({ state }),
  
  addUpdate: (update) => set((prev) => {
    // Keep max 500 points to prevent out of memory and sluggish rendering
    const maxPoints = 500;
    let newData = [...prev.chartData, update];
    if (newData.length > maxPoints) {
      // Decimate or just slice (we'll just slice for now to keep it simple, 
      // or we can drop old points if we want a scrolling window)
      // Actually, since it converges, we want to see the whole curve, so we can just downsample
      // but let's just keep up to 1000 points.
      if (newData.length > 1000) {
        newData = newData.filter((_, i) => i % 2 === 0);
      }
    }
    
    // Smooth out IPS over last 10 updates
    let ips = 0;
    if (newData.length > 1) {
      const start = newData[Math.max(0, newData.length - 10)];
      const dtMs = update.elapsed_ms - start.elapsed_ms;
      const dIter = update.iteration - start.iteration;
      if (dtMs > 0) {
        ips = (dIter / dtMs) * 1000;
      }
    }

    return {
      chartData: newData,
      elapsedMs: update.elapsed_ms,
      iterationTick: prev.iterationTick + 1,
      iterationsPerSec: ips
    };
  }),

  reset: () => set({ 
    state: 'idle', 
    iterationTick: 0, 
    chartData: [], 
    elapsedMs: 0,
    iterationsPerSec: 0
  })
}));

import { Component, type ReactNode } from 'react';
import { motion, Variants } from 'framer-motion';
import { EmptyState } from '../components/EmptyState';

const STAGES = [
  {
    n: 1,
    label: 'Parser',
    detail: 'Reads MPS/LP file format. Validates structure, extracts objective coefficients, constraint matrix (CSR), RHS, and variable bounds.',
  },
  {
    n: 2,
    label: 'Presolve',
    detail: 'Eliminates redundant rows and fixed variables, tightens bounds, and rescales the constraint matrix before dispatch.',
  },
  {
    n: 3,
    label: 'LP Core',
    sub: 'PDLP  ·  Simplex',
    detail: 'GPU-native PDLP (primal–dual first-order) for large-scale continuous relaxations; CPU Simplex fallback for small dense instances.',
  },
  {
    n: 4,
    label: 'MILP Engine',
    detail: 'Branch-and-bound over the LP core. Integer feasibility enforced per node; best-bound pruning limits the search tree.',
  },
  {
    n: 5,
    label: 'Output',
    detail: 'Primal solution vector, objective value, status code, iteration count, and wall-clock time returned to API and UI.',
  },
];

// Thin horizontal arrow — no blur, no glow
function Arrow() {
  return (
    <div className="flex items-center justify-center shrink-0 w-12" aria-hidden>
      <svg width="48" height="16" viewBox="0 0 48 16" fill="none">
        <line x1="0" y1="8" x2="40" y2="8" stroke="#E8A33D" strokeWidth="1" />
        <polyline points="36,4 44,8 36,12" stroke="#E8A33D" strokeWidth="1" fill="none" />
      </svg>
    </div>
  );
}

const containerVariants: Variants = {
  hidden: {},
  visible: {
    transition: { staggerChildren: 0.12, delayChildren: 0.1 },
  },
};

const itemVariants: Variants = {
  hidden: { opacity: 0, x: -16 },
  visible: { opacity: 1, x: 0, transition: { duration: 0.22, ease: 'easeOut' } },
};

// Minimal error boundary to catch rendering failures in the animated diagram
class DiagramErrorBoundary extends Component<
  { children: ReactNode },
  { error: Error | null }
> {
  constructor(props: { children: ReactNode }) {
    super(props);
    this.state = { error: null };
  }
  static getDerivedStateFromError(error: Error) {
    return { error };
  }
  render() {
    if (this.state.error) {
      return (
        <EmptyState
          headline="Pipeline diagram failed to render."
          body={`An unexpected error occurred while drawing the diagram: ${this.state.error.message}. The architecture data is intact — refresh the page to try again.`}
        />
      );
    }
    return this.props.children;
  }
}

export function Architecture() {
  return (
    <div className="flex flex-col gap-8 max-w-5xl w-full">

      {/* Header */}
      <div className="border-b border-border pb-4 mb-2">
        <h2 className="text-xl font-mono text-text-primary uppercase tracking-widest mb-1">
          Architecture
        </h2>
        <p className="text-sm font-sans text-text-muted max-w-2xl">
          Firefly's solve pipeline is a strict linear sequence. Each stage hands a fully
          validated, transformed problem to the next — no shared mutable state between stages.
        </p>
      </div>

      {/* Pipeline diagram — staggered fade+slide on first render only */}
      <DiagramErrorBoundary>
        <div className="overflow-x-auto pb-2">
          <motion.div
            className="flex items-start gap-0 min-w-max"
            variants={containerVariants}
            initial="hidden"
            animate="visible"
          >
            {STAGES.map((stage, i) => (
              <motion.div key={stage.n} className="flex items-start" variants={itemVariants}>
                {/* Stage box */}
                <div className="flex flex-col w-40">
                  <div className="border border-border bg-panel p-4 flex flex-col gap-2">
                    {/* Stage number + marker */}
                    <div className="flex items-center gap-2 border-b border-border pb-2 mb-1">
                      <span className="font-mono text-[10px] text-text-muted tracking-widest">
                        {stage.n.toString().padStart(2, '0')}
                      </span>
                      <div className="w-1.5 h-1.5 bg-signal shrink-0" />
                    </div>
                    <span className="font-mono text-sm text-text-primary leading-snug">
                      {stage.label}
                    </span>
                    {stage.sub && (
                      <span className="font-mono text-[10px] text-text-muted tracking-wide">
                        {stage.sub}
                      </span>
                    )}
                  </div>
                  {/* Description beneath box */}
                  <p className="text-[11px] font-sans text-text-muted leading-relaxed mt-3 pr-2">
                    {stage.detail}
                  </p>
                </div>

                {/* Arrow connector — not after final stage */}
                {i < STAGES.length - 1 && (
                  <div className="mt-8">
                    <Arrow />
                  </div>
                )}
              </motion.div>
            ))}
          </motion.div>
        </div>
      </DiagramErrorBoundary>

      {/* Supplementary specification panels */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-0 border border-border divide-y md:divide-y-0 md:divide-x divide-border bg-panel">
        <div className="flex flex-col p-5 gap-2">
          <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider">GPU Target</span>
          <span className="font-mono text-sm text-text-primary">NVIDIA RTX 4050 · sm_89</span>
          <p className="text-xs font-sans text-text-muted leading-relaxed">
            PDLP kernel compiled for compute capability 8.9. cuBLAS and cuSPARSE used
            as arithmetic primitives only — no solver logic delegated to libraries.
          </p>
        </div>
        <div className="flex flex-col p-5 gap-2">
          <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider">Convergence Criterion</span>
          <span className="font-mono text-sm text-text-primary">Primal · Dual · Gap</span>
          <p className="text-xs font-sans text-text-muted leading-relaxed">
            Solve terminates when primal infeasibility, dual infeasibility, and duality
            gap all fall below ε = 1×10⁻⁴ (relative norms).
          </p>
        </div>
        <div className="flex flex-col p-5 gap-2">
          <span className="text-[10px] font-mono text-text-muted uppercase tracking-wider">External Dependencies</span>
          <span className="font-mono text-sm text-text-primary">0 solver libraries</span>
          <p className="text-xs font-sans text-text-muted leading-relaxed">
            No HiGHS, GLPK, or CBC in core/. Decision-making logic — simplex pivots,
            PDLP iterations, B&B search — is original C++20 + CUDA code.
          </p>
        </div>
      </div>

    </div>
  );
}

import { Link } from 'react-router-dom';
import { AnnunciatorPanel } from '../components/SolverStatus';

export function Home() {
  return (
    <div className="flex flex-col gap-10 max-w-5xl w-full">
      {/* Hero Section */}
      <section className="flex flex-col lg:flex-row lg:items-start justify-between gap-12 mt-8">
        <div className="flex flex-col gap-6 max-w-2xl">
          <h1 className="text-4xl font-mono text-text-primary uppercase tracking-widest">
            Firefly
          </h1>
          <div className="space-y-4">
            <p className="text-base font-sans text-text-primary leading-relaxed">
              A sovereign, GPU-native optimization engine — built from first principles.
            </p>
            <p className="text-sm font-sans text-text-muted">
              Solves Large-Scale Linear (LP), Mixed-Integer (MILP), and Quadratic Programs (QP).
            </p>
          </div>
          
          <div className="pt-4">
            <Link
              to="/solve"
              className="inline-flex items-center justify-center bg-panel border border-border text-text-primary hover:text-signal hover:border-signal active:opacity-75 transition-colors px-6 py-2.5 font-mono text-sm uppercase tracking-wider"
            >
              Run a Solve
            </Link>
          </div>
        </div>

        {/* Visual Anchor */}
        <div className="shrink-0 p-5 border border-border bg-panel flex flex-col gap-3">
          <span className="text-xs font-mono text-text-muted uppercase tracking-widest">SM_89 CUDA Lanes [Idle]</span>
          <AnnunciatorPanel state="idle" count={32} />
        </div>
      </section>

      {/* Specifications Row */}
      <section>
        <h2 className="text-xs font-mono text-text-muted uppercase tracking-widest mb-4">System Specifications</h2>
        <div className="grid grid-cols-1 md:grid-cols-3 gap-0 border-y border-border divide-y md:divide-y-0 md:divide-x divide-border bg-panel">
          
          {/* Fact 1 */}
          <div className="flex flex-col p-5">
            <span className="text-xs font-mono text-text-muted uppercase tracking-wider mb-2">GPU Target</span>
            <span className="font-mono text-sm text-text-primary">RTX 4050 / sm_89</span>
          </div>

          {/* Fact 2 */}
          <div className="flex flex-col p-5">
            <span className="text-xs font-mono text-text-muted uppercase tracking-wider mb-2">Core Dependencies</span>
            <span className="font-mono text-sm text-text-primary">0 external solver libraries</span>
          </div>

          {/* Fact 3 */}
          <div className="flex flex-col p-5">
            <span className="text-xs font-mono text-text-muted uppercase tracking-wider mb-2">Validation</span>
            <span className="font-mono text-sm text-text-primary">Benchmarked on N public MIPLIB/Netlib instances</span>
          </div>

        </div>
      </section>
    </div>
  );
}

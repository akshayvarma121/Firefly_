/**
 * EmptyState — instrument panel aesthetic empty state.
 *
 * A small amber signal diamond acts as the visual anchor (consistent with
 * SolverStatusLight in idle mode, but here it reads as "awaiting input"
 * rather than a status indicator).  No icons, no illustrations, no glow.
 */

interface EmptyStateProps {
  headline: string;
  body: string;
  /** Optional: a call-to-action element (button or link) */
  action?: React.ReactNode;
}

export function EmptyState({ headline, body, action }: EmptyStateProps) {
  return (
    <div className="flex flex-col items-start gap-5 border border-border bg-panel px-8 py-10 w-full">
      {/* Amber diamond — signal accent used as "awaiting" indicator */}
      <div className="flex items-center gap-3">
        <div className="w-3 h-3 rotate-45 bg-signal opacity-60" />
        <span className="font-mono text-[10px] text-text-muted uppercase tracking-widest">
          Awaiting input
        </span>
      </div>

      <div className="flex flex-col gap-2 max-w-lg">
        <p className="font-mono text-base text-text-primary">{headline}</p>
        <p className="font-sans text-sm text-text-muted leading-relaxed">{body}</p>
      </div>

      {action && <div className="mt-1">{action}</div>}
    </div>
  );
}

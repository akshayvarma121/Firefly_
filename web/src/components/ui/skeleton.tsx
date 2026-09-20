import { cn } from "@/lib/utils"

/**
 * Skeleton — instrument panel aesthetic override.
 * Uses bg-border (our #33362E dim surface) with an animate-pulse opacity cycle.
 * No rounding (border-radius: 0), no box-shadow, consistent with design system.
 */
function Skeleton({
  className,
  ...props
}: React.HTMLAttributes<HTMLDivElement>) {
  return (
    <div
      className={cn("animate-pulse bg-border", className)}
      {...props}
    />
  )
}

export { Skeleton }

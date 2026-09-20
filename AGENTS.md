# The Fireflies — Firefly Solver

## What this project is
An original LP/MILP/QP optimization solver core built from mathematical
first principles, GPU-accelerated via CUDA, for SIH 2026 PS 26119.

## Three Non-Negotiable Rules
1. Never import, call, or link against any existing LP/MILP/QP solver library (SciPy's linprog, HiGHS, GLPK, SCIP, CBC, OR-Tools, Gurobi, CPLEX, Xpress) inside core/. Basic math libraries (Eigen, cuBLAS, cuSPARSE) are fine as low-level primitives only.
2. Never alter test data, input files, or fixtures to make a failing test or endpoint pass. If something fails, fix the real code or stop and ask — never edit the input that exposed the failure.
3. Never let any endpoint or function silently return a fabricated or fallback result without an explicit, visible flag (e.g. mock: true) distinguishing it from a real one. This applies even under time pressure or for "temporary" reasons.

## Stack
- core/: C++20 + CUDA, target compute capability sm_89 (RTX 4050)
- api/: Python FastAPI + pybind11 bindings over core/
- web/: React + Vite + TypeScript + Tailwind + shadcn/ui + Recharts + Framer Motion

## Brand & Design Direction
Team: The Fireflies. Product: Firefly. Visual language is scientific
instrumentation — a refinery control panel or an oscilloscope, NOT a SaaS
dashboard. No gradients, no glow/blur/bokeh effects, no emoji, no drop
shadows, no rounded "card kit" look. Flat surfaces, hairline 1px borders,
2px max corner radius.

Palette — exactly these six values, nothing else invented:
- Background #111310, Panel #1B1D18, Border/grid #33362E
- Text primary #E8E6DE, Text muted #8C8B80
- Signal accent #E8A33D — used ONLY for live/active state, never decoration
- Chart secondary trace #6B8F71 — used only for the dual-objective line

Type: IBM Plex Sans for all UI text/headings. IBM Plex Mono for every
number, label, and solver readout, no exceptions — digits must not jitter
in width as they update. No other typefaces.

Firefly motif: expressed as (1) a small status dot/diamond next to
"SOLVER" — dim idle, pulsing flat amber while solving, solid on
convergence — and (2) an annunciator-style row of small flat square
indicator lights (like an old mainframe panel) representing parallel GPU
lanes, blinking async while solving, locking into shared rhythm at
convergence. Never particles, glow, or blur. Keep this consistent across
every UI prompt below.

## Definition of done for any solver-core task
"It compiles" is not done. "It matches a known correct answer, with a test
proving it" is done.
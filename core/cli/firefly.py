"""
core/cli/firefly.py
-------------------
Firefly command-line interface.

Usage
-----
  firefly solve <file.mps> [--method auto|simplex|pdlp|milp]
                            [--gpu | --no-gpu]
                            [--output <path>]
                            [--quiet | --verbose | --debug]

  firefly benchmark  <folder>   -- solve all .mps + compare against references
  firefly solve-batch <folder>  -- solve all .mps, no reference comparison

Exit codes
----------
  0  OPTIMAL or FEASIBLE
  1  INFEASIBLE
  2  UNBOUNDED
  3  TIME_LIMIT or NODE_LIMIT
  4  ERROR / parse failure / unexpected exception
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import sys
import traceback
from io import StringIO
from typing import Optional

# ---------------------------------------------------------------------------
# Windows: add CUDA DLL directories before importing the native extension.
# The old pyd (core_old) links against DLLs in %CUDA_PATH%\bin, while the
# new one needs %CUDA_PATH%\bin\x64.  Add both.
# ---------------------------------------------------------------------------
if os.name == "nt":
    _cuda = os.environ.get("CUDA_PATH", "")
    if _cuda:
        for _sub in ("bin", os.path.join("bin", "x64")):
            _p = os.path.join(_cuda, _sub)
            if os.path.isdir(_p):
                os.add_dll_directory(_p)

# ---------------------------------------------------------------------------
# Attempt to load the native extension
# ---------------------------------------------------------------------------
try:
    import firefly_solver as _fs          # type: ignore
    _FS_AVAILABLE = True
except Exception as _import_err:          # noqa: BLE001
    _fs = None                            # type: ignore
    _FS_AVAILABLE = False
    _FS_IMPORT_ERR = _import_err
else:
    _FS_IMPORT_ERR = None

# ---------------------------------------------------------------------------
# Shared bench logic
# ---------------------------------------------------------------------------
# Support both:
#   (a) installed via `pip install -e .` → `from cli.bench import …`
#   (b) run directly:  `python core/cli/firefly.py …`
#                       → inject core/ root so the absolute import resolves.
_CLI_DIR  = os.path.dirname(os.path.abspath(__file__))
_CORE_ROOT = os.path.abspath(os.path.join(_CLI_DIR, ".."))
if _CORE_ROOT not in sys.path:
    sys.path.insert(0, _CORE_ROOT)

from cli.bench import (              # noqa: E402
    BatchSummary,
    ProblemResult,
    collect_mps_files,
    result_to_row,
    run_batch,
    solve_one,
    summary_header,
)

# ---------------------------------------------------------------------------
# Exit-code mapping
# ---------------------------------------------------------------------------
_EXIT_CODES: dict[str, int] = {
    "OPTIMAL":    0,
    "FEASIBLE":   0,
    "INFEASIBLE": 1,
    "UNBOUNDED":  2,
    "TIME_LIMIT": 3,
    "NODE_LIMIT": 3,
    "ERROR":      4,
}


def _exit_code_for(status: str) -> int:
    return _EXIT_CODES.get(status.upper(), 4)


# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------

def _write_solution(path: str, result, col_names: list[str]) -> None:
    """Write the full solution vector to *path* as CSV or JSON."""
    ext = os.path.splitext(path)[1].lower()
    sol = result.solution if result.solution else []
    names = col_names if col_names else [f"x{i}" for i in range(len(sol))]

    if ext == ".json":
        payload = {
            "status":      result.status,
            "objective":   result.objective,
            "iterations":  result.iterations,
            "wall_time_ms": result.wall_time_ms,
            "solution":    {n: v for n, v in zip(names, sol)},
            "mock":        getattr(result, "mock", False),
        }
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(payload, fh, indent=2)
    else:
        # Default: CSV (even for unknown extensions)
        with open(path, "w", newline="", encoding="utf-8") as fh:
            writer = csv.writer(fh)
            writer.writerow(["variable", "value"])
            for n, v in zip(names, sol):
                writer.writerow([n, v])


def _print_summary(result, filepath: str, quiet: bool, verbose: bool) -> None:
    """Print the human-readable solve summary (unless --quiet)."""
    if quiet:
        # Only the objective value
        if result.objective is not None:
            print(result.objective)
        return

    mock_tag = "  [MOCK — solver not available]" if getattr(result, "mock", False) else ""
    print()
    print(f"  Problem   : {os.path.basename(filepath)}")
    print(f"  Status    : {result.status}{mock_tag}")
    if result.status == "ERROR" and getattr(result, "error_message", None):
        print(f"  Error     : {result.error_message}")
    if result.objective is not None:
        print(f"  Objective : {result.objective:.10g}")
    print(f"  Time      : {result.wall_time_ms:.1f} ms")
    print(f"  Iterations: {result.iterations}")
    print()


# ---------------------------------------------------------------------------
# Sub-command: solve
# ---------------------------------------------------------------------------

def _cmd_solve(args: argparse.Namespace) -> int:
    filepath = args.file
    if not os.path.isfile(filepath):
        print(f"firefly: error: file not found: {filepath!r}", file=sys.stderr)
        return 4

    # Verbose progress callback — overwrites the same terminal line
    last_line: list[str] = [""]
    def _verbose_cb(iteration: int, primal: float, dual: float, elapsed_ms: float) -> None:
        if args.verbose:
            line = (
                f"\r  iter {iteration:>6d} | primal {primal:>14.6g} "
                f"| dual {dual:>14.6g} | {elapsed_ms/1000.0:.2f} s"
            )
            sys.stdout.write(line)
            sys.stdout.flush()
            last_line[0] = line

    cb = _verbose_cb if args.verbose else None

    try:
        result = solve_one(
            filepath,
            method=args.method,
            gpu=args.gpu,
            iteration_callback=cb,
            firefly_solver=_fs,
            raise_errors=args.debug,
        )
    except Exception as exc:
        if args.debug:
            traceback.print_exc()
        else:
            print(f"firefly: error: {exc}", file=sys.stderr)
        return 4

    if args.verbose and last_line[0]:
        # Move to a fresh line after the live progress
        print()

    _print_summary(result, filepath, quiet=args.quiet, verbose=args.verbose)

    if args.output:
        try:
            col_names: list[str] = []
            # Try to extract column names from the SparseProblem if available
            if _fs is not None:
                try:
                    prob = _fs.parse_mps(filepath)
                    # SparseProblem exposes no col_names in the Python layer;
                    # generate canonical names matching the C++ default.
                    col_names = [f"x{i}" for i in range(prob.num_vars)]
                except Exception:
                    pass
            _write_solution(args.output, result, col_names)
            if not args.quiet:
                print(f"  Solution written to: {args.output}")
        except Exception as exc:
            if args.debug:
                traceback.print_exc()
            else:
                print(f"firefly: warning: could not write output: {exc}", file=sys.stderr)

    return _exit_code_for(result.status)


# ---------------------------------------------------------------------------
# Sub-command: benchmark
# ---------------------------------------------------------------------------

# Known reference values shipped with the repo.
_BUILTIN_REFERENCES: dict[str, float] = {
    "test_problem1.mps": -10.0,
    "test_problem2.mps": -12.0,
}

def _print_batch_result(pr: ProblemResult, quiet: bool) -> None:
    if not quiet:
        print(result_to_row(pr))


def _cmd_benchmark(args: argparse.Namespace) -> int:
    folder = args.folder
    if not os.path.isdir(folder):
        print(f"firefly: error: folder not found: {folder!r}", file=sys.stderr)
        return 4

    files = collect_mps_files(folder)
    if not files:
        print(f"firefly: error: no .mps files found in {folder!r}", file=sys.stderr)
        return 4

    if not args.quiet:
        print(f"\nBenchmark: {len(files)} problem(s) in {folder!r}\n")
        print(summary_header())

    summary = run_batch(
        folder,
        method=args.method,
        gpu=args.gpu,
        references=_BUILTIN_REFERENCES,
        on_result=lambda pr: _print_batch_result(pr, args.quiet),
        firefly_solver=_fs,
    )

    if not args.quiet:
        print()
        print(f"  Total: {summary.total}  |  Passed: {summary.passed}  "
              f"|  Failed: {summary.failed}  |  Errors: {summary.errors}")
        print()

    return 0 if summary.all_passed else 4


# ---------------------------------------------------------------------------
# Sub-command: solve-batch
# ---------------------------------------------------------------------------

def _cmd_solve_batch(args: argparse.Namespace) -> int:
    folder = args.folder
    if not os.path.isdir(folder):
        print(f"firefly: error: folder not found: {folder!r}", file=sys.stderr)
        return 4

    files = collect_mps_files(folder)
    if not files:
        print(f"firefly: error: no .mps files found in {folder!r}", file=sys.stderr)
        return 4

    if not args.quiet:
        print(f"\nSolve-batch: {len(files)} problem(s) in {folder!r}\n")
        print(summary_header())

    summary = run_batch(
        folder,
        method=args.method,
        gpu=args.gpu,
        references=None,          # no reference comparison
        on_result=lambda pr: _print_batch_result(pr, args.quiet),
        firefly_solver=_fs,
    )

    if not args.quiet:
        print()
        print(f"  Total: {summary.total}  |  Errors: {summary.errors}")
        print()

    return 0 if summary.errors == 0 else 4


# ---------------------------------------------------------------------------
# Argument parser
# ---------------------------------------------------------------------------

def _build_parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(
        prog="firefly",
        description="Firefly LP/MILP/QP solver — command-line interface",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Exit codes
----------
  0   OPTIMAL or FEASIBLE
  1   INFEASIBLE
  2   UNBOUNDED
  3   TIME_LIMIT or NODE_LIMIT
  4   ERROR / unexpected failure

Examples
--------
  firefly solve problem.mps
  firefly solve problem.mps --method simplex --no-gpu --output solution.csv
  firefly solve problem.mps --verbose --output result.json
  firefly benchmark  ./problems/
  firefly solve-batch ./problems/ --method pdlp
""",
    )

    sub = root.add_subparsers(dest="command", metavar="<command>")
    sub.required = True

    # ------------------------------------------------------------------
    # Shared method/gpu flags (reused across sub-commands)
    # ------------------------------------------------------------------
    def _add_method_gpu(p: argparse.ArgumentParser) -> None:
        p.add_argument(
            "--method",
            choices=["auto", "simplex", "pdlp", "milp"],
            default="auto",
            metavar="METHOD",
            help="Solver method: auto (default), simplex, pdlp, milp",
        )
        gpu_grp = p.add_mutually_exclusive_group()
        gpu_grp.add_argument(
            "--gpu",
            dest="gpu",
            action="store_true",
            default=True,
            help="Use GPU acceleration (default)",
        )
        gpu_grp.add_argument(
            "--no-gpu",
            dest="gpu",
            action="store_false",
            help="Disable GPU; use CPU solver",
        )

    def _add_verbosity(p: argparse.ArgumentParser) -> None:
        vgrp = p.add_mutually_exclusive_group()
        vgrp.add_argument(
            "--quiet", "-q",
            action="store_true",
            default=False,
            help="Print only the final objective value",
        )
        vgrp.add_argument(
            "--verbose", "-v",
            action="store_true",
            default=False,
            help="Print live-updating iteration progress",
        )
        p.add_argument(
            "--debug",
            action="store_true",
            default=False,
            help="Show full traceback on error",
        )

    # ------------------------------------------------------------------
    # firefly solve
    # ------------------------------------------------------------------
    p_solve = sub.add_parser(
        "solve",
        help="Solve a single MPS file",
        description="Solve a single LP/MILP/QP problem in MPS format.",
    )
    p_solve.add_argument("file", metavar="<file.mps>", help="Path to the MPS file")
    _add_method_gpu(p_solve)
    p_solve.add_argument(
        "--output", "-o",
        metavar="PATH",
        default=None,
        help=(
            "Write full solution vector to PATH (.csv) or (.json); "
            "format is inferred from the file extension"
        ),
    )
    _add_verbosity(p_solve)

    # ------------------------------------------------------------------
    # firefly benchmark
    # ------------------------------------------------------------------
    p_bench = sub.add_parser(
        "benchmark",
        help="Solve all .mps files in a folder and compare against known references",
        description=(
            "Run every .mps file in FOLDER through the solver and compare "
            "results against built-in or provided reference objectives."
        ),
    )
    p_bench.add_argument("folder", metavar="<folder>", help="Directory containing .mps files")
    _add_method_gpu(p_bench)
    p_bench.add_argument(
        "--quiet", "-q",
        action="store_true",
        default=False,
        help="Suppress table output",
    )
    p_bench.add_argument(
        "--debug",
        action="store_true",
        default=False,
        help="Show full traceback on error",
    )

    # ------------------------------------------------------------------
    # firefly solve-batch
    # ------------------------------------------------------------------
    p_batch = sub.add_parser(
        "solve-batch",
        help="Solve all .mps files in a folder (no reference comparison)",
        description="Solve every .mps file in FOLDER. No reference comparison.",
    )
    p_batch.add_argument("folder", metavar="<folder>", help="Directory containing .mps files")
    _add_method_gpu(p_batch)
    p_batch.add_argument(
        "--quiet", "-q",
        action="store_true",
        default=False,
        help="Suppress table output",
    )
    p_batch.add_argument(
        "--debug",
        action="store_true",
        default=False,
        help="Show full traceback on error",
    )

    return root


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    parser = _build_parser()
    
    # If run without arguments (e.g. double-clicked in Windows Explorer)
    if len(sys.argv) == 1:
        print(r"""
  ___  _            __  _       
 | __|(_) _ _  ___ / _|| | _  _ 
 | _| | || '_|/ -_)|  _|| || || |
 |_|  |_||_|  \___||_|  |_| \_, |
                            |__/ 

       LP/MILP/QP Solver Engine - SIH 2026
""")
        parser.print_help()
        print("\n[Firefly CLI is designed to be run from the command prompt or terminal.]")
        if os.name == "nt":
            input("Press Enter to exit...")
        sys.exit(0)

    args = parser.parse_args()

    # Warn if the native solver is unavailable (unless --quiet)
    quiet = getattr(args, "quiet", False)
    if not _FS_AVAILABLE and not quiet:
        print(
            f"[WARNING] firefly_solver native extension not loaded: {_FS_IMPORT_ERR}\n"
            "          Results are mock values and are NOT real solver output.",
            file=sys.stderr,
        )

    debug = getattr(args, "debug", False)

    try:
        if args.command == "solve":
            code = _cmd_solve(args)
        elif args.command == "benchmark":
            code = _cmd_benchmark(args)
        elif args.command == "solve-batch":
            code = _cmd_solve_batch(args)
        else:
            parser.print_help()
            code = 4
    except Exception as exc:
        if debug:
            traceback.print_exc()
        else:
            print(f"firefly: unexpected error: {exc}", file=sys.stderr)
        code = 4

    sys.exit(code)


if __name__ == "__main__":
    main()

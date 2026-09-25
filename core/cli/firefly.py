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
# TrueColor ANSI Theme (Firefly Brand)
# ---------------------------------------------------------------------------
import ctypes
if os.name == 'nt':
    try:
        kernel32 = ctypes.windll.kernel32
        kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
    except Exception:
        pass

class Theme:
    PRIMARY = "\x1b[38;2;232;230;222m"
    MUTED   = "\x1b[38;2;140;139;128m"
    ACCENT  = "\x1b[38;2;232;163;61m"
    SECOND  = "\x1b[38;2;107;143;113m"
    RESET   = "\x1b[0m"

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

from cli.bench import (              # type: ignore
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
    print(f"  {Theme.MUTED}Problem   :{Theme.RESET} {Theme.PRIMARY}{os.path.basename(filepath)}{Theme.RESET}")
    print(f"  {Theme.MUTED}Status    :{Theme.RESET} {Theme.ACCENT}{result.status}{mock_tag}{Theme.RESET}")
    if result.status == "ERROR" and getattr(result, "error_message", None):
        print(f"  {Theme.MUTED}Error     :{Theme.RESET} \x1b[31m{result.error_message}{Theme.RESET}")
    if result.objective is not None:
        print(f"  {Theme.MUTED}Objective :{Theme.RESET} {Theme.SECOND}{result.objective:.10g}{Theme.RESET}")
    print(f"  {Theme.MUTED}Time      :{Theme.RESET} {Theme.PRIMARY}{result.wall_time_ms:.1f} ms{Theme.RESET}")
    print(f"  {Theme.MUTED}Iterations:{Theme.RESET} {Theme.PRIMARY}{result.iterations}{Theme.RESET}")
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

    _add_recent_solve(args.file, result.objective, result.status, result.wall_time_ms)
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
# Sub-command: test-standard
# ---------------------------------------------------------------------------

def _cmd_test_standard(args: argparse.Namespace) -> int:
    import tempfile
    import urllib.request
    
    # Standard small netlib problems
    files = ["afiro.mps", "adlittle.mps", "israel.mps"]
    
    print(f"\n{Theme.ACCENT}Downloading standard test problems (Netlib)...{Theme.RESET}")
    with tempfile.TemporaryDirectory() as tmpdir:
        for name in files:
            url = f"https://raw.githubusercontent.com/ERGO-Code/HiGHS/master/check/instances/{name}"
            out_path = os.path.join(tmpdir, name)
            print(f"  Fetching {name}...")
            try:
                req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
                with urllib.request.urlopen(req, timeout=10) as response:
                    with open(out_path, 'wb') as f:
                        f.write(response.read())
            except Exception as e:
                print(f"{Theme.MUTED}Failed to download {name}: {e}{Theme.RESET}")
                return 4
                
        print(f"\n{Theme.ACCENT}Running Solver on Standard Problems...{Theme.RESET}\n")
        print(summary_header())
        
        netlib_refs = {
            "afiro.mps": -464.75314286,
            "adlittle.mps": 225494.96316,
            "israel.mps": -896644.82186,
        }

        summary = run_batch(
            tmpdir,
            method=args.method,
            gpu=args.gpu,
            references=netlib_refs,
            on_result=lambda pr: _print_batch_result(pr, args.quiet),
            firefly_solver=_fs,
        )
        
        print()
        print(f"  Total: {summary.total}  |  Passed: {summary.passed}  "
              f"|  Failed: {summary.failed}  |  Errors: {summary.errors}")
        if summary.all_passed:
            print(f"\n  {Theme.PRIMARY}All standard tests completed successfully!{Theme.RESET}")
        else:
            print(f"\n  {Theme.MUTED}Some tests encountered errors or missed the optimum.{Theme.RESET}")
        print()
        
    return 0 if summary.all_passed else 4

# ---------------------------------------------------------------------------
# Sub-command: update
# ---------------------------------------------------------------------------

def _cmd_update(args: argparse.Namespace) -> int:
    import subprocess
    import tempfile
    import urllib.request

    print(f"\n{Theme.ACCENT}Checking for updates...{Theme.RESET}")

    if os.name != "nt":
        print("Update not supported on this OS via CLI yet.")
        return 4

    # Download the install script to a temp file so we can run it
    # after this process exits (releasing the file lock on firefly.exe).
    # We write a small wrapper that:
    #   1. Waits 2 s for this process to fully exit
    #   2. Runs curl to overwrite firefly.exe
    #   3. Prints a clean success message
    exe_path = os.path.abspath(sys.executable if getattr(sys, 'frozen', False) else __file__)
    install_dir = os.path.dirname(exe_path)
    exe_dest = os.path.join(install_dir, "firefly.exe")
    release_url = "https://github.com/akshayvarma121/Firefly_solver/releases/download/v0.1.0/firefly.exe"

    ps_lines = [
        "Start-Sleep -Seconds 2",
        f'Write-Host ""',
        f'Write-Host "  Downloading latest Firefly..." -ForegroundColor Yellow',
        f'Write-Host ""',
        f'curl.exe -L "{release_url}" -o "{exe_dest}"',
        f'Write-Host ""',
        f'if ($LASTEXITCODE -eq 0) {{',
        f'    Write-Host "  ✓ Firefly updated successfully!" -ForegroundColor Green',
        f'}} else {{',
        f'    Write-Host "  ✗ Update failed. Please re-run: irm https://bit.ly/install-firefly | iex" -ForegroundColor Red',
        f'}}',
        f'Write-Host ""',
        f'Write-Host "  Run `firefly home` to see all available commands." -ForegroundColor Gray',
        f'Write-Host ""',
        f'Start-Sleep -Seconds 3',
    ]

    tmp = tempfile.NamedTemporaryFile(mode='w', suffix='.ps1', delete=False, encoding='utf-8')
    tmp.write('\n'.join(ps_lines))
    tmp.close()

    subprocess.Popen(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
         "-File", tmp.name],
        creationflags=subprocess.CREATE_NEW_CONSOLE,
    )

    print(f"  {Theme.PRIMARY}Update is downloading in the new window that just opened.{Theme.RESET}")
    print(f"  {Theme.MUTED}This window is now safe to close.{Theme.RESET}\n")
    return 0

# ---------------------------------------------------------------------------
# Sub-command: test
# ---------------------------------------------------------------------------

def _cmd_test(args: argparse.Namespace) -> int:
    import subprocess
    cli_dir = os.path.dirname(os.path.abspath(__file__))
    core_dir = os.path.abspath(os.path.join(cli_dir, ".."))
    root_dir = os.path.abspath(os.path.join(core_dir, ".."))
    
    if os.name == "nt":
        script_path = os.path.join(root_dir, "run_audit.bat")
        cwd = root_dir
    else:
        script_path = os.path.join(core_dir, "audit.sh")
        cwd = core_dir
    
    if not os.path.isfile(script_path):
        print(f"firefly: error: test script not found: {script_path}", file=sys.stderr)
        return 4
        
    try:
        print(f"\n{Theme.ACCENT}Running Firefly full audit suite...{Theme.RESET}")
        
        if os.name == "nt":
            return subprocess.call([script_path], cwd=cwd)
        else:
            return subprocess.call(["bash", script_path], cwd=cwd)
    except Exception as exc:
        if getattr(args, "debug", False):
            import traceback
            traceback.print_exc()
        print(f"firefly: error running tests: {exc}", file=sys.stderr)
        return 4

# ---------------------------------------------------------------------------
# Utility sub-commands (Version, Recent, Audit)
# ---------------------------------------------------------------------------

def _cmd_version(args: argparse.Namespace) -> int:
    print(f"{Theme.ACCENT}Firefly Solver Engine{Theme.RESET} v0.1.0")
    print(f"{Theme.MUTED}Build: sm_89 CUDA-accelerated{Theme.RESET}")
    return 0

def _get_recent_file() -> str:
    app_data = os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "Firefly")
    os.makedirs(app_data, exist_ok=True)
    return os.path.join(app_data, "recent_solves.json")

def _add_recent_solve(filepath: str, objective: float, status: str, time_ms: float) -> None:
    history_file = _get_recent_file()
    try:
        if os.path.exists(history_file):
            with open(history_file, "r", encoding="utf-8") as f:
                history = json.load(f)
        else:
            history = []
    except Exception:
        history = []
        
    entry = {
        "file": os.path.abspath(filepath),
        "objective": objective,
        "status": status,
        "time_ms": time_ms,
        "timestamp": __import__("time").time()
    }
    history.insert(0, entry)
    history = history[:5]  # Keep last 5
    
    try:
        with open(history_file, "w", encoding="utf-8") as f:
            json.dump(history, f)
    except Exception:
        pass

def _cmd_recent(args: argparse.Namespace) -> int:
    history_file = _get_recent_file()
    if not os.path.exists(history_file):
        print(f"\n  {Theme.MUTED}No recent solves found.{Theme.RESET}\n")
        return 0
        
    try:
        with open(history_file, "r", encoding="utf-8") as f:
            history = json.load(f)
    except Exception:
        print(f"\n  {Theme.MUTED}Failed to read recent history.{Theme.RESET}\n")
        return 4
        
    print(f"\n{Theme.ACCENT}▶ RECENT SOLVES{Theme.RESET}")
    for i, entry in enumerate(history):
        fname = os.path.basename(entry["file"])
        obj = f"{entry['objective']:.6g}" if entry["objective"] is not None else "N/A"
        time_struct = __import__("time").localtime(entry["timestamp"])
        time_str = __import__("time").strftime("%Y-%m-%d %H:%M", time_struct)
        
        print(f"  {Theme.PRIMARY}{i+1}. {fname}{Theme.RESET}")
        print(f"     Status: {entry['status']} | Objective: {obj} | Time: {entry['time_ms']:.1f}ms | {time_str}")
        print()
    return 0

def _cmd_audit(args: argparse.Namespace) -> int:
    import subprocess
    cli_dir = os.path.dirname(os.path.abspath(__file__))
    core_dir = os.path.abspath(os.path.join(cli_dir, ".."))
    audit_dir = os.path.join(core_dir, "audit_reports")
    
    if not os.path.exists(audit_dir):
        try:
            os.makedirs(audit_dir)
        except Exception:
            print(f"firefly: error: could not create directory {audit_dir}", file=sys.stderr)
            return 4
            
    print(f"\n{Theme.ACCENT}Opening audit reports directory...{Theme.RESET}")
    if os.name == "nt":
        os.startfile(audit_dir)
    elif sys.platform == "darwin":
        subprocess.Popen(["open", audit_dir])
    else:
        subprocess.Popen(["xdg-open", audit_dir])
    return 0

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
  firefly test
  firefly test-standard
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

    # ------------------------------------------------------------------
    # firefly test
    # ------------------------------------------------------------------
    p_test = sub.add_parser(
        "test",
        help="Run the internal solver test suite",
        description="Run all internal C++ unit tests for the Firefly solver.",
    )
    p_test.add_argument(
        "--debug",
        action="store_true",
        default=False,
        help="Show full traceback on error",
    )

    # ------------------------------------------------------------------
    # firefly test-standard
    # ------------------------------------------------------------------
    p_test_std = sub.add_parser(
        "test-standard",
        help="Test the solver against real, standard MPS problems (Netlib)",
        description="Downloads a few standard Netlib MPS files and solves them visibly.",
    )
    _add_method_gpu(p_test_std)
    p_test_std.add_argument(
        "--quiet", "-q",
        action="store_true",
        default=False,
        help="Suppress table output",
    )

    # ------------------------------------------------------------------
    # firefly update
    # ------------------------------------------------------------------
    p_update = sub.add_parser(
        "update",
        help="Update Firefly CLI to the latest version from GitHub",
        description="Downloads the latest executable and replaces the current one.",
    )

    # ------------------------------------------------------------------
    # firefly home
    # ------------------------------------------------------------------
    p_home = sub.add_parser(
        "home",
        help="Show the Firefly homepage and command list",
    )
    
    # ------------------------------------------------------------------
    # firefly audit
    # ------------------------------------------------------------------
    p_audit = sub.add_parser(
        "audit",
        help="Open the audit reports directory",
    )
    
    # ------------------------------------------------------------------
    # firefly recent
    # ------------------------------------------------------------------
    p_recent = sub.add_parser(
        "recent",
        help="Show the last 5 solved files and their objectives",
    )
    
    # ------------------------------------------------------------------
    # firefly version
    # ------------------------------------------------------------------
    p_version = sub.add_parser(
        "version",
        help="Show current engine version",
    )

    return root


# ---------------------------------------------------------------------------
# ASCII Logo
# ---------------------------------------------------------------------------
def _print_firefly_logo() -> None:
    print(f"""
{Theme.ACCENT}      \\ /       {Theme.PRIMARY}  ___ _            __ _       
{Theme.ACCENT}======= ======= {Theme.PRIMARY} | __|(_) _ _  ___ / _|| | _  _ 
{Theme.ACCENT}  ====   ====   {Theme.PRIMARY} | _| | || '_|/ -_)|  _|| || || |
{Theme.ACCENT}   /  | |  \\    {Theme.PRIMARY} |_|  |_||_|  \\___||_|  |_| \\_, |
{Theme.ACCENT}  /   | |   \\   {Theme.PRIMARY}                            |__/ 
{Theme.ACCENT}      | |       
{Theme.ACCENT}      | |       {Theme.MUTED}LP/MILP/QP Solver Engine - SIH 2026{Theme.RESET}""")

def _print_homepage() -> None:
    _print_firefly_logo()
    print(f"\n{Theme.PRIMARY}Welcome to the Firefly Solver Engine!{Theme.RESET}\n")
    
    print(f"{Theme.ACCENT}▶ CORE COMMANDS{Theme.RESET}")
    print(f"  {Theme.PRIMARY}firefly solve <file.mps>{Theme.RESET}    Solve a single LP/MILP/QP problem")
    print(f"  {Theme.PRIMARY}firefly solve-batch <folder>{Theme.RESET} Solve all .mps files in a directory")
    print()
    
    print(f"{Theme.ACCENT}▶ TESTING & BENCHMARKING{Theme.RESET}")
    print(f"  {Theme.PRIMARY}firefly benchmark <folder>{Theme.RESET}   Run and compare against known optimums")
    print(f"  {Theme.PRIMARY}firefly test-standard{Theme.RESET}        Download & solve Netlib standard problems")
    print(f"  {Theme.PRIMARY}firefly test{Theme.RESET}                 Run the internal C++ test suite")
    print()
    
    print(f"{Theme.ACCENT}▶ UTILITIES{Theme.RESET}")
    print(f"  {Theme.PRIMARY}firefly audit{Theme.RESET}                Open the audit reports directory")
    print(f"  {Theme.PRIMARY}firefly recent{Theme.RESET}               Show recent solve results")
    print(f"  {Theme.PRIMARY}firefly update{Theme.RESET}               Auto-update this executable to the newest version")
    print(f"  {Theme.PRIMARY}firefly version{Theme.RESET}              Show current engine version")
    print(f"  {Theme.PRIMARY}firefly home{Theme.RESET}                 Show this beautiful homepage")
    print()
    
    print(f"{Theme.ACCENT}▶ LINKS{Theme.RESET}")
    print(f"  {Theme.PRIMARY}GitHub Repository{Theme.RESET}            https://github.com/akshayvarma121/Firefly_solver")
    print(f"  {Theme.PRIMARY}Official Website{Theme.RESET}             https://firefly-solver.vercel.app")
    print()
    
    print(f"{Theme.MUTED}Tip: Use any command with -h (e.g., `firefly solve -h`) to see its specific flags.{Theme.RESET}\n")

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    # Quick solve shortcut: if `firefly path/to/file.mps` is used, insert "solve"
    if len(sys.argv) == 2 and not sys.argv[1].startswith("-") and sys.argv[1].lower().endswith(".mps"):
        sys.argv.insert(1, "solve")

    parser = _build_parser()
    interactive_mode = False
    
    # If run without arguments (e.g. double-clicked in Windows Explorer)
    if len(sys.argv) == 1:
        _print_firefly_logo()
        print()
        if os.name == "nt":
            print("Welcome to Firefly! For advanced usage, run this tool from a command prompt.")
            print("To start a solve right now, you can drag and drop a .mps file here.")
            while True:
                try:
                    user_input = input(f"\n{Theme.PRIMARY}Drop your .mps file here{Theme.RESET} (or press Enter for help): ").strip().strip('"').strip("'")
                except (EOFError, KeyboardInterrupt):
                    user_input = ""
                    break
                    
                if not user_input:
                    break
                    
                if os.path.isfile(user_input):
                    print(f"\nStarting solve for {os.path.basename(user_input)}...\n")
                    interactive_mode = True
                    sys.argv.extend(["solve", user_input])
                    break
                else:
                    print(f"  \x1b[31m[Error] File not found: {user_input}{Theme.RESET}")
            
            if not interactive_mode:
                _print_homepage()
                print("\n[Firefly CLI is designed to be run from the command prompt or terminal.]")
                try:
                    input("Press Enter to exit...")
                except (EOFError, KeyboardInterrupt):
                    pass
                sys.exit(0)
        else:
            _print_homepage()
            sys.exit(0)


    args = parser.parse_args()

    quiet = getattr(args, "quiet", False)
    
    # Print the logo for normal CLI runs — skip for home/help since _print_homepage already prints it
    if not quiet and len(sys.argv) > 1 and not interactive_mode and args.command not in ["home", "help"]:
        _print_firefly_logo()

    # Warn if the native solver is unavailable (unless --quiet)
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
        elif args.command == "test":
            code = _cmd_test(args)
        elif args.command == "test-standard":
            code = _cmd_test_standard(args)
        elif args.command == "update":
            code = _cmd_update(args)
        elif args.command == "audit":
            code = _cmd_audit(args)
        elif args.command == "recent":
            code = _cmd_recent(args)
        elif args.command == "version":
            code = _cmd_version(args)
        elif args.command in ["home", "help"]:
            _print_homepage()
            code = 0
        else:
            _print_homepage()
            code = 4
    except Exception as exc:
        if debug:
            traceback.print_exc()
        else:
            print(f"firefly: unexpected error: {exc}", file=sys.stderr)
        code = 4

    if interactive_mode and os.name == "nt":
        print("\n[Done]")
        try:
            input("Press Enter to exit...")
        except (EOFError, KeyboardInterrupt):
            pass

    if not quiet and args.command not in ["home", "help", "update"] and not interactive_mode:
        print("\n" + "="*70)
        _print_homepage()

    sys.exit(code)


if __name__ == "__main__":
    main()

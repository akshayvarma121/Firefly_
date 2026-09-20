import os
import sys

# ---------------------------------------------------------------------------
# Windows DLL search path — MUST run before any import of firefly_solver.
# ---------------------------------------------------------------------------
if os.name == "nt":
    cuda_path = os.environ.get("CUDA_PATH")
    if cuda_path:
        _cuda_bin = os.path.join(cuda_path, "bin", "x64")
        if os.path.isdir(_cuda_bin):
            os.add_dll_directory(_cuda_bin)

import firefly_solver
import narration

def downsample_history(history, max_points=100):
    n = len(history)
    if n <= max_points:
        return history
    
    # Always include first and last
    indices = [int(i * (n - 1) / (max_points - 1)) for i in range(max_points)]
    return [history[i] for i in indices]

def build_trace_from_prob(p, method: str, gpu: bool):
    """
    Given a parsed problem, performs one real solve.
    Returns the trace and the SolveResult.
    """
    trace = []
    
    is_milp = any(p.is_integer) if hasattr(p, 'is_integer') and p.is_integer else False
    
    parse_data = {
        "stage": "parse",
        "vars": p.num_vars,
        "constrs": p.num_constrs,
        "is_milp": is_milp,
        "sense": "minimize"
    }
    parse_data["narration"] = narration.describe_parse(parse_data)
    trace.append(parse_data)
    
    history = []
    
    def iteration_callback(it, p_obj, d_obj, ms):
        if is_milp:
            history.append({
                "node": int(it),
                "incumbent": float(p_obj),
                "bound": float(d_obj),
                "time_ms": float(ms)
            })
        else:
            history.append({
                "iteration": int(it),
                "primal_obj": float(p_obj),
                "dual_obj": float(d_obj),
                "time_ms": float(ms)
            })

    # Execute exactly one real solve
    res = firefly_solver.solve(p, method=method, gpu=gpu, iteration_callback=iteration_callback)
    
    # 2. Presolve Stage
    if hasattr(res, 'presolve_stats'):
        ps = res.presolve_stats
        presolve_data = {
            "stage": "presolve",
            "original_rows": ps.original_rows,
            "original_cols": ps.original_cols,
            "reduced_rows": ps.reduced_rows,
            "reduced_cols": ps.reduced_cols,
            "rows_removed": ps.rows_removed,
            "bounds_tightened": ps.bounds_tightened,
            "variables_fixed": ps.variables_fixed,
            "scaling_applied": ps.scaling_applied
        }
        presolve_data["narration"] = narration.describe_presolve(presolve_data)
        trace.append(presolve_data)
    else:
        presolve_data = {
            "stage": "presolve",
            "message": "PresolveStats not available"
        }
        presolve_data["narration"] = narration.describe_presolve(presolve_data)
        trace.append(presolve_data)
        
    sampled_history = downsample_history(history, 100)
    
    # 3 & 4. LP Core & MILP Stages
    if is_milp:
        lp_core_data = {
            "stage": "lp_core",
            "message": "LP relaxations solved within B&B"
        }
        lp_core_data["narration"] = narration.describe_lp_core(lp_core_data)
        trace.append(lp_core_data)
        
        milp_data = {
            "stage": "milp",
            "history": sampled_history
        }
        milp_data["narration"] = narration.describe_milp(milp_data)
        trace.append(milp_data)
    else:
        lp_core_data = {
            "stage": "lp_core",
            "history": sampled_history,
            "message": f"Final LP iterations: {res.iterations}"
        }
        lp_core_data["narration"] = narration.describe_lp_core(lp_core_data)
        trace.append(lp_core_data)
        
    # 5. Output Stage
    output_data = {
        "stage": "output",
        "status": res.status,
        "objective": res.objective,
        "iterations": res.iterations,
        "time_ms": res.wall_time_ms
    }
    output_data["narration"] = narration.describe_output(output_data)
    trace.append(output_data)
    
    return trace, res

def build_solve_trace(mps_path: str, method: str, gpu: bool):
    """
    Parses the MPS file and performs one real solve.
    Returns an ordered list of stage entries tracking the solve lifecycle.
    """
    p = firefly_solver.parse_mps(mps_path)
    trace, _ = build_trace_from_prob(p, method, gpu)
    return trace

if __name__ == "__main__":
    import argparse
    import json
    
    parser = argparse.ArgumentParser()
    parser.add_argument("mps_path", help="Path to MPS file")
    parser.add_argument("--method", default="auto", help="Solver method")
    parser.add_argument("--no-gpu", action="store_true", help="Disable GPU")
    args = parser.parse_args()
    
    trace_data = build_solve_trace(args.mps_path, args.method, not args.no_gpu)
    print(json.dumps(trace_data, indent=2))

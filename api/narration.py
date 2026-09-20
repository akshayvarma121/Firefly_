def describe_parse(data: dict) -> str:
    prob_type = "mixed-integer" if data.get("is_milp") else "linear"
    return (f"Parsed a {prob_type} programming problem to {data.get('sense', 'minimize')} "
            f"an objective over {data.get('vars')} variables and {data.get('constrs')} constraints.")

def describe_presolve(data: dict) -> str:
    if "message" in data:
        return data["message"]
        
    removed_rows = data.get("rows_removed", 0)
    fixed_vars = data.get("variables_fixed", 0)
    tightened = data.get("bounds_tightened", 0)
    
    if removed_rows == 0 and fixed_vars == 0 and tightened == 0:
        return "The presolver found no obvious simplifications, leaving the problem at its original size."
        
    return (f"Presolve eliminated {removed_rows} constraints and fixed {fixed_vars} variables, "
            f"reducing the matrix to {data.get('reduced_rows')} rows and {data.get('reduced_cols')} columns.")

def describe_lp_core(data: dict) -> str:
    if "history" in data and len(data["history"]) > 0:
        iters = data["history"][-1].get("iteration", 0)
        return f"The continuous solver converged after {iters} iterations."
    return data.get("message", "LP continuous relaxations were solved internally.")

def describe_milp(data: dict) -> str:
    if "history" not in data or not data["history"]:
        return "Branch-and-bound exploration completed instantly."
    nodes = data["history"][-1].get("node", 0)
    return f"The branch-and-bound tree was explored across {nodes} nodes to prove integer optimality."

def describe_output(data: dict) -> str:
    status = data.get("status", "UNKNOWN")
    obj = data.get("objective", 0.0)
    time_ms = data.get("time_ms", 0.0)
    iters = data.get("iterations", 0)
    
    if status == "OPTIMAL":
        return f"An optimal solution with objective {obj:g} was found in {time_ms:g} ms."
    elif status == "INFEASIBLE":
        return f"The solver proved the problem is mathematically infeasible after {iters} iterations."
    elif status == "UNBOUNDED":
        return f"The solver stopped because the objective is unbounded and can be improved infinitely."
    elif status in ("TIME_LIMIT", "NODE_LIMIT"):
        return f"The solve was interrupted by a limit after {time_ms:g} ms, returning the best known state."
    else:
        return f"The solver terminated with status {status}."

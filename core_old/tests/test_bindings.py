import os
import sys

# Windows DLL loading for CUDA
if os.name == "nt":
    cuda_path = os.environ.get("CUDA_PATH")
    if cuda_path:
        try:
            os.add_dll_directory(os.path.join(cuda_path, "bin", "x64"))
        except:
            pass

import firefly_solver

def test_bindings():
    mps_data = """NAME   TEST
ROWS
 N  OBJ
 L  C1
 L  C2
COLUMNS
    X         OBJ       -5.5
    X         C1        1.0
    X         C2        5.0
    Y         OBJ       -2.1
    Y         C1        1.0
    Y         C2        9.0
RHS
    RHS1      C1        5.2
    RHS1      C2        45.0
BOUNDS
 LO BND1      X         0.0
 LO BND1      Y         0.0
ENDATA
"""

    print("Parsing MPS...")
    problem = firefly_solver.parse_mps_string(mps_data)
    
    print(f"Problem parsed: {problem.name}")
    print(f"Variables: {problem.num_vars}, Constraints: {problem.num_constrs}")

    def on_iteration(iter_num, primal_obj, dual_obj, elapsed_ms):
        print(f"Iter {iter_num}: primal={primal_obj:.4f}, dual={dual_obj:.4f}, time={elapsed_ms:.2f}ms")

    print("Solving with PDLP...")
    # Force GPU to false since this is a small test
    result = firefly_solver.solve(problem, method="pdlp", gpu=False, iteration_callback=on_iteration)

    print(f"Status: {result.status}")
    print(f"Objective: {result.objective}")
    print(f"Solution: {result.solution}")
    print(f"Time: {result.wall_time_ms} ms")
    print(f"Iterations: {result.iterations}")

if __name__ == "__main__":
    test_bindings()

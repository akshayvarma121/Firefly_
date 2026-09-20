import os
import sys

# Windows DLL loading for CUDA
if os.name == "nt":
    cuda_path = os.environ.get("CUDA_PATH")
    if cuda_path:
        os.add_dll_directory(os.path.join(cuda_path, "bin", "x64"))
        
import firefly_solver

def test_parse_problem2():
    # Path to test_problem2.mps
    script_dir = os.path.dirname(__file__)
    filepath = os.path.join(script_dir, "..", "..", "api", "sample_problems", "test_problem2.mps")
    
    print(f"Parsing {filepath} ...")
    prob = firefly_solver.parse_mps(filepath)
    
    # Assertions
    assert prob.num_vars == 2, f"Expected 2 variables, got {prob.num_vars}"
    assert prob.num_constrs == 1, f"Expected 1 constraint, got {prob.num_constrs}"
    assert prob.is_integer == [True, True], f"Expected both variables to be integer, got {prob.is_integer}"
    print("SUCCESS: test_problem2.mps parsed correctly, variables flagged as integer.")

if __name__ == "__main__":
    test_parse_problem2()

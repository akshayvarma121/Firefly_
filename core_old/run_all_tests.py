import os
import sys
import subprocess
import glob
import time

def print_header(title):
    print(f"\n{'='*60}\n{title}\n{'='*60}")

def run_command(cmd, cwd=None, env=None, timeout=300):
    try:
        result = subprocess.run(
            cmd,
            cwd=cwd,
            env=env,
            shell=True,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout
        )
        return result.returncode == 0, result.stdout
    except subprocess.TimeoutExpired as e:
        return False, f"TIMEOUT after {timeout} seconds\n" + (e.stdout if e.stdout else "")
    except Exception as e:
        return False, str(e)

def main():
    core_dir = os.path.dirname(os.path.abspath(__file__))
    build_dir = os.path.join(core_dir, "build")
    
    if not os.path.exists(build_dir):
        os.makedirs(build_dir)
        
    print_header("PHASE 1: BUILD")
    print("Running CMake Configure...")
    success, out = run_command(f"cmake .. -DPython_EXECUTABLE=\"{sys.executable}\" -DPYTHON_EXECUTABLE=\"{sys.executable}\" -DPython3_EXECUTABLE=\"{sys.executable}\"", cwd=build_dir)
    if not success:
        print(out)
        print("FAIL: CMake Configure failed")
        sys.exit(1)
        
    print("Running CMake Build (Release)...")
    success, out = run_command("cmake --build . --config Release", cwd=build_dir)
    if not success:
        print(out)
        print("FAIL: CMake Build failed")
        sys.exit(1)
        
    print("Build Successful.")
    
    print_header("PHASE 2: C++ UNIT TESTS")
    cpp_tests = [
        "firefly_test.exe",
        "firefly_bnb_test.exe",
        "firefly_pdlp_test.exe",
        "firefly_presolve_test.exe",
        "firefly_simplex_test.exe"
    ]
    
    results = {}
    
    for exe in cpp_tests:
        exe_path = os.path.join(build_dir, "Release", exe)
        if not os.path.exists(exe_path):
            print(f"[{exe}] NOT FOUND")
            results[exe] = "FAIL (Not Found)"
            continue
            
        print(f"Running {exe}...")
        # 30 second timeout for C++ tests to prevent hanging (e.g. bnb)
        success, out = run_command(exe_path, cwd=build_dir, timeout=120)
        if success:
            print(f"[{exe}] PASS")
            results[exe] = "PASS"
        else:
            print(f"[{exe}] FAIL")
            print(out)
            results[exe] = "FAIL"

    print_header("PHASE 3: PYTHON UNIT TESTS")
    env = os.environ.copy()
    release_dir = os.path.join(build_dir, "Release")
    
    if "PYTHONPATH" in env:
        env["PYTHONPATH"] = f"{release_dir};{env['PYTHONPATH']}"
    else:
        env["PYTHONPATH"] = release_dir
        
    py_tests = ["test_parser.py", "test_bindings.py"]
    for py_test in py_tests:
        test_path = os.path.join(core_dir, "tests", py_test)
        print(f"Running {py_test}...")
        success, out = run_command(f"{sys.executable} {test_path}", cwd=core_dir, env=env, timeout=60)
        if success:
            print(f"[{py_test}] PASS")
            results[py_test] = "PASS"
        else:
            print(f"[{py_test}] FAIL")
            print(out)
            results[py_test] = "FAIL"
            
    print_header("PHASE 4: REGRESSION TESTS")
    regression_dir = os.path.join(core_dir, "tests", "regression")
    mps_files = glob.glob(os.path.join(regression_dir, "*.mps"))
    
    regression_runner = os.path.join(regression_dir, "run_regression.py")
    runner_code = """import sys, os
if os.name == "nt":
    cuda_path = os.environ.get("CUDA_PATH")
    if cuda_path:
        try:
            os.add_dll_directory(os.path.join(cuda_path, "bin", "x64"))
        except:
            pass

try:
    import firefly_solver
except ImportError as e:
    print(f"ImportError: {e}")
    sys.exit(1)

mps_file = sys.argv[1]
basename = os.path.basename(mps_file)

try:
    prob = firefly_solver.parse_mps(mps_file)
except Exception as e:
    if "broken" in basename:
        print("Expected parse exception caught. PASS.")
        sys.exit(0)
    else:
        print(f"Unexpected parse exception: {e}")
        sys.exit(1)

if "broken" in basename:
    print("Expected parse failure, but it succeeded! FAIL.")
    sys.exit(1)

try:
    res = firefly_solver.solve(prob, method="pdlp", gpu=False)
    print(f"Status: {res.status}")
except Exception as e:
    print(f"Solve exception: {e}")
    sys.exit(1)
"""
    with open(regression_runner, "w") as f:
        f.write(runner_code)
        
    for mps_file in mps_files:
        name = os.path.basename(mps_file)
        print(f"Running regression test: {name}...")
        success, out = run_command(f"{sys.executable} {regression_runner} {mps_file}", cwd=core_dir, env=env, timeout=60)
        if success:
            print(f"[{name}] PASS")
            results[name] = "PASS"
        else:
            print(f"[{name}] FAIL")
            print(out)
            results[name] = "FAIL"

    if os.path.exists(regression_runner):
        os.remove(regression_runner)

    print_header("TEST SUMMARY")
    all_passed = True
    for test, status in results.items():
        print(f"{test.ljust(30)} {status}")
        if status != "PASS":
            all_passed = False
            
    if all_passed:
        print("\nSUCCESS: All tests passed!")
        sys.exit(0)
    else:
        print("\nFAILURE: Some tests failed.")
        sys.exit(1)

if __name__ == "__main__":
    main()

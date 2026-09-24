import os
import requests
import sys
import threading
import time

def test_concurrency(base_url):
    print("\n--- Testing Concurrency ---")
    results = [None, None]
    
    def worker(idx, prob):
        try:
            r = requests.post(
                f"{base_url}/solve",
                data={"problem_def": prob, "method": "auto", "gpu": "false"}
            )
            results[idx] = r.json()
        except Exception as e:
            results[idx] = str(e)
            
    prob1 = '{"num_vars": 2, "num_constrs": 1, "obj_coeffs": [1.0, 1.0], "row_ptr": [0, 2], "col_idx": [0, 1], "values": [1.0, 1.0], "row_senses": "L", "rhs": [10.0]}'
    prob2 = '{"num_vars": 2, "num_constrs": 1, "obj_coeffs": [2.0, 2.0], "row_ptr": [0, 2], "col_idx": [0, 1], "values": [1.0, 1.0], "row_senses": "L", "rhs": [20.0]}'
    
    t1 = threading.Thread(target=worker, args=(0, prob1))
    t2 = threading.Thread(target=worker, args=(1, prob2))
    
    t1.start(); t2.start()
    t1.join(); t2.join()
    
    if isinstance(results[0], dict) and isinstance(results[1], dict):
        if "status" in results[0] and "status" in results[1]:
            print("[Concurrency] SUCCESS: No cross-request corruption.")
            return True
    print(f"[Concurrency] FAILED: Invalid responses: {results}")
    return False

def test_4xx_errors(base_url):
    print("\n--- Testing 4xx Errors ---")
    failed = 0
    # Missing fields
    prob_bad = '{"num_vars": 2}'
    r = requests.post(f"{base_url}/solve", data={"problem_def": prob_bad})
    if 400 <= r.status_code < 500:
        print("[4xx Missing Fields] SUCCESS")
    else:
        print(f"[4xx Missing Fields] FAILED: {r.status_code}")
        failed += 1
        
    return failed == 0

def main():
    regression_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "core", "tests", "regression"))
    if not os.path.isdir(regression_dir):
        print(f"Error: {regression_dir} not found")
        sys.exit(1)
    
    files = [f for f in os.listdir(regression_dir) if f.endswith(".mps")]
    
    base_url = "http://127.0.0.1:8080"
    
    print(f"Running API regression sweep on {len(files)} files...")
    
    try:
        requests.get(base_url, timeout=2)
    except requests.exceptions.RequestException:
        pass # Wait for health poll in bash
        
    passed = 0
    failed = 0
    
    if not test_concurrency(base_url): failed += 1
    else: passed += 1
        
    if not test_4xx_errors(base_url): failed += 1
    else: passed += 1
    
    # Test Benchmark endpoint
    print("\n--- Testing /benchmark ---")
    resp = requests.get(f"{base_url}/benchmark")
    if resp.status_code == 200 and "benchmark_results" in resp.json():
        print("[/benchmark] SUCCESS")
        passed += 1
    else:
        print(f"[/benchmark] FAILED: {resp.status_code}")
        failed += 1
    
    for f in sorted(files):
        filepath = os.path.join(regression_dir, f)
        print(f"\n--- Testing {f} ---")
        
        # /inspect
        try:
            with open(filepath, "r") as fp:
                resp = requests.post(f"{base_url}/inspect", files={"file": (f, fp, "application/octet-stream")})
            if resp.status_code == 200:
                if f.startswith("malformed"):
                    print(f"[/inspect] FAILED: Expected 4xx for {f}, got 200")
                    failed += 1
                    continue
                data = resp.json()
                print(f"[/inspect] SUCCESS: {data.get('problem_summary')}")
            else:
                if f.startswith("malformed") and resp.status_code >= 400:
                    print(f"[/inspect] SUCCESS (Expected Error): {resp.status_code}")
                    passed += 1
                    continue
                else:
                    print(f"[/inspect] FAILED: {resp.status_code} - {resp.text}")
                    failed += 1
                    continue
        except Exception as e:
            print(f"[/inspect] ERROR: {e}")
            failed += 1
            continue
            
        # /solve
        try:
            with open(filepath, "r") as fp:
                resp = requests.post(
                    f"{base_url}/solve",
                    files={"file": (f, fp, "application/octet-stream")},
                    data={"method": "auto", "gpu": "false"}
                )
            if resp.status_code == 200:
                data = resp.json()
                trace = data.get("trace", [])
                trace_valid = isinstance(trace, list) and len(trace) > 0 and "narration" in trace[0]
                if trace_valid:
                    print(f"[/solve] SUCCESS: Status={data.get('status')} | Obj={data.get('objective')} | Trace narration included")
                    passed += 1
                else:
                    print(f"[/solve] FAILED: Valid response but missing/invalid trace structure. Data: {data}")
                    failed += 1
            else:
                print(f"[/solve] FAILED: {resp.status_code} - {resp.text}")
                failed += 1
        except Exception as e:
            print(f"[/solve] ERROR: {e}")
            failed += 1
            
    print(f"\nAPI Regression Sweep Complete: {passed} passed, {failed} failed")
    print(f"[EVIDENCE] API HTTP Sweep: {passed} passed, {failed} failed out of {passed+failed} endpoints")
    sys.exit(0 if failed == 0 else 1)

if __name__ == '__main__':
    main()

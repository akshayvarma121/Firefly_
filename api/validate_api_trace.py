import asyncio
import os
import sys
from httpx import AsyncClient, ASGITransport

from main import app, FIREFLY_SOLVER_AVAILABLE
import main

# MPS file contents
LP_MPS = """NAME          TEST_LP
ROWS
 N  OBJ
 L  R1
 L  R2
COLUMNS
    X1        OBJ       -1.0
    X1        R1        1.0
    X1        R2        2.0
    X2        OBJ       -2.0
    X2        R1        2.0
    X2        R2        1.0
RHS
    RHS1      R1        5.0
    RHS1      R2        4.0
BOUNDS
 LO BND       X1        0.0
 LO BND       X2        0.0
ENDATA
"""

MILP_MPS = """NAME          TEST_MILP
ROWS
 N  OBJ
 L  R1
COLUMNS
    MARKER    'MARKER'                 'INTORG'
    X1        OBJ       -1.0
    X1        R1        1.5
    X2        OBJ       -2.0
    X2        R1        2.5
    MARKER    'MARKER'                 'INTEND'
RHS
    RHS1      R1        4.0
BOUNDS
 LO BND       X1        0.0
 LO BND       X2        0.0
ENDATA
"""

INFEAS_MPS = """NAME          TEST_INFEAS
ROWS
 N  OBJ
 G  R1
 L  R2
COLUMNS
    X1        OBJ       1.0
    X1        R1        1.0
    X1        R2        1.0
RHS
    RHS1      R1        5.0
    RHS1      R2        2.0
BOUNDS
 LO BND       X1        0.0
ENDATA
"""

UNBOUNDED_MPS = """NAME          TEST_UNBND
ROWS
 N  OBJ
 G  R1
COLUMNS
    X1        OBJ       -1.0
    X1        R1        1.0
RHS
    RHS1      R1        5.0
BOUNDS
 LO BND       X1        0.0
ENDATA
"""

async def run_tests():
    files = {
        "lp.mps": LP_MPS,
        "milp.mps": MILP_MPS,
        "infeas.mps": INFEAS_MPS,
        "unbnd.mps": UNBOUNDED_MPS
    }
    
    for fname, content in files.items():
        with open(fname, "w") as f:
            f.write(content)

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        for fname in files.keys():
            print(f"\n--- Testing {fname} ---")
            
            # Inspect
            with open(fname, "rb") as f:
                res_inspect = await client.post("/inspect", files={"file": (fname, f)})
            insp_data = res_inspect.json()
            
            # Solve
            with open(fname, "rb") as f:
                res_solve = await client.post("/solve", files={"file": (fname, f)})
            sol_data = res_solve.json()
            
            trace = sol_data["trace"]
            stages = [s["stage"] for s in trace]
            print(f"Stages: {stages}")
            
            if "milp" in fname:
                assert stages == ["parse", "presolve", "lp_core", "milp", "output"]
            else:
                assert stages == ["parse", "presolve", "lp_core", "output"]
                
            parse_stage = trace[0]
            print(f"Inspect summary: {insp_data['problem_summary']}")
            print(f"Trace parse summary: {parse_stage['narration']}")
            assert insp_data["problem_summary"] == parse_stage["narration"]
            assert insp_data["vars"] == parse_stage["vars"]
            assert insp_data["constrs"] == parse_stage["constrs"]
            
            output_stage = trace[-1]
            print(f"Solve status: {sol_data['status']}")
            print(f"Output stage narration: {output_stage['narration']}")

        print("\n--- Testing MOCK Fallback ---")
        # Break firefly_solver artificially inside main.py
        original_solver = main.firefly_solver
        main.firefly_solver = None
        main.FIREFLY_SOLVER_AVAILABLE = False
        
        try:
            with open("lp.mps", "rb") as f:
                res_mock = await client.post("/solve", files={"file": ("lp.mps", f)})
            mock_data = res_mock.json()
            print(f"Mock status code: {res_mock.status_code}")
            print(f"Mock trace: {mock_data.get('trace')}")
            assert mock_data["mock"] is True
            assert mock_data["trace"][0]["narration"].startswith("[MOCK]")
        finally:
            main.firefly_solver = original_solver
            main.FIREFLY_SOLVER_AVAILABLE = True

        print("\n--- Testing Restore ---")
        with open("lp.mps", "rb") as f:
            res_restore = await client.post("/solve", files={"file": ("lp.mps", f)})
        restore_data = res_restore.json()
        assert restore_data.get("mock", False) is False
        print("Restored real mode successfully.")

if __name__ == "__main__":
    asyncio.run(run_tests())

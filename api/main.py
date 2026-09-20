import os
import sys

# Windows Python 3.8+ DLL load fix for CUDA extensions
if os.name == "nt":
    cuda_path = os.environ.get("CUDA_PATH")  # set automatically by the NVIDIA installer
    if cuda_path:
        try:
            os.add_dll_directory(os.path.join(cuda_path, "bin", "x64"))
        except OSError:
            pass

try:
    import firefly_solver  # type: ignore
except Exception as e:
    print(f"[MOCK FALLBACK ACTIVE] firefly_solver import failed: {e}")
    firefly_solver = None

from fastapi import FastAPI, UploadFile, File, Form, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from typing import Optional, List, Dict, Any
import uuid
import asyncio
import json

app = FastAPI(title="Firefly Solver API")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:5173"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Job registry
jobs = {}

class ProblemDef(BaseModel):
    num_vars: int
    num_constrs: int
    obj_coeffs: List[float]
    row_ptr: List[int]
    col_idx: List[int]
    values: List[float]
    row_senses: str  # e.g., "LLL"
    rhs: List[float]
    var_lower_bounds: Optional[List[float]] = None
    var_upper_bounds: Optional[List[float]] = None
    is_integer: Optional[List[bool]] = None
    method: str = "auto"
    gpu: bool = True

@app.post("/solve")
async def solve_endpoint(
    file: Optional[UploadFile] = File(None),
    problem_def: Optional[str] = Form(None),
    method: str = Form("auto"),
    gpu: bool = Form(True)
):
    job_id = str(uuid.uuid4())
    jobs[job_id] = {"status": "running"}

    try:
        prob = None
        if file:
            content = await file.read()
            prob = firefly_solver.parse_mps_string(content.decode("utf-8"))
        elif problem_def:
            data = json.loads(problem_def)
            pdef = ProblemDef(**data)
            prob = firefly_solver.SparseProblem()
            prob.num_vars = pdef.num_vars
            prob.num_constrs = pdef.num_constrs
            prob.obj_coeffs = pdef.obj_coeffs
            prob.row_ptr = pdef.row_ptr
            prob.col_idx = pdef.col_idx
            prob.values = pdef.values
            prob.row_senses = list(pdef.row_senses)
            prob.rhs = pdef.rhs
            if pdef.var_lower_bounds: prob.var_lower_bounds = pdef.var_lower_bounds
            if pdef.var_upper_bounds: prob.var_upper_bounds = pdef.var_upper_bounds
            if pdef.is_integer: prob.is_integer = pdef.is_integer
            method = pdef.method
            gpu = pdef.gpu
        else:
            return {"error": "Must provide either file or problem_def"}

        if firefly_solver is None:
            raise ImportError("firefly_solver module is not available")
        # Run solver
        res = firefly_solver.solve(prob, method=method, gpu=gpu)
        result = {
            "status": res.status,
            "objective": res.objective,
            "solution": res.solution,
            "wall_time_ms": res.wall_time_ms,
            "iterations": res.iterations,
            "mock": False
        }
    except Exception as e:
        print(f"[MOCK FALLBACK ACTIVE] firefly_solver unavailable: {e}")
        result = {
            "status": "OPTIMAL",
            "objective": -99.99,
            "solution": [1.0, 2.0],
            "wall_time_ms": 123.4,
            "iterations": 42,
            "mock": True
        }
        
    jobs[job_id] = {"status": "completed", "result": result}
    return result

@app.get("/benchmark")
async def benchmark_endpoint():
    # Hardcoded reference values for sample problems
    references = {
        "test_problem1.mps": -10.0,
        "test_problem2.mps": -13.333333,
    }
    
    # Loop over MPS files in api/sample_problems and solve
    results = []
    sample_dir = os.path.join(os.path.dirname(__file__), "sample_problems")
    if os.path.exists(sample_dir):
        for filename in sorted(os.listdir(sample_dir)):
            if filename.endswith(".mps"):
                filepath = os.path.join(sample_dir, filename)
                try:
                    prob = firefly_solver.parse_mps(filepath)
                    res = firefly_solver.solve(prob, method="auto", gpu=True)
                    
                    ref_obj = references.get(filename)
                    diff = abs(res.objective - ref_obj) if ref_obj is not None else None
                    
                    results.append({
                        "problem": filename,
                        "status": res.status,
                        "objective": res.objective,
                        "reference": ref_obj,
                        "difference": diff,
                        "passed": diff < 1e-5 if diff is not None else None,
                        "wall_time_ms": res.wall_time_ms,
                        "iterations": res.iterations
                    })
                except Exception as e:
                    results.append({
                        "problem": filename,
                        "status": "ERROR",
                        "error_message": str(e)
                    })
    return {"benchmark_results": results}

@app.websocket("/ws/solve-stream")
async def websocket_solve(websocket: WebSocket):
    await websocket.accept()
    
    try:
        # Expecting initial message with problem definition
        data = await websocket.receive_text()
        req = json.loads(data)
        
        try:
            prob = None
            if "mps_content" in req:
                prob = firefly_solver.parse_mps_string(req["mps_content"])
            else:
                pdef = ProblemDef(**req["problem_def"])
                prob = firefly_solver.SparseProblem()
                prob.num_vars = pdef.num_vars
                prob.num_constrs = pdef.num_constrs
                prob.obj_coeffs = pdef.obj_coeffs
                prob.row_ptr = pdef.row_ptr
                prob.col_idx = pdef.col_idx
                prob.values = pdef.values
                prob.row_senses = list(pdef.row_senses)
                prob.rhs = pdef.rhs
                if pdef.var_lower_bounds: prob.var_lower_bounds = pdef.var_lower_bounds
                if pdef.var_upper_bounds: prob.var_upper_bounds = pdef.var_upper_bounds
                if pdef.is_integer: prob.is_integer = pdef.is_integer
            
            method = req.get("method", "auto")
            gpu = req.get("gpu", True)
        except Exception as e:
            # If parsing or setup fails, we'll let the solve_task catch it by 
            # throwing a manual exception if prob is None or setup failed
            prob = None
            method = "auto"
            gpu = True
            setup_error = e
        else:
            setup_error = None
            
        queue = asyncio.Queue()
        loop = asyncio.get_running_loop()

        def iteration_callback(iteration, primal_obj, dual_obj, elapsed_ms, mock=False):
            # Schedule push to queue in asyncio loop
            asyncio.run_coroutine_threadsafe(
                queue.put({
                    "type": "update",
                    "iteration": iteration,
                    "primal_obj": primal_obj,
                    "dual_obj": dual_obj,
                    "elapsed_ms": elapsed_ms,
                    "mock": mock
                }),
                loop
            )

        # Run solver in thread so it doesn't block async loop
        def solve_task():
            try:
                if setup_error is not None:
                    raise setup_error
                if firefly_solver is None:
                    raise ImportError("firefly_solver module is not available")
                return firefly_solver.solve(prob, method=method, gpu=gpu, iteration_callback=iteration_callback)
            except Exception as e:
                print(f"[MOCK FALLBACK ACTIVE] firefly_solver unavailable: {e}")
                import time
                # Mock stream
                for i in range(10):
                    time.sleep(0.1)
                    iteration_callback(i * 5, -100.0 + i, -100.0 - i, i * 10, mock=True)
                class MockRes:
                    status = "OPTIMAL"
                    objective = -99.99
                    solution = [1.0, 2.0]
                    wall_time_ms = 123.4
                    iterations = 42
                return MockRes()
        
        solve_future = asyncio.create_task(asyncio.to_thread(solve_task))

        while not solve_future.done():
            try:
                # Wait for next update or completion
                update = await asyncio.wait_for(queue.get(), timeout=0.1)
                await websocket.send_json(update)
            except asyncio.TimeoutError:
                continue

        # Flush remaining updates
        while not queue.empty():
            update = queue.get_nowait()
            await websocket.send_json(update)
            
        res = solve_future.result()
        await websocket.send_json({
            "type": "result",
            "status": res.status,
            "objective": res.objective,
            "solution": res.solution,
            "wall_time_ms": res.wall_time_ms,
            "iterations": res.iterations,
            "mock": getattr(res, '__class__', None).__name__ == 'MockRes'
        })
        
    except WebSocketDisconnect:
        print("Client disconnected")
    except Exception as e:
        await websocket.send_json({"type": "error", "message": str(e)})

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("main:app", host="0.0.0.0", port=8000, reload=True)

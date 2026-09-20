# Firefly Solver

An original, GPU-accelerated Linear (LP), Mixed-Integer Linear (MILP), and Quadratic Programming (QP) solver built entirely from mathematical first principles. Designed for high-performance scientific computing and strict algorithmic verification, Firefly leverages modern C++20 and CUDA to achieve massively parallel continuous relaxations.

Developed for SIH 2026 Problem Statement 26119.

---

## Architecture Overview

Firefly operates in a decoupled, three-tier architecture:

1. **Core (C++20 & CUDA)**
   The mathematical foundation. Contains all original implementations of algorithmic primitives without external solver dependencies. 
   - **Presolve Engine**: Matrix equilibration (Ruiz), scaling invariance, empty row/column elimination, and advanced singleton-row bound tightening capable of identifying trivially infeasible configurations analytically.
   - **LP Solvers**: 
     - **PDLP (Primal-Dual Hybrid Gradient)**: A GPU-accelerated, first-order method exploiting cuBLAS and cuSPARSE to evaluate ultra-large constraint matrices efficiently on NVIDIA architectures.
     - **Simplex**: A CPU-bound fallback and cross-validation solver for small-scale precision and edge-case verification.
   - **MILP Solver**: A rigorous Branch and Bound engine supporting depth-first and best-first node traversal strategies, dynamic upper/lower bound pruning, and configurable fractionality divergence logic.

2. **API (Python FastAPI)**
   A high-throughput translation layer bridging external programmatic requests to the C++ core via `pybind11` extensions. Facilitates distributed task queueing and serialization of MPS-format constraint files.

3. **Web (React & TypeScript)**
   A telemetry and control panel interface engineered with a scientific instrumentation aesthetic. Visualizes convergence rates, real-time duality gaps, parallel lane utilization, and solver node expansion in an oscilloscope-style UI.

---

## Technical Specifications

- **Target Compute Capability**: CUDA `sm_89` (NVIDIA RTX 40-series architecture and above)
- **Language Standards**: C++20, Python 3.10+, TypeScript 5.0+
- **Build System**: CMake 3.25+, MSBuild / Make
- **Strict Dependencies**: 
  - Standard mathematical primitives (Eigen 3.4)
  - NVIDIA Toolkit (cuBLAS, cuSPARSE)
  - *No commercial or open-source solver binaries (e.g., Gurobi, CPLEX, GLPK) are linked.*

---

## Build and Installation

### Prerequisites
- CMake 3.25 or higher
- NVIDIA CUDA Toolkit 12.0 or higher
- A compatible C++20 compiler (MSVC, GCC, Clang)

---

### Windows Setup

> **This section is not optional.** Python 3.8+ on Windows stopped honouring
> the `PATH` environment variable for DLL dependencies of compiled extension
> modules (`.pyd` files). `firefly_solver.pyd` links against
> `cusparse64_12.dll` and `cublas64_13.dll` from the CUDA Toolkit. Without
> the steps below the import will raise a misleading `ImportError: DLL load
> failed … The specified module could not be found` even when the DLLs are
> physically present.
>
> Background: [Python 3.8 changelog — bpo-36085](https://docs.python.org/3/whatsnew/3.8.html#bpo-36085-whatsnew)

**Step 1 — Verify `CUDA_PATH` is set**

The NVIDIA Toolkit installer sets this automatically. Confirm in PowerShell:

```powershell
$env:CUDA_PATH
# Expected: C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v<version>
```

If it is empty, set it in System Environment Variables and reopen any
terminal or IDE before continuing.

**Step 2 — Confirm the DLLs exist in `bin\x64\`**

```powershell
Get-Item "$env:CUDA_PATH\bin\x64\cusparse64_12.dll"
Get-Item "$env:CUDA_PATH\bin\x64\cublas64_13.dll"
```

> **Important:** The DLLs live in `bin\x64\`, not `bin\`. The API server
> calls `os.add_dll_directory(os.path.join(CUDA_PATH, "bin", "x64"))`
> before importing `firefly_solver`. Any other script that imports the
> module directly must do the same, before the import statement:
>
> ```python
> import os, sys
> if os.name == "nt":
>     cuda_path = os.environ.get("CUDA_PATH")
>     if cuda_path:
>         os.add_dll_directory(os.path.join(cuda_path, "bin", "x64"))
> import firefly_solver   # safe to import after the line above
> ```

**Step 3 — Install the Python bindings**

```powershell
# From the repo root — uses scikit-build-core + CMake + MSVC
pip install -e core --no-build-isolation
```

This compiles `firefly_core` (C++20 static library), fetches Eigen via
CMake FetchContent, and links `firefly_solver.pyd`. A full first build
takes 2–5 minutes; incremental rebuilds are fast.

**Step 4 — Smoke-test the import**

```powershell
python -c "
import os
os.add_dll_directory(os.path.join(os.environ['CUDA_PATH'], 'bin', 'x64'))
import firefly_solver
p = firefly_solver.SparseProblem()
p.num_vars = 2; p.num_constrs = 1
p.obj_coeffs = [-1.0, -2.0]; p.row_ptr = [0,2]; p.col_idx = [0,1]
p.values = [1.0,1.0]; p.row_senses = ['L']; p.rhs = [4.0]
p.var_lower_bounds = [0.0,0.0]; p.var_upper_bounds = [1e30,1e30]
p.is_integer = [False,False]
res = firefly_solver.solve(p, method='simplex', gpu=False)
assert res.status == 'OPTIMAL' and abs(res.objective + 8.0) < 1e-4
print('OK — firefly_solver is working correctly')
"
```

---

### Compiling the Core

```bash
# Clone the repository
git clone https://github.com/akshayvarma121/Firefly_.git
cd Firefly_

# Configure CMake
mkdir core/build && cd core/build
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build the project
cmake --build . --config Release
```

### Running the Test Suite

Firefly enforces strict algorithmic correctness via comprehensive unit testing against established MIPLIB and Netlib reference datasets.

```bash
cd core/build
ctest -C Release --output-on-failure
```

---

## Performance Considerations

Firefly's PDLP implementation heavily favors matrix-vector multiplications over factorization, trading traditional exactness for massive scalability on dense, unstructured models. The solver demonstrates near-linear parallel scaling across CUDA thread blocks but is highly sensitive to the conditioning of the input matrix. 

Users are strictly advised to enable the Presolve engine's `Ruiz Equilibration` pass prior to invoking PDLP to guarantee convergence stability.

## Current State: What's Built vs. Planned

### What's Built (Backend & Core)
- **Core Solver Engine (C++20 & CUDA)**: Original PDLP, Simplex, and MILP Branch & Bound algorithms implemented from mathematical first principles with custom matrix equilibration (Ruiz) and scaling invariance.
- **Python Extension (`pybind11`)**: Direct, efficient bridging between the Python runtime and the C++ execution environment, rigorously tested against Windows DLL linking quirks.
- **CLI (`firefly`)**: Command-line tool supporting single problem solving and directory benchmarking.
- **HTTP API (FastAPI)**: Robust `/inspect` and `/solve` endpoints supporting MPS parsing, detailed narration generation (plain-English traces of solve stages), strict malformed input handling, and thread-safe concurrent solves.
- **Testing Infrastructure**: Comprehensive sweep tests at the C++, CLI, and HTTP levels against regression MPS files ensuring exact state mappings and zero fabricated fallbacks.

### What's Planned (Frontend)
- **React & TypeScript Dashboard**: A telemetry web interface built with Vite, Tailwind, shadcn/ui, and Recharts.
- **Scientific Aesthetic**: Strict visual language mirroring refinery control panels/oscilloscopes with flat surfaces, 1px borders, and IBM Plex fonts.
- **Real-Time Visualizations**: Dual-objective line charts, solver status motifs (amber pulsing lights, async parallel GPU lane indicators locking into shared rhythm on convergence).

---

## Authors

**The Fireflies** 
*An SIH 2026 Initiative*

---

## Command-Line Interface (CLI)

Installing the Python bindings via `pip install -e core` automatically registers the `firefly` command in your environment.

```bash
# Solve a single MPS file
firefly solve path/to/problem.mps

# Solve with specific options
firefly solve problem.mps --method pdlp --no-gpu --verbose --output solution.csv

# Batch solve all .mps files in a directory
firefly benchmark ./problems/
```

---

## Starting the API Server

The FastAPI translation layer requires additional Python dependencies.

```bash
# 1. Install the API requirements
pip install -r api/requirements.txt

# 2. Start the FastAPI server using Uvicorn
uvicorn api.main:app --host 0.0.0.0 --port 8000
```

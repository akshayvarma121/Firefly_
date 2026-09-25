# Firefly Solver

An original LP/MILP/QP optimization solver core built from mathematical first principles, GPU-accelerated via CUDA, for SIH 2026 PS 26119.

[View the Firefly Landing Page Repository](https://github.com/akshayvarma121/firefly_solver_landing_page)

---

## Architecture

Firefly operates in a decoupled, three-tier architecture with a strict linear solve pipeline:

```mermaid
flowchart LR
    A[Parser] --> B[Presolve]
    B --> C[LP Core]
    C --> D[MILP Engine]
    D --> E[Output]

    classDef stage fill:#1B1D18,stroke:#33362E,color:#E8E6DE
    class A,B,C,D,E stage
```

1. **Parser**: Reads MPS/LP file format. Validates structure, extracts objective coefficients, constraint matrix (CSR), RHS, and variable bounds.
2. **Presolve**: Eliminates redundant rows and fixed variables, tightens bounds, and rescales the constraint matrix before dispatch.
3. **LP Core**: GPU-native PDLP (primal–dual first-order) for large-scale continuous relaxations; CPU Simplex fallback for small dense instances.
4. **MILP Engine**: Branch-and-bound over the LP core. Integer feasibility enforced per node; best-bound pruning limits the search tree.
5. **Output**: Primal solution vector, objective value, status code, iteration count, and wall-clock time returned to API and UI.

---

## Installation

### Quick Install (Windows)
To install the standalone CLI and add it to your right-click context menu, run this one-liner in **PowerShell**:

```powershell
irm https://raw.githubusercontent.com/akshayvarma121/Firefly_solver/main/install.ps1 | iex
```

---

## Build from Source

### Core (C++20 & CUDA)
Requires CMake 3.25+ and NVIDIA CUDA Toolkit 12.0+ (target sm_89).

```bash
mkdir core/build && cd core/build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

To run the test suite:
```bash
cd core/build
ctest -C Release --output-on-failure
```

### API (Python FastAPI)
A translation layer bridging programmatic requests to the C++ core via `pybind11`. Requires Python 3.10+.

```bash
# Compile and install the core Python bindings
pip install -e core --no-build-isolation

# Install API requirements
pip install -r api/requirements.txt

# Start the FastAPI server
uvicorn api.main:app --host 0.0.0.0 --port 8000
```

*Note on Windows*: Ensure `CUDA_PATH` is set and its `bin\x64` directory is accessible for DLL linking.

### Web (React & TypeScript)
A telemetry interface engineered with a scientific instrumentation aesthetic.

```bash
cd web
npm install
npm run dev
```

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

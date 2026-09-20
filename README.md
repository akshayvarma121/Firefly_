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

---

## Authors

**The Fireflies** 
*An SIH 2026 Initiative*

# Firefly

Firefly — an indigenous GPU-accelerated optimization solver, built by The Fireflies for SIH 2026.

## Project Structure

- **core/**: C++20 + CUDA, target compute capability sm_89 (RTX 4050). The mathematical solver core.
- **api/**: Python FastAPI server + pybind11 bindings over `core/`.
- **web/**: React + Vite + TypeScript frontend.

## Windows setup

If you are developing on Windows with Python 3.8 or later, you might encounter an `ImportError: DLL load failed` when attempting to import the `firefly_solver` Python module.

This occurs because Python 3.8+ intentionally ignores the system `%PATH%` environment variable when resolving DLL dependencies for C extension modules (such as our pybind11 module linking to cuBLAS and cuSPARSE).

To resolve this, ensure the CUDA Toolkit is installed. The NVIDIA installer automatically sets the `CUDA_PATH` environment variable. Our API entrypoint automatically detects this and adds the CUDA `bin/x64` directory to Python's trusted DLL search paths using `os.add_dll_directory()`.

import os
import sys

# ---------------------------------------------------------------------------
# Windows DLL search path — MUST run before any import of the native extension.
# ---------------------------------------------------------------------------
if os.name == "nt":
    cuda_path = os.environ.get("CUDA_PATH")
    if cuda_path:
        _cuda_bin = os.path.join(cuda_path, "bin", "x64")
        if not os.path.isdir(_cuda_bin):
            _cuda_bin = os.path.join(cuda_path, "bin") # fallback
        if os.path.isdir(_cuda_bin):
            os.add_dll_directory(_cuda_bin)
        else:
            print(f"[WARNING] CUDA_PATH set but {_cuda_bin!r} does not exist.", file=sys.stderr)
    else:
        print("[WARNING] CUDA_PATH environment variable is not set on Windows.", file=sys.stderr)

# Import all symbols from the internal compiled extension
try:
    from ._firefly_solver import *
except ImportError:
    from _firefly_solver import *

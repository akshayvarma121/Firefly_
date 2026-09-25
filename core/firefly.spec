# -*- mode: python ; coding: utf-8 -*-

import os
import glob
import sys
import sysconfig

block_cipher = None

# Identify CUDA path for DLL bundling (Windows/Linux only)
binaries = []
if sys.platform == "win32":
    cuda_path = os.environ.get("CUDA_PATH", r"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2")
    if os.path.exists(cuda_path):
        for sub_dir in ["bin", os.path.join("bin", "x64")]:
            cublas = glob.glob(os.path.join(cuda_path, sub_dir, "cublas64_*.dll"))
            cusparse = glob.glob(os.path.join(cuda_path, sub_dir, "cusparse64_*.dll"))
            for f in cublas + cusparse:
                binaries.append((f, '.'))

# Explicitly add the compiled extension to binaries
site_pkgs = sysconfig.get_path("purelib")
pyd_ext = sysconfig.get_config_var('EXT_SUFFIX')
pyd_path = os.path.join(site_pkgs, "firefly_solver", f"_firefly_solver{pyd_ext}")

if os.path.exists(pyd_path):
    # Put it in the firefly_solver package folder inside the bundled app
    binaries.append((pyd_path, 'firefly_solver'))
else:
    print(f"WARNING: Native extension not found at {pyd_path}")

a = Analysis(
    ['cli/firefly.py'],
    pathex=[os.path.abspath('.')],
    binaries=binaries,
    datas=[],
    hiddenimports=['firefly_solver', 'cli.bench'],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)
pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name='firefly',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)

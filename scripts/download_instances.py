import os
import urllib.request

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT_DIR, "core", "tests", "netlib_miplib")
os.makedirs(OUT_DIR, exist_ok=True)

# LPs from Netlib (present in HiGHS repo)
lps = [
    "afiro.mps",
    "adlittle.mps",
    "israel.mps",
    "greenbea.mps",
    "woodinfe.mps"
]

# MILPs from MIPLIB (present in HiGHS repo)
mips = [
    "p0548.mps",
    "flugpl.mps",
    "egout.mps"
]

files = lps + mips

for name in files:
    url = f"https://raw.githubusercontent.com/ERGO-Code/HiGHS/master/check/instances/{name}"
    out = os.path.join(OUT_DIR, name)
    print(f"Downloading {name}...")
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req) as response:
            with open(out, 'wb') as f:
                f.write(response.read())
    except Exception as e:
        print(f"Failed {name}: {e}")

print("Done.")

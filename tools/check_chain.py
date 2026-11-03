#!/usr/bin/env python3
r"""
Safety check of the generation chain (run after build_elf.ps1 and --dump-context). A wrong ROM offset is SILENT:
N64Recomp then recompiles the wrong bytes without any error (seen 2026-10-19: Shift-JIS strings re-encoded in UTF-8
made file 6 0x5E0 bytes bigger and shifted every later section).

1. build/ga.elf, dumped with llvm-objcopy -O binary, must equal baserom.us.decompressed.z64 (lld adds a few bytes at the end).
2. Every [[section]] rom of syms/ga.syms.toml must equal the start of the same segment in config/splat.yaml.
Usage: .\.venv\Scripts\python.exe tools\check_chain.py   (needs pyyaml from the venv). Exit code 1 on failure.
"""
import pathlib
import re
import subprocess
import sys

import yaml

ROOT = pathlib.Path(__file__).resolve().parent.parent
OBJCOPY = r"C:\Program Files\LLVM\bin\llvm-objcopy.exe"


def main():
    ok = True
    out = ROOT / "build" / "ga_check.bin"
    subprocess.run([OBJCOPY, "-O", "binary", str(ROOT / "build" / "ga.elf"), str(out)], check=True)
    elf, rom = out.read_bytes(), (ROOT / "baserom.us.decompressed.z64").read_bytes()
    diff = next((i for i in range(len(rom)) if i >= len(elf) or elf[i] != rom[i]), None)
    if diff is None:
        print(f"OK  ELF == decompressed ROM ({len(rom):#x} bytes, ELF +{len(elf) - len(rom)})")
    else:
        ok = False
        print(f"ERR ELF differs from the ROM at {diff:#x}")

    y = yaml.safe_load((ROOT / "config" / "splat.yaml").read_text(encoding="utf-8"))
    real = {"." + s["name"]: s["start"] for s in y["segments"] if isinstance(s, dict) and s.get("type") == "code"}
    syms = (ROOT / "syms" / "ga.syms.toml").read_text(encoding="utf-8")
    secs = {n: int(r, 16) for n, r in re.findall(r'name = "([^"]+)"\nrom = (0x[0-9A-Fa-f]+)', syms)}
    bad = [n for n in real if secs.get(n) != real[n]]
    if bad:
        ok = False
        print(f"ERR {len(bad)} sections with a wrong rom in syms/ga.syms.toml, first: {bad[0]} "
              f"(splat {real[bad[0]]:#x}, syms {secs.get(bad[0], 0):#x})")
    else:
        print(f"OK  {len(real)} section rom offsets")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

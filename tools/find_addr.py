#!/usr/bin/env python3
"""
Which function contains a game address? Searches syms/ga.syms.toml (all sections: main, file 6, overlays, the 311 modules).
Several modules share 0x08000000, so one address can be in several modules: the section name .file_<n> is the GAME file
number (the one printed by "[GGA MAP] file <n>" in the log).

Usage:  python tools\find_addr.py 0x08006098            (all sections)
        python tools\find_addr.py 0x08006098 309        (only .file_309)
Prints "START" when the address is exactly the start of a known function, "INSIDE" when it falls inside one
(splat cut the code differently: the game calls a function N64Recomp does not know), "NO FUNCTION" otherwise.
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from match_libultra import parse_syms  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    addr = int(sys.argv[1], 16)
    only = f".file_{sys.argv[2]}" if len(sys.argv) > 2 else None
    for sec, fns in parse_syms(ROOT / "syms" / "ga.syms.toml"):
        name = sec.get("name")
        if only and name != only:
            continue
        if not (sec.get("vram", 0) <= addr < sec.get("vram", 0) + sec.get("size", 0)):
            continue
        hit = next(((n, v, s) for n, v, s in fns if v <= addr < v + s), None)
        rom = sec["rom"] + addr - sec["vram"]
        if hit is None:
            print(f"{name}: NO FUNCTION at 0x{addr:08X} (ROM 0x{rom:X}: data, or code splat did not see)")
        elif hit[1] == addr:
            print(f"{name}: START of {hit[0]} (size 0x{hit[2]:X})")
        else:
            print(f"{name}: INSIDE {hit[0]} (+0x{addr - hit[1]:X} of 0x{hit[2]:X}), ROM 0x{rom:X}")


if __name__ == "__main__":
    main()

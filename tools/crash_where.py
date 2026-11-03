#!/usr/bin/env python3
r"""
Where did GGARecompiled.exe crash? Converts a "Fault offset" / "exe+0x..." value into the function name, with the link
map build-vs\GGARecompiled.map (written by the /MAP option of CMakeLists.txt). The map must come from the SAME build
as the crash (rebuild = new addresses).

Usage:  python tools\crash_where.py 0xA5E0FC
        python tools\crash_where.py --log build\spawn_log.txt   (call stacks of a trace: lines "<frame> fn <X>: <offsets>",
        "MARK <frame>"; prints each distinct stack once, with its count and first frame)
Find the offset: gga_crash.txt (line "at exe+0x..."), or the Windows event log:
  Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'} -MaxEvents 3 | Format-List Message
(if the faulting module is ucrtbase.dll and the code 0xc0000409, it is an abort: read build\run_log.txt instead).
"""
import bisect
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASE = 0x140000000  # preferred load address in the map


def load_map():
    syms = []
    for line in (ROOT / "build-vs" / "GGARecompiled.map").read_text(encoding="latin-1").splitlines():
        m = re.match(r"\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\s+(.*)", line)
        if m:
            syms.append((int(m.group(2), 16), m.group(1), m.group(3).strip()))
    syms.sort()
    return syms


def name(syms, offset):
    target = BASE + offset
    i = bisect.bisect_right(syms, (target, "￿")) - 1
    return None if i < 0 else (syms[i][1], target - syms[i][0], syms[i][2])


def main():
    syms = load_map()
    if sys.argv[1] == "--log":
        stacks = {}
        for line in pathlib.Path(sys.argv[2]).read_text().splitlines():
            if line.startswith("MARK"):
                print(line)
                continue
            head, _, offs = line.partition(":")
            frame, _, fn = head.split()
            key = (fn, tuple(name(syms, int(o, 16))[0] for o in offs.split()))
            if key not in stacks:
                print(f"frame {frame} NEW stack fn {fn}: " + " < ".join(key[1]))
            stacks[key] = stacks.get(key, 0) + 1
        print("\ncounts:")
        for (fn, chain), n in sorted(stacks.items(), key=lambda kv: -kv[1]):
            print(f"{n:5}  fn {fn}: " + " < ".join(chain))
        return
    best = name(syms, int(sys.argv[1], 16))
    if best is None:
        print("not found (offset outside the executable?)")
    else:
        print(f"{best[0]} +0x{best[1]:X}   [{best[2]}]")


if __name__ == "__main__":
    main()

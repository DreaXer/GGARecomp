#!/usr/bin/env python3
"""
Extract the 4 code files of GGA (see notes/OVERLAYS.md) from the decompressed ROM into build/.
Output: build/code_<rommy index>_<VRAM>.bin and build/code_files.json (index, vram, size).
Needs baserom.us.decompressed.z64 and config/rommy.json (run tools/ga_rommy.py first).
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
# rommy index -> VRAM (game index = rommy index + 1) ; table 0x80026B00 of the game, see notes/OVERLAYS.md
CODE = {5: 0x800C7B10, 7: 0x801738A0, 8: 0x801738A0, 9: 0x801738A0}

rom = (ROOT / "baserom.us.decompressed.z64").read_bytes()
files = json.loads((ROOT / "config" / "rommy.json").read_text())["files"]
(ROOT / "build").mkdir(exist_ok=True)
out = []
for idx, vram in CODE.items():
    f = files[idx]
    assert f["index"] == idx
    data = rom[f["new_offset"] : f["new_offset"] + f["new_size"]]
    name = f"code_{idx}_{vram:08X}.bin"
    (ROOT / "build" / name).write_bytes(data)
    out.append({"rommy_index": idx, "game_index": idx + 1, "vram": f"{vram:#010x}", "size": len(data), "file": name})
    print(f"{name}: {len(data):#x} bytes")
(ROOT / "build" / "code_files.json").write_text(json.dumps(out, indent=1))

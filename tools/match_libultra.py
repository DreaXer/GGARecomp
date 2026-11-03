#!/usr/bin/env python3
"""
Find libultra (and other SDK) functions in GGA's main code by comparing them with the named functions of DK64 and Goemon 1.

References: ../dk64recomp/DK64Syms/dump.toml (names) + ../dk64recomp/donkeykong64.decompressed.us.z64 (bytes);
            ../Goemon64Recomp/Goemon64RecompSyms/mnsg.syms.toml (section .main) + ref/mnsg.us.decompressed.z64
            (ref/mnsg.us.z64 decompressed with ../Goemon64Recomp/lib/mnsg/tools/rommy.py; skipped if missing).
Only SDK-looking names are used (N64Recomp symbol lists or LIB_RE): game functions only match by chance.
Target:    syms/ga.syms.toml (section .main) + baserom.us.decompressed.z64.
Comparison per instruction, with address-dependent fields masked: j/jal target, lui immediate,
immediate of loads/stores/addiu/ori whose base register is not $sp.
Only exact size + masked-bytes matches that are unique on both sides are kept.

Usage: python tools/match_libultra.py            -> prints the report, writes build/libultra_matches.txt
       (copy the lines you trust into config/symbol_addrs.txt, then regenerate: gen_splat, splat, build_elf, --dump-context)
"""
import pathlib
import re
import struct
from collections import defaultdict

ROOT = pathlib.Path(__file__).resolve().parent.parent
DK = ROOT.parent / "dk64recomp"

SECTION_RE = re.compile(r"^\[\[section\]\]")
KV_RE = re.compile(r"^(name|rom|vram|size)\s*=\s*(\S+)")
FUNC_RE = re.compile(r'\{\s*name\s*=\s*"([^"]+)",\s*vram\s*=\s*(0x[0-9A-Fa-f]+),\s*size\s*=\s*(0x[0-9A-Fa-f]+)')
MEM_OPS = {0x09, 0x0D, 0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x28, 0x29, 0x2B, 0x31, 0x35, 0x37, 0x39, 0x3D, 0x3F}


def parse_syms(path):
    """Return list of (section dict, [(name, vram, size)])."""
    out, cur = [], None
    for line in path.read_text(encoding="utf-8").splitlines():
        if SECTION_RE.match(line):
            cur = [{}, []]
            out.append(cur)
        elif cur is not None:
            m = KV_RE.match(line)
            if m and m.group(1) not in cur[0]:
                v = m.group(2).strip('"')
                cur[0][m.group(1)] = int(v, 16) if v.startswith("0x") else v
            for f in FUNC_RE.finditer(line):
                cur[1].append((f.group(1), int(f.group(2), 16), int(f.group(3), 16)))
    return out


def masked(b):
    ws = struct.unpack(f">{len(b) // 4}I", b)
    out = []
    for w in ws:
        op, rs = w >> 26, (w >> 21) & 31
        if op in (2, 3):
            w &= 0xFC000000
        elif op == 0x0F or (op in MEM_OPS and rs != 29):
            w &= 0xFFFF0000
        out.append(w)
    return tuple(out)


def funcs_with_bytes(syms, rom, section_filter):
    res = []
    for sec, fns in syms:
        if not section_filter(sec):
            continue
        for name, vram, size in fns:
            off = sec["rom"] + vram - sec["vram"]
            if size >= 12 and size % 4 == 0:
                res.append((name, vram, masked(rom[off : off + size])))
    return res


LIB_RE = re.compile(r"^(_*os|__ll|__ull|__d_|__f_|_*gu[A-Z]|_*al[A-Z]|n_al|__n_|_*Pfs|bcopy|bzero|bcmp|memcpy|memset|strlen|strchr|"
                    r"sprintf|_Printf|_Litob|_Ldtob|_Genld|sinf|cosf|sqrtf|send_mesg|__rmon|_bnkf|__CSP|__seq|__vs|__init)")
SYMBOL_LISTS = DK / "lib" / "N64ModernRuntime" / "N64Recomp" / "src" / "symbol_lists.cpp"


def references():
    """(label, functions) of each reference game; only SDK-looking names are kept (game functions match by chance)."""
    known = set(re.findall(r'"(\w+)"', SYMBOL_LISTS.read_text(encoding="utf-8")))
    refs = [("DK64", DK / "DK64Syms" / "dump.toml", DK / "donkeykong64.decompressed.us.z64", lambda s: "rom" in s),
            ("Goemon1", ROOT.parent / "Goemon64Recomp" / "Goemon64RecompSyms" / "mnsg.syms.toml",
             ROOT / "ref" / "mnsg.us.decompressed.z64", lambda s: s.get("name") == ".main")]
    out = []
    for label, syms, rom, filt in refs:
        if not rom.exists():
            print(f"(reference {label} skipped: {rom} missing)")
            continue
        fns = funcs_with_bytes(parse_syms(syms), rom.read_bytes(), filt)
        out.append((label, [f for f in fns if f[0] in known or LIB_RE.match(f[0])]))
    return out


def main():
    ga_rom = (ROOT / "baserom.us.decompressed.z64").read_bytes()
    refs = references()
    dk = [f for _, fns in refs for f in fns]  # all reference functions (names may repeat across games)
    ga = funcs_with_bytes(parse_syms(ROOT / "syms" / "ga.syms.toml"), ga_rom, lambda s: s.get("name") == ".main")
    # GGA's own names would hide the matches: compare on the raw func_ names only
    ga_names = {v: n for n, v, _ in ga}

    by_sig_dk, by_sig_ga = defaultdict(set), defaultdict(list)
    for name, _, sig in dk:
        by_sig_dk[sig].add(name)
    for name, vram, sig in ga:
        by_sig_ga[sig].append(vram)

    found, ambiguous = [], []
    for sig, names in by_sig_dk.items():
        hits = by_sig_ga.get(sig, [])
        if not hits:
            continue
        if len(names) == 1 and len(hits) == 1:
            found.append((hits[0], next(iter(names))))
        else:
            ambiguous.append((sorted(hits), sorted(names)))
    # Ambiguous: libultra keeps its link order, so pick the (address, name) pairs whose already-named neighbours agree
    # with DK64's neighbours (previous / next named function).
    dk_nb = defaultdict(set)  # name -> {(previous, next)} in each reference game
    for _, fns in refs:
        order = sorted((v, n) for n, v, _ in fns)
        for i, (_, n) in enumerate(order):
            dk_nb[n].add((order[i - 1][1] if i else None, order[i + 1][1] if i + 1 < len(order) else None))
    ga_vrams = sorted(v for _, v, _ in ga)
    for _ in range(3):  # resolving some ambiguities gives neighbours to others
        named = dict(found)
        left = []
        for hits, names in ambiguous:
            pairs = []
            for h in hits:
                i = ga_vrams.index(h)
                prev = named.get(ga_vrams[i - 1]) if i else None
                nxt = named.get(ga_vrams[i + 1]) if i + 1 < len(ga_vrams) else None
                for n in names:
                    score = max((prev is not None and prev == p) + (nxt is not None and nxt == q) for p, q in dk_nb[n])
                    if score:
                        pairs.append((h, n))
            used_h, used_n = set(), set()
            for h, n in pairs:
                if sum(1 for p in pairs if p[0] == h) == 1 and sum(1 for p in pairs if p[1] == n) == 1:
                    found.append((h, n))
                    used_h.add(h)
                    used_n.add(n)
            rest_h, rest_n = [h for h in hits if h not in used_h], [n for n in names if n not in used_n]
            if len(rest_h) == 1 and len(rest_n) == 1 and used_h:
                found.append((rest_h[0], rest_n[0]))
            elif rest_h and rest_n:
                left.append((rest_h, rest_n))
        ambiguous = left
    found = sorted(set(found))
    # two references can give one name two addresses (different signatures) or one address two names: drop and report
    by_name, by_addr = defaultdict(set), defaultdict(set)
    for v, n in found:
        by_name[n].add(v)
        by_addr[v].add(n)
    clash = [(v, n) for v, n in found if len(by_name[n]) > 1 or len(by_addr[v]) > 1]
    for v, n in clash:
        print(f"  CLASH dropped: 0x{v:08X} {n}")
    found = [f for f in found if f not in clash]
    lines = [f"{name} = 0x{vram:08X}; // type:func" for vram, name in found]
    (ROOT / "build" / "libultra_matches.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("references:", ", ".join(f"{label} {len(fns)} functions" for label, fns in refs), f"; GGA .main functions: {len(ga)}")
    print(f"unique matches: {len(found)} -> build/libultra_matches.txt")
    for hits, names in ambiguous:
        print("  ambiguous:", " ".join(f"0x{h:08X}" for h in hits), "<->", ", ".join(names))
    # compare with the names already in syms (they come from config/symbol_addrs.txt)
    for vram, name in found:
        cur = ga_names.get(vram, "")
        if not cur.startswith("func_") and cur != name:
            print(f"  CONFLICT 0x{vram:08X}: symbol_addrs says {cur}, match says {name}")
    new = [(v, n) for v, n in found if ga_names.get(v, "").startswith("func_")]
    print(f"new names ({len(new)}):", ", ".join(f"{n}@0x{v:08X}" for v, n in new))


if __name__ == "__main__":
    main()

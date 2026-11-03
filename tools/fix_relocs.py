#!/usr/bin/env python3
"""
Make the MIPS relocations of an ELF acceptable to N64Recomp (written for clang-assembled splat output).

N64Recomp follows the MIPS ABI: every R_MIPS_HI16 must be immediately followed by its R_MIPS_LO16, and both must name the same
symbol. GNU as guarantees that; clang emits relocations in instruction order (HI HI LO LO...), and spimdisasm sometimes writes
`%hi(symA)` + `%lo(symB)` for two neighbouring symbols. N64Recomp only keeps the *section* of the symbol (the exact address comes
from the instruction immediates, already final after linking), so it is safe to:
  1. pair each HI16 with LO16 relocations of a symbol in the SAME SECTION whose base register (bits 25..21) is the register written
     by the `lui` (bits 20..16): all of them, until another `lui` rewrites that register;
  2. if none, pair with the nearest later LO16 in the same section, whatever the register;
  3. move the LO16 right after the HI16 and give it the HI16's symbol index.
HI16 left without any LO16 are counted and reported (N64Recomp refuses an ELF where such a HI16 is followed by a different symbol).

Usage: python fix_relocs.py build/ga.elf      (in place, idempotent; ELF32 big-endian only)
"""
import struct
import sys

SHT_REL, R_HI16, R_LO16 = 9, 5, 6
WINDOW = 256  # how far ahead (in relocations) to look for the matching LO16


def main(path):
    d = bytearray(open(path, "rb").read())
    assert d[:4] == b"\x7fELF" and d[4] == 1 and d[5] == 2, "ELF32 big-endian expected"
    shoff = struct.unpack_from(">I", d, 0x20)[0]
    shentsize, shnum = struct.unpack_from(">HH", d, 0x2E)
    sh = [struct.unpack_from(">10I", d, shoff + i * shentsize) for i in range(shnum)]  # name type flags addr off size link info align entsize
    shndx_cache = {}

    def shndx(symtab, i):  # section index of symbol i (st_shndx)
        key = (symtab, i)
        if key not in shndx_cache:
            shndx_cache[key] = struct.unpack_from(">H", d, sh[symtab][4] + i * 16 + 14)[0]
        return shndx_cache[key]

    moved = total = lonely = 0
    for s in sh:
        if s[1] != SHT_REL:
            continue
        tgt = sh[s[7]]  # sh_info = section the relocations apply to
        if tgt[1] == 8:  # NOBITS
            continue
        n = s[5] // 8
        # lld does not emit the relocations in address order: sort first (stable), "later" means "higher address" below
        rels = sorted((struct.unpack_from(">II", d, s[4] + i * 8) for i in range(n)), key=lambda r: r[0])
        total += n

        def insn(off):
            return struct.unpack_from(">I", d, tgt[4] + off - tgt[3])[0]

        typ = lambda r: r[1] & 0xFF
        sym = lambda r: r[1] >> 8
        sec = lambda r: shndx(s[6], sym(r))
        used = [False] * n  # LO16 already given to a HI16
        match = {i: [] for i, r in enumerate(rels) if typ(r) == R_HI16}  # HI16 index -> its LO16 indexes

        def hunt(i, ok, any_reg):
            """Give HI16 #i the nearest later LO16 accepted by ok() (base register = the lui register unless any_reg)."""
            reg = (insn(rels[i][0]) >> 16) & 31
            for j in range(i + 1, min(n, i + WINDOW)):
                q = rels[j]
                if not any_reg and typ(q) == R_HI16 and (insn(q[0]) >> 16) & 31 == reg:
                    break  # register rewritten by another lui
                if not used[j] and typ(q) == R_LO16 and ok(q) and (any_reg or (insn(q[0]) >> 21) & 31 == reg):
                    used[j] = True
                    match[i].append(j)
                    return

        # global phases, exact pairs first, so that a HI16 without a LO16 of its own cannot steal the LO16 of a later HI16:
        # same symbol + register / same section (HI shared by neighbouring symbols) + register / same section, any register
        for phase in range(3):
            for i in match:
                if not match[i]:
                    r = rels[i]
                    hunt(i, (lambda q: sym(q) == sym(r)) if phase == 0 else (lambda q: sec(q) == sec(r)), phase == 2)
        # LO16 still free (second use of a lui value: HI LO LO, or a branch target): attach to the nearest earlier HI16 of the same
        # section and register, which N64Recomp accepts as consecutive LO16
        for j, q in enumerate(rels):
            if used[j] or typ(q) != R_LO16:
                continue
            for i in range(j - 1, max(-1, j - WINDOW), -1):
                if i in match and sec(rels[i]) == sec(q) and (insn(rels[i][0]) >> 16) & 31 == (insn(q[0]) >> 21) & 31:
                    used[j] = True
                    match[i].append(j)
                    break
        out = []
        for i, r in enumerate(rels):
            if used[i]:
                continue
            if i in match and not match[i]:
                # HI16 whose LO16 is at a branch target, out of reach: N64Recomp rejects it. Turn it into R_MIPS_NONE (the lui keeps its
                # linked immediate, which is right as long as the target does not move: overlays 8/9/10 stay at their VRAM, but a
                # 0x08xxxxxx module target would be a real loss -> printed so that it can be checked)
                lonely += 1
                print(f"  HI16 sans LO16 -> R_MIPS_NONE : section {s[7]} offset {r[0]:#x} lui {insn(r[0]) & 0xFFFF:#06x}xxxx")
                r = (r[0], r[1] & ~0xFF)
            out.append(r)
            for j in match.get(i, []):
                out.append((rels[j][0], (sym(r) << 8) | R_LO16))  # LO16 renamed to the HI16's symbol
        if out != rels:
            moved += 1
            for i, (o, inf) in enumerate(out):
                struct.pack_into(">II", d, s[4] + i * 8, o, inf)
    open(path, "wb").write(d)
    print(f"{total} relocations, {moved} sections reordonnees, {lonely} HI16 sans LO16")


if __name__ == "__main__":
    main(sys.argv[1])

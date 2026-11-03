"""GGA (2026-10-24): look for cracks between ground pieces in a display list dump (build\\dl_dump_<n>.txt).
The F7 dump trace was removed from src/main/rt64_render_context.cpp after the fix (ga_seam_grow); to recreate it, see
notes/REPRISE.md, section 0nonies. Lines: "C pc w0 w1", then "M" + 16 words after a G_MTX, one "V" + 4 words per vertex.

Replays the F3DEX2 matrix stack, G_VTX and triangles, then compares the open edges (used by one triangle only) of each
matrix group with the vertices of the other groups, in eye space (modelview, game units):
  - "shared"   : vertex of another group at the same place (< 0.01)
  - "near"     : vertex of another group close but not equal (crack between the pieces' vertices)
  - "T"        : vertex of another group lying on the edge, between its ends (T-junction)
Usage: python tools\\seam_check.py [build\\dl_dump.txt]
"""
import struct
import sys


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def mtx(words):
    m = [[0.0] * 4 for _ in range(4)]
    for k in range(16):
        w = words[k // 2]
        f = words[8 + k // 2]
        hi = (w >> 16) if k % 2 == 0 else (w & 0xFFFF)
        lo = (f >> 16) if k % 2 == 0 else (f & 0xFFFF)
        m[k // 4][k % 4] = ((s16(hi) << 16) | lo) / 65536.0
    return m


def mul(a, b):  # row vectors: v * a * b
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def xf(v, m):
    return [sum(v[k] * m[k][j] for k in range(4)) for j in range(4)]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "build\\dl_dump.txt"
    ident = [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
    mv, proj, stack = ident, ident, []
    group = 0
    vbuf = [None] * 64
    tris = []  # (group, [eye pos x3], [screen x3], [raw x3])
    lines = open(path).read().split("\n")
    i = 0
    pending_vtx = None
    while i < len(lines):
        p = lines[i].split()
        i += 1
        if not p:
            continue
        if p[0] == "C":
            w0, w1 = int(p[2], 16), int(p[3], 16)
            op = w0 >> 24
            if op == 0xDA:
                words = [int(x, 16) for x in lines[i].split()[1:]]
                i += 1
                b = (w0 & 0xFF) ^ 1
                m = mtx(words)
                if b & 4:
                    proj = m if b & 2 else mul(m, proj)
                else:
                    if b & 1:
                        stack.append(mv)
                    mv = m if b & 2 else mul(m, mv)
                    group += 1
            elif op == 0xD8:
                for _ in range(max(1, w1 // 64)):
                    if stack:
                        mv = stack.pop()
                group += 1
            elif op == 0x01:
                n = (w0 >> 12) & 0xFF
                v0 = ((w0 >> 1) & 0x7F) - n
                for k in range(n):
                    q = [int(x, 16) for x in lines[i].split()[1:]]
                    i += 1
                    raw = (s16(q[0] >> 16), s16(q[0] & 0xFFFF), s16(q[1] >> 16))
                    eye = xf([raw[0], raw[1], raw[2], 1.0], mv)
                    clip = xf(eye, proj)
                    scr = None
                    if clip[3] > 1e-6:
                        scr = ((clip[0] / clip[3] + 1) * 160, (1 - clip[1] / clip[3]) * 120)
                    if 0 <= v0 + k < 64:
                        vbuf[v0 + k] = (eye[:3], scr, raw)
            elif op in (0x05, 0x06, 0x07):
                idx = [[(w0 >> 16) & 0xFF, (w0 >> 8) & 0xFF, w0 & 0xFF]]
                if op != 0x05:
                    idx.append([(w1 >> 16) & 0xFF, (w1 >> 8) & 0xFF, w1 & 0xFF])
                for t in idx:
                    vs = [vbuf[x // 2] for x in t]
                    if all(vs):
                        tris.append((group, [v[0] for v in vs], [v[1] for v in vs], [v[2] for v in vs]))
    print(f"{len(tris)} triangles, {group} matrix groups")

    # open edges per group
    def key(p):
        return tuple(round(c, 2) for c in p)

    edges = {}
    for g, eye, scr, raw in tris:
        for a, b in ((0, 1), (1, 2), (2, 0)):
            ka, kb = key(eye[a]), key(eye[b])
            k = (g,) + tuple(sorted((ka, kb)))
            e = edges.setdefault(k, [0, eye[a], eye[b], scr[a], scr[b], raw[a], raw[b]])
            e[0] += 1
    open_e = [(k[0], v) for k, v in edges.items() if v[0] == 1]
    print(f"{len(open_e)} open edges")
    verts = {}
    for g, eye, scr, raw in tris:
        for j in range(3):
            verts.setdefault(g, []).append((eye[j], scr[j], raw[j]))

    def dist(a, b):
        return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5

    near, tj, shared = [], [], 0
    for g, (_, a, b, sa, sb, ra, rb) in open_e:
        L = dist(a, b)
        if L < 1e-6:
            continue
        d = [b[j] - a[j] for j in range(3)]
        for g2, vl in verts.items():
            if g2 == g:
                continue
            for p, sp, rp in vl:
                for end in (a, b):
                    de = dist(p, end)
                    if de < 0.01:
                        shared += 1
                    elif de < 3.0:
                        near.append((de, g, g2, end, p, sp))
                t = sum((p[j] - a[j]) * d[j] for j in range(3)) / (L * L)
                if 0.01 < t < 0.99:
                    q = [a[j] + t * d[j] for j in range(3)]
                    off = dist(p, q)
                    if off < 3.0:
                        tj.append((off, g, g2, p, sp, ra, rb, rp))
    print(f"shared vertices: {shared}, near (< 3 units, not equal): {len(near)}, T-junction candidates: {len(tj)}")
    for de, g, g2, e, p, sp in sorted(near, reverse=True)[:15]:
        print(f"  near {de:.4f}  group {g} / {g2}  eye {[round(c, 3) for c in e]} vs {[round(c, 3) for c in p]}  screen {sp and [round(c, 1) for c in sp]}")
    for off, g, g2, p, sp, ra, rb, rp in sorted(tj, reverse=True)[:15]:
        print(f"  T off {off:.4f}  group {g} edge {ra}-{rb} / group {g2} vertex {rp}  screen {sp and [round(c, 1) for c in sp]}")


main()

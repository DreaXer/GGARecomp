"""Decodes the textures dumped by RT64 (GGA_DUMP_TEXTURES=<folder>, see src/main/rt64_render_context.cpp), stdlib only.

RT64 writes, per texture (name = its replacement hash): <hash>.v5.tile.json (render tile, width / height), <hash>.v5.rice.json
(load: texture image, load tile, Tile / Block), <hash>.v5.rice.rdram (the RDRAM bytes loaded) and, with a TLUT,
<hash>.v5.rice.palette.rdram. The RDRAM copy is the recompiled game's memory: 32-bit words with their bytes reversed.

  python tools/tex_dump.py fringe <dump folder>   list the textures whose transparent texels next to opaque ones have a
                                                  very different color (what bilinear filtering shows as a colored fringe)
  (OBSOLETE since 2026-11-11: the same fix runs in the app, gga_fix_fringe in src/main/rt64_render_context.cpp)
  python tools/tex_dump.py bleed <dump folder>    texture pack build/gga_fringe_fix.rtz: those textures whose transparent
                                                  texels are a saturated key color (bright green, magenta: never meant to be
                                                  seen) get them recolored with the nearest opaque color ("alpha bleeding",
                                                  alpha unchanged): no more green fringe around trees, fences (2026-10-21)
"""
import json, os, sys, zipfile

BPP = {0: 4, 1: 8, 2: 16, 3: 32}


def n64_bytes(raw):
    """RDRAM copy (words with reversed bytes) -> N64 byte order. The dumps start on 8-byte boundaries."""
    out = bytearray(len(raw))
    for i in range(len(raw)):
        j = i ^ 3
        if j < len(raw):
            out[i] = raw[j]
    return bytes(out)


def rgba16(v):
    return ((v >> 11) & 31) * 255 // 31, ((v >> 6) & 31) * 255 // 31, ((v >> 1) & 31) * 255 // 31, 255 if v & 1 else 0


def decode(folder, base):
    """-> (width, height, rows of (r, g, b, a)) or None if the format is not handled."""
    tile = json.load(open(os.path.join(folder, base + '.tile.json')))
    rice = json.load(open(os.path.join(folder, base + '.rice.json')))
    w, h = tile['width'], tile['height']
    t = tile['tile']
    fmt, siz = t['fmt'], t['siz']
    if w <= 0 or h <= 0 or w > 1024 or h > 1024:
        return None
    data = n64_bytes(open(os.path.join(folder, base + '.rice.rdram'), 'rb').read())
    bits = BPP[siz]
    if rice['type'] == 'Block':
        stride = t['line'] * 8 if t['line'] > 0 else (w * bits + 7) // 8
    else:
        stride = rice['texture']['width'] * BPP[rice['texture']['siz']] // 8
    pal = None
    if fmt == 2:  # CI: RGBA16 palette
        pp = os.path.join(folder, base + '.rice.palette.rdram')
        if not os.path.exists(pp):
            return None
        pb = n64_bytes(open(pp, 'rb').read())
        pal = [rgba16((pb[2 * i] << 8) | pb[2 * i + 1]) for i in range(len(pb) // 2)]
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            bit = y * stride * 8 + x * bits
            o = bit // 8
            if o + max(1, bits // 8) > len(data):
                return None
            if bits == 4:
                v = (data[o] >> 4) if x % 2 == 0 else (data[o] & 15)
            elif bits == 8:
                v = data[o]
            elif bits == 16:
                v = (data[o] << 8) | data[o + 1]
            else:
                v = int.from_bytes(data[o:o + 4], 'big')
            if fmt == 0 and bits == 16:
                px = rgba16(v)
            elif fmt == 0 and bits == 32:
                px = (v >> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255)
            elif fmt == 2:
                idx = v + (t['palette'] * 16 if bits == 4 else 0)
                if idx >= len(pal):
                    return None
                px = pal[idx]
            elif fmt == 3 and bits == 16:  # IA16
                px = (v >> 8, v >> 8, v >> 8, v & 255)
            elif fmt == 3 and bits == 8:   # IA8
                i = (v >> 4) * 17
                px = (i, i, i, (v & 15) * 17)
            elif fmt == 3 and bits == 4:   # IA4
                i = (v >> 1) * 255 // 7
                px = (i, i, i, 255 if v & 1 else 0)
            elif fmt == 4:                 # I4 / I8: no alpha to fringe
                return None
            else:
                return None
            row.append(px)
        rows.append(row)
    return w, h, rows


def fringe(folder):
    for d, n, base, w, h, col in edge_stats(folder):
        print('%-28s %4dx%-4d edge texels %4d  mean color diff %5.0f  transparent color %s' % (base, w, h, n, d, col))


def edge_stats(folder):
    """(mean color difference transparent / opaque neighbors, edge texel count, base, w, h, mean transparent color), worst
    first. Textures bigger than TMEM (4 KB) are decoding errors (tile size deduced from a clamp) and are skipped."""
    res = []
    for fn in sorted(os.listdir(folder)):
        if not fn.endswith('.tile.json'):
            continue
        base = fn[:-len('.tile.json')]
        try:
            img = decode(folder, base)
        except (OSError, KeyError, ValueError):
            img = None
        if img is None:
            continue
        w, h, rows = img
        tile = json.load(open(os.path.join(folder, base + '.tile.json')))['tile']
        if w * h * BPP[tile['siz']] // 8 > 4096:
            continue
        n = diff = 0
        acc = [0, 0, 0]
        for y in range(h):
            for x in range(w):
                r, g, b, a = rows[y][x]
                if a >= 128:
                    continue
                nb = [rows[yy][xx] for xx, yy in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))
                      if 0 <= xx < w and 0 <= yy < h and rows[yy][xx][3] >= 128]
                if not nb:
                    continue
                mr = sum(p[0] for p in nb) / len(nb)
                mg = sum(p[1] for p in nb) / len(nb)
                mb = sum(p[2] for p in nb) / len(nb)
                n += 1
                diff += abs(r - mr) + abs(g - mg) + abs(b - mb)
                acc[0] += r; acc[1] += g; acc[2] += b
        if n:
            res.append((diff / n, n, base, w, h, tuple(int(c / n) for c in acc)))
    res.sort(reverse=True)
    return res


def fmt_of(folder, base):
    return json.load(open(os.path.join(folder, base + '.tile.json')))['tile']['fmt']


def bleed_rows(w, h, rows):
    """Transparent texels take the mean color of their already colored neighbors, ring after ring from the opaque ones."""
    done = [[p[3] >= 128 for p in row] for row in rows]
    out = [[list(p) for p in row] for row in rows]
    while True:
        changed = []
        for y in range(h):
            for x in range(w):
                if done[y][x]:
                    continue
                nb = [out[yy][xx] for xx, yy in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1),
                                                 (x - 1, y - 1), (x + 1, y - 1), (x - 1, y + 1), (x + 1, y + 1))
                      if 0 <= xx < w and 0 <= yy < h and done[yy][xx]]
                if nb:
                    changed.append((x, y, [sum(p[c] for p in nb) // len(nb) for c in range(3)]))
        if not changed:
            return out
        for x, y, c in changed:
            out[y][x][:3] = c
            done[y][x] = True


def bleed(folder):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from hud_art import save_png
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    pack = os.path.join(root, 'build', 'fringe_pack')
    os.makedirs(pack, exist_ok=True)
    textures = []
    for d, n, base, w, h, col in edge_stats(folder):
        r, g, b = col
        key = (g >= 150 and r <= 60 and b <= 140) or (r >= 200 and b >= 200 and g <= 60)  # green / magenta key colors only:
        if d < 150 or not key:  # an opaque texture may have meaningless alpha bits (e.g. red (185, 4, 1) seen, 2026-10-21)
            continue
        # color-indexed textures are left out (the village background's green fringe came from the widened panorama, fixed in
        # ga_widescreen.cpp, 2026-10-24). Replacements of the village background (CI8 stacks of 16 x 16 tiles) showed lines /
        # ghost roofs at tile edges, cause not proven (CI or stacking): stacks of tiles are left out too
        if fmt_of(folder, base) == 2 or (h > w and h % w == 0):
            continue
        _, _, rows = decode(folder, base)
        # a texture taller than wide, height multiple of its width, is a stack of square tiles drawn one at a time (village
        # background strips 16 x 128 = 8 tiles 16 x 16): bleeding across tiles put a neighbour tile's colors at a tile's edge
        # (ghost roofs, lines, 2026-10-21) -> bleed inside each tile
        th = w if h > w and h % w == 0 else h
        out = []
        for y0 in range(0, h, th):
            out += bleed_rows(w, th, rows[y0:y0 + th])
        name = base.split('.')[0] + '.png'
        save_png(os.path.join(pack, name), (w, h, [bytearray(b for p in row for b in p) for row in out]))
        textures.append({'path': name, 'hashes': {'rt64': base.split('.')[0]}})
        print('fixed', base, '%dx%d' % (w, h), 'key color', col)
    # defaultShift none: same-resolution replacements; RT64's default (half a texel, meant for HD packs) samples the next tile's
    # row at the edges of tiles stacked in one texture (lines in the village background, 2026-10-21)
    db = {'configuration': {'autoPath': 'rt64', 'configurationVersion': 3, 'hashVersion': 5, 'defaultShift': 'none'},
          'textures': textures}
    json.dump(db, open(os.path.join(pack, 'rt64.json'), 'w'), indent=2)
    rtz = os.path.join(root, 'build', 'gga_fringe_fix.rtz')
    with zipfile.ZipFile(rtz, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(os.path.join(pack, 'rt64.json'), 'rt64.json')
        for t in textures:
            z.write(os.path.join(pack, t['path']), t['path'])
    print('ok:', rtz, len(textures), 'textures')


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == 'fringe':
        fringe(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == 'bleed':
        bleed(sys.argv[2])
    else:
        sys.exit(__doc__)

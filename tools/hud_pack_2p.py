"""Texture pack of the user's HD HUD art (assets/hud/new/, 2026-10-23), stdlib only.

1. hud_2p_fond_v2.png redraws the game's 2P HUD background (288 x 32: textures J1 0x03000000 / 0x03001000, center
   0x03002000, J2 0x03002800 / 0x03003800, x 16..304 of the 4:3 screen, drawn by the game in every mode). Its bar (opaque area
   measured: x 0..2171, y 203..457, about 7.5 px per game pixel; v1 hud_2p_fond.png: x 6..2165, y 225..468) is cut at the
   texture boundaries and each piece replaces its texture by RT64 hash (hashes = names of the RT64 dumps of those textures in
   build/texdump, one per texture).
2. hud_3p.png / hud_4p.png: the J3 / J4 blocks at the top in 3P / 4P (128 x 32 in the game). The game has no texture of its own
   for them, so ga_hud.c draws "proxies" (the art scaled down to 2 halves of 64 x 32 RGBA16, src/game/ga_hud_top.h, written
   here) and the pack maps the proxies' hashes, found in an RT64 dump (GGA_DUMP_TEXTURES), to the HD halves.

  python tools/hud_pack_2p.py               -> src/game/ga_hud_top.h + build/gga_hud_2p.rtz (bar only)
  python tools/hud_pack_2p.py <dump folder> -> the same, with the J3 / J4 halves found in the dump
  (copy the .rtz to %LOCALAPPDATA%\\Goemon64Recompiled\\mods)
"""
import colorsys, json, os, sys, zipfile

sys.path.insert(0, os.path.dirname(__file__))
from hud_art import load_png, crop, save_png, proxy_rgba16, tmem_matches

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NEW = os.path.join(ROOT, 'assets', 'hud', 'new')
SRC = os.path.join(NEW, 'hud_2p_fond_v2.png')
HEADER = os.path.join(ROOT, 'src', 'game', 'ga_hud_top.h')
PACK_DIR = os.path.join(ROOT, 'build', 'hud_pack_2p')
RTZ = os.path.join(ROOT, 'build', 'gga_hud_2p.rtz')
BAR_X0, BAR_X1, BAR_Y0, BAR_Y1 = 0, 2171, 203, 458  # 2171: the user cut the last column (stray yellow), 2026-10-23
# (file, first game pixel, width in game pixels, RT64 hash)
PIECES = [('j1a.png', 0, 64, '5b87d334c382c791'), ('j1b.png', 64, 64, '1936bce07e2b7c97'),
          ('center.png', 128, 32, '208befeff2bebb54'), ('j2a.png', 160, 64, 'ece6786e547a0bee'),
          ('j2b.png', 224, 64, 'de85b4e3c93c95b3')]
TOP = ['hud_3p.png', 'hud_4p.png']  # J3, J4: whole images (opaque area = the whole image, measured)


def binary_alpha(img):
    """The N64 textures have a 1-bit alpha: half transparent pixels (antialiased edges) showed their stray colors (user's
    test, 2026-10-23) -> each pixel fully opaque or fully transparent (black)."""
    for row in img[2]:
        for i in range(3, len(row), 4):
            if row[i] >= 128:
                row[i] = 255
            else:
                row[i - 3:i + 1] = b'\x00\x00\x00\x00'
    return img


def saturate(img, k):
    """Saturation x k (HLS). The bar art is a bit less saturated than hud_3p / hud_4p (measured 2026-10-23: mean 0.848 vs
    0.874 / 0.878), so the top blocks looked brighter than the bottom ones (user's capture); the user wants the bar's look."""
    for row in img[2]:
        for i in range(0, len(row), 4):
            if row[i + 3]:
                h, l, s = colorsys.rgb_to_hls(row[i] / 255, row[i + 1] / 255, row[i + 2] / 255)
                row[i:i + 3] = bytes(round(v * 255) for v in colorsys.hls_to_rgb(h, l, min(1.0, s * k)))
    return img


BAR_SAT = 1.0   # saturation of the bar art (1.0 = as drawn; 1.03 tried: the user prefers the bar as drawn)
TOP_SAT = 0.80  # saturation of the J3 / J4 HD art (0.97 = 0.848 / 0.876, the measured ratio: "still too bright") (the proxies keep the art: same hashes)
os.makedirs(PACK_DIR, exist_ok=True)
img = saturate(binary_alpha(load_png(SRC)), BAR_SAT)
k = (BAR_X1 - BAR_X0) / 288
for fn, x, w, _ in PIECES:
    save_png(os.path.join(PACK_DIR, fn), crop(img, (BAR_X0 + round(x * k), BAR_Y0, BAR_X0 + round((x + w) * k), BAR_Y1)))
textures = [{'path': fn, 'hashes': {'rt64': h}} for fn, _, _, h in PIECES]
# Weather disc of the bubble (0x03008D40, 32 x 32 RGBA16, night half at the top, day half at the bottom, turned by the game;
# hash = its RT64 dump in build/texdump_hud, 2026-10-23; original: assets/hud/original/meteo.png): the user's meteo.png if any
# Put on a transparent square where the disc takes METEO_DISC of the width (original: 30 / 32 = 0.94; 0.85 = a bit smaller,
# user's request 2026-10-23). Measured on the user's meteo.png (1254 x 1254): opaque disc x 80..1168, top 72 (the bottom
# has a glow) -> diameter 1089, centered on (624, 617).
METEO_DISC, METEO_D, METEO_CX, METEO_CY = 0.85, 1089, 624, 617


def disc_square(img):
    w, h, rows = img
    size = round(METEO_D / METEO_DISC)
    x0, y0 = METEO_CX - size // 2, METEO_CY - size // 2
    out = []
    for y in range(size):
        row = bytearray(size * 4)
        sy = y0 + y
        if 0 <= sy < h:
            for x in range(max(0, -x0), min(size, w - x0)):
                row[x * 4:x * 4 + 4] = rows[sy][(x0 + x) * 4:(x0 + x) * 4 + 4]
        out.append(row)
    return size, size, out


if os.path.exists(os.path.join(NEW, 'meteo.png')):
    save_png(os.path.join(PACK_DIR, 'meteo.png'), binary_alpha(disc_square(load_png(os.path.join(NEW, 'meteo.png')))))
    PIECES.append(('meteo.png', 0, 0, 'e2f6b3a935a55e4e'))
    textures.append({'path': 'meteo.png', 'hashes': {'rt64': 'e2f6b3a935a55e4e'}})

# J3 / J4: HD halves + proxies (the same halves scaled down to 64 x 32)
halves, proxies = [], []
for fn in TOP:
    top = binary_alpha(load_png(os.path.join(NEW, fn)))
    w, h = top[0], top[1]
    for i, box in enumerate(((0, 0, w // 2, h), (w // 2, 0, w, h))):
        half = crop(top, box)
        name = fn[:-4] + 'ab'[i] + '.png'
        save_png(os.path.join(PACK_DIR, name), saturate((half[0], half[1], [bytearray(r) for r in half[2]]), TOP_SAT))
        halves.append(name)
        proxies.append(proxy_rgba16(half, (64, 32)))
with open(HEADER, 'w', newline='\n') as f:
    f.write('/* Generated by tools/hud_pack_2p.py (do not edit): J3 / J4 HUD blocks, halves of 64 x 32 RGBA 5551 (J3 left, J3 right,\n'
            '   J4 left, J4 right), the user\'s art scaled down; the texture pack replaces them by the HD art. */\n')
    f.write('static const uint16_t top_tex[4][64 * 32] = {\n')
    for p in proxies:
        f.write('    {\n')
        for i in range(0, len(p), 16):
            f.write('        ' + ', '.join(f'0x{v:04X}' for v in p[i:i + 16]) + ',\n')
        f.write('    },\n')
    f.write('};\n')

if len(sys.argv) >= 2:  # the proxies' hashes, from an RT64 dump made with this header compiled in
    dump, found = sys.argv[1], {}
    for fn in sorted(os.listdir(dump)):
        if not fn.endswith('.tile.json'):
            continue
        base = fn[:-len('.tile.json')]
        info = json.load(open(os.path.join(dump, fn)))
        if (info.get('width'), info.get('height')) != (64, 32) or not os.path.exists(os.path.join(dump, base + '.tmem')):
            continue
        tmem = open(os.path.join(dump, base + '.tmem'), 'rb').read()
        for i, p in enumerate(proxies):
            if tmem_matches(tmem, p, 64):
                found.setdefault(i, set()).add(base.split('.')[0])
    for i, name in enumerate(halves):
        print(name, '->', sorted(found.get(i, [])) or 'NOT FOUND')
        textures += [{'path': name, 'hashes': {'rt64': h}} for h in sorted(found.get(i, []))]
else:
    print('J3 / J4 not in the pack: give the RT64 dump folder (GGA_DUMP_TEXTURES, 3P / 4P HUD shown)')
    halves = []

db = {'configuration': {'autoPath': 'rt64', 'configurationVersion': 3, 'hashVersion': 5}, 'textures': textures}
json.dump(db, open(os.path.join(PACK_DIR, 'rt64.json'), 'w'), indent=2)
with zipfile.ZipFile(RTZ, 'w', zipfile.ZIP_DEFLATED) as z:
    for fn in ['rt64.json'] + [p[0] for p in PIECES] + halves:  # no mod.json: .rtz gets a default manifest
        z.write(os.path.join(PACK_DIR, fn), fn)
print('ok:', HEADER, RTZ)

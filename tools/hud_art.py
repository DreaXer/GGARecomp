"""HD art of the wide 3P / 4P HUD (2026-10-21), stdlib only.

  python tools/hud_art.py
      assets/hud/bloc.png + "hud meteo.png" -> src/game/ga_hud_art.h (small RGBA16 "proxy" textures drawn by ga_hud.c)
      and build/hud_pack/*.png (the HD images, cropped to what the proxies cover).
  python tools/hud_art.py pack <RT64 texture dump folder>
      finds the proxies among the dumped textures (RT64 names each dump by its replacement hash), writes
      build/hud_pack/rt64.json and build/gga_hud_hd.rtz (texture pack mod: copy it to the mods folder).

Why proxies: RT64 replaces a texture by the hash of its contents once loaded in TMEM; the game draws the proxy, the pack
maps the proxy's hash to the HD image. The proxy is the HD image scaled down, so the HUD still looks right without the pack.
"""
import json, os, struct, sys, zipfile, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BLOCK_SRC = os.path.join(ROOT, 'assets', 'hud', 'bloc_large.png')  # ChatGPT's image, not squeezed (2026-10-21)
BUBBLE_SRC = os.path.join(ROOT, 'assets', 'hud', 'hud meteo.png')
HEADER = os.path.join(ROOT, 'src', 'game', 'ga_hud_art.h')
PACK_DIR = os.path.join(ROOT, 'build', 'hud_pack')
RTZ = os.path.join(ROOT, 'build', 'gga_hud_hd.rtz')

# Crops (measured on the images, 2026-10-21): bloc_large.png opaque area x 5..859 / y 71..217; the bubble keeps its whole square.
BLOCK_CROP = (5, 71, 860, 218)
BLOCK_PROXY = (64, 11)   # 5.8:1 like the art (855 x 147); width 64 (power of 2: load block row swaps stay exact)
BUBBLE_PROXY = (32, 32)


def load_png(path):
    b = open(path, 'rb').read()
    assert b[:8] == b'\x89PNG\r\n\x1a\n', path
    p, idat = 8, b''
    while p < len(b):
        n, t = struct.unpack('>I4s', b[p:p + 8])
        if t == b'IHDR':
            w, h, bd, ct, _, _, il = struct.unpack('>IIBBBBB', b[p + 8:p + 21])
            assert bd == 8 and ct == 6 and il == 0, f'{path}: 8-bit RGBA, not interlaced expected'
        elif t == b'IDAT':
            idat += b[p + 8:p + 8 + n]
        p += 12 + n
    raw, st, prev, rows = zlib.decompress(idat), w * 4, bytearray(w * 4), []
    for y in range(h):
        f, line = raw[y * (st + 1)], bytearray(raw[y * (st + 1) + 1:(y + 1) * (st + 1)])
        for i in range(st):
            a = line[i - 4] if i >= 4 else 0
            up, c = prev[i], (prev[i - 4] if i >= 4 else 0)
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + up) & 255
            elif f == 3:
                line[i] = (line[i] + (a + up) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(up - c), abs(a - c), abs(a + up - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else (up if pb <= pc else c))) & 255
        rows.append(line)
        prev = line
    return w, h, rows


def crop(img, box):
    w, h, rows = img
    x0, y0, x1, y1 = box
    return x1 - x0, y1 - y0, [r[x0 * 4:x1 * 4] for r in rows[y0:y1]]


def save_png(path, img):
    w, h, rows = img
    raw = b''.join(b'\x00' + bytes(r) for r in rows)
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def proxy_rgba16(img, size):
    """Area average (alpha weighted) down to size, as RGBA 5551 values."""
    w, h, rows = img
    pw, ph = size
    out = []
    for py in range(ph):
        y0, y1 = py * h // ph, max((py + 1) * h // ph, py * h // ph + 1)
        for px in range(pw):
            x0, x1 = px * w // pw, max((px + 1) * w // pw, px * w // pw + 1)
            r = g = b = a = n = 0
            for y in range(y0, y1):
                row = rows[y]
                for x in range(x0, x1):
                    al = row[x * 4 + 3]
                    r += row[x * 4] * al
                    g += row[x * 4 + 1] * al
                    b += row[x * 4 + 2] * al
                    a += al
                    n += 1
            if a == 0:
                out.append(0)
                continue
            r, g, b = r // a, g // a, b // a
            out.append(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (1 if a // n >= 128 else 0))
    return out


def c_array(name, values, size):
    lines = [f'static const uint16_t {name}[{size[0]} * {size[1]}] = {{']
    for i in range(0, len(values), 16):
        lines.append('    ' + ', '.join(f'0x{v:04X}' for v in values[i:i + 16]) + ',')
    return '\n'.join(lines) + '\n};\n'


def make():
    block = crop(load_png(BLOCK_SRC), BLOCK_CROP)
    bubble = load_png(BUBBLE_SRC)
    os.makedirs(PACK_DIR, exist_ok=True)
    save_png(os.path.join(PACK_DIR, 'hud_block.png'), block)
    save_png(os.path.join(PACK_DIR, 'hud_bubble.png'), bubble)
    with open(HEADER, 'w', newline='\n') as f:
        f.write('/* Generated by tools/hud_art.py from assets/hud/ (do not edit): proxy textures of the HD HUD art, RGBA 5551. */\n')
        f.write('#define HUD_ART_BLOCK_W %d\n#define HUD_ART_BLOCK_H %d\n' % BLOCK_PROXY)
        f.write('#define HUD_ART_BUBBLE_W %d\n#define HUD_ART_BUBBLE_H %d\n' % BUBBLE_PROXY)
        f.write(c_array('hud_art_block', proxy_rgba16(block, BLOCK_PROXY), BLOCK_PROXY))
        f.write(c_array('hud_art_bubble', proxy_rgba16(bubble, BUBBLE_PROXY), BUBBLE_PROXY))
    print('ok:', HEADER, PACK_DIR)


def tmem_matches(tmem, texels, width):
    """First row of the proxy at TMEM address 0, in either byte order (RT64's TMEM order not assumed)."""
    row = texels[:width]
    be = b''.join(struct.pack('>H', v) for v in row)
    le = b''.join(struct.pack('<H', v) for v in row)
    swz = bytes(be[i ^ 3] for i in range(len(be)))
    return tmem[:len(be)] in (be, le, swz)


def pack(dump_dir):
    src = open(HEADER).read()
    arrays = {}
    for name in ('hud_art_block', 'hud_art_bubble'):
        body = src.split(name + '[')[1].split('{', 1)[1].split('}', 1)[0]
        arrays[name] = [int(v, 16) for v in body.replace('\n', ' ').split(',') if v.strip()]
    wanted = {'hud_art_block': (BLOCK_PROXY, 'hud_block.png'), 'hud_art_bubble': (BUBBLE_PROXY, 'hud_bubble.png')}
    found = {}
    for fn in sorted(os.listdir(dump_dir)):
        if not fn.endswith('.tile.json'):
            continue
        base = fn[:-len('.tile.json')]
        info = json.load(open(os.path.join(dump_dir, fn)))
        tmem_path = os.path.join(dump_dir, base + '.tmem')
        if not os.path.exists(tmem_path):
            continue
        tmem = open(tmem_path, 'rb').read()
        for name, (size, _) in wanted.items():
            if (info.get('width'), info.get('height')) == size and tmem_matches(tmem, arrays[name], size[0]):
                found.setdefault(name, []).append(base.split('.')[0])
    for name in wanted:
        print(name, '->', found.get(name, 'NOT FOUND'))
    if len(found) != len(wanted):
        sys.exit('missing proxies in the dump: play the 3P / 4P HUD in widescreen with GGA_DUMP_TEXTURES set')
    db = {
        'configuration': {'autoPath': 'rt64', 'configurationVersion': 3, 'hashVersion': 5},
        'textures': [{'path': wanted[name][1], 'hashes': {'rt64': h}} for name, hs in found.items() for h in hs],
    }
    json.dump(db, open(os.path.join(PACK_DIR, 'rt64.json'), 'w'), indent=2)
    with zipfile.ZipFile(RTZ, 'w', zipfile.ZIP_DEFLATED) as z:
        for fn in ('rt64.json', 'hud_block.png', 'hud_bubble.png'):  # no mod.json: .rtz gets a default manifest
            z.write(os.path.join(PACK_DIR, fn), fn)
    print('ok:', RTZ)


if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == 'pack':
        pack(sys.argv[2])
    else:
        make()

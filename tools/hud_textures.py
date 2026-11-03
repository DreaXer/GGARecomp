# Converts the HUD textures (hud_seg3.bin: 40 KB of segment 3 dumped from RDRAM, see notes/REPRISE.md, "HUD") to PNG files,
# 8x enlarged, with a magenta grid every 8 texels to read texel coordinates. Stdlib only.
# Usage (project root): python tools\hud_textures.py hud_seg3.bin <output folder>
import struct, sys, zlib, os

data = open(sys.argv[1], 'rb').read()
out = sys.argv[2]


def png(path, w, h, px, scale):
    rows = b''
    for y in range(h * scale):
        row = bytearray([0])
        for x in range(w * scale):
            row += bytes(px[(y // scale) * w + x // scale])
        rows += bytes(row)

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w * scale, h * scale, 8, 6, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def rgba16(off, w, h):
    px = []
    for i in range(w * h):
        v = struct.unpack('>H', data[off + 2 * i: off + 2 * i + 2])[0]
        r, g, b, a = (v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31, v & 1
        px.append((r * 255 // 31, g * 255 // 31, b * 255 // 31, 255 if a else 0))
    return px


def grid(px, w, step):
    return [(255, 0, 255, 255) if (i % w) % step == 0 or (i // w) % step == 0 else p for i, p in enumerate(px)]


# segment 3 offsets (display lists of file 6: D_80108C50, D_80108E18)
for name, off, w, h in [('bar_j1_a', 0x0000, 64, 32), ('bar_j1_b', 0x1000, 64, 32), ('center', 0x2000, 32, 32),
                        ('bar_j2_a', 0x2800, 64, 32), ('bar_j2_b', 0x3800, 64, 32), ('atlas', 0x4F00, 64, 32)]:
    png(os.path.join(out, name + '.png'), w, h, grid(rgba16(off, w, h), w, 8), 8)

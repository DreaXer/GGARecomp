#!/usr/bin/env python3
"""
Decompress the Konami "Nisitenma-Ichigo" file table of Goemon's Great Adventure (US, NGME).

Layout of the output (same idea as lib/mnsg/tools/rommy.py for Goemon 1):
    ROM[0:first file]  copied as is (header, boot, code, file table)
    every file, decompressed, written back to back (no gaps)
    ROM[end marker:]   copied as is (data after the last file)
The file table is rewritten with the new offsets (bit 31 cleared, nothing is compressed any more).

Per-file format:
    table bit 31 = 0 : raw data, no header
    table bit 31 = 1 : 4-byte header, then
        header bit 31 = 1                  -> zlib stream ("78 DA ...")
        header bit 31 = 0, top 4 bits = 0  -> LZKN64 (header = compressed size incl. header); only rommy file 8 uses it
        (the game also knows a multi-chunk LZKN64 variant, top 4 bits != 0: never seen, raises)

Usage:
    python ga_rommy.py decompress -i baserom.us.z64 -o baserom.us.decompressed.z64 -m config/rommy.json
Standard library only. The header CRCs of the ROM are NOT recalculated (N64Recomp does not check them).
"""
import argparse
import json
import struct
import sys
import zlib

SIG = b"Nisitenma-Ichigo"
FLAG = 0x80000000
MASK = 0x7FFFFFFF


def read_table(rom):
    """Return (offset of the first entry, list of raw 32-bit entries without the zero terminator)."""
    base = rom.find(SIG)
    if base < 0:
        sys.exit("Nisitenma-Ichigo signature not found")
    base += len(SIG)
    ents = []
    while True:
        v = struct.unpack_from(">I", rom, base + 4 * len(ents))[0]
        if v == 0:
            return base, ents
        ents.append(v)


def lzkn64_decompress(src):
    """LZKN64 stream (after the 4-byte header). Commands, deduced by comparing with the `lzkn64` crate
    on file 8 (output identical, 0x44400 bytes):
        00-7F  window copy: len = (c>>2)+2, offset = ((c&3)<<8) | next byte
        80-9F  literal: c&0x1F bytes
        A0-BF  never seen
        C0-DF  fill: next byte repeated (c&0x1F)+2 times
        E0-FE  zeros: (c&0x1F)+2
        FF     zeros: next byte + 2   (seen once, run of 236)
    """
    out = bytearray()
    i = 0
    while i < len(src):
        c = src[i]
        i += 1
        if c < 0x80:
            n, o = (c >> 2) + 2, ((c & 3) << 8) | src[i]
            i += 1
            for _ in range(n):
                out.append(out[-o])
        elif c < 0xA0:
            n = c & 0x1F
            out += src[i : i + n]
            i += n
        elif c < 0xC0:
            raise ValueError(f"unknown LZKN64 command {c:#x}")
        elif c < 0xE0:
            out += bytes([src[i]]) * ((c & 0x1F) + 2)
            i += 1
        elif c < 0xFF:
            out += bytes((c & 0x1F) + 2)
        else:
            out += bytes(src[i] + 2)
            i += 1
    return bytes(out)


def unpack(raw, compressed):
    """Return (kind, data) for one file span."""
    if not compressed:
        return "raw", raw
    h = struct.unpack_from(">I", raw)[0]
    if h & FLAG:
        d = zlib.decompressobj()
        out = d.decompress(raw[4:])
        # one trailing byte after the zlib stream has been seen in real files
        if not d.eof or len(d.unused_data) > 1:
            raise ValueError("bad zlib stream")
        return "zlib", out
    if h >> 28:
        raise ValueError("multi-chunk LZKN64 variant, not implemented")
    # header = compressed size including the 4 header bytes
    return "lzkn64", lzkn64_decompress(raw[4 : h])


def decompress(rom):
    base, ents = read_table(rom)
    offs = [e & MASK for e in ents]
    # the last entry is the end marker of the last file, not a file itself
    if offs != sorted(offs) or offs[-1] > len(rom):
        sys.exit("file table is not sorted / out of bounds")
    out = bytearray(rom[: offs[0]])
    new_offs, files = [], []
    for i in range(len(ents) - 1):
        raw = rom[offs[i] : offs[i + 1]]
        kind, data = unpack(raw, bool(ents[i] & FLAG)) if raw else ("empty", b"")
        new_offs.append(len(out))
        files.append(
            {
                "index": i,
                "kind": kind,
                "orig_offset": offs[i],
                "orig_size": len(raw),
                "new_offset": len(out),
                "new_size": len(data),
                "crc32": zlib.crc32(data),
            }
        )
        out += data
    new_offs.append(len(out))  # end marker
    out += rom[offs[-1] :]  # tail after the last file
    for i, o in enumerate(new_offs):
        struct.pack_into(">I", out, base + 4 * i, o)
    manifest = {
        "version": 1,
        "table_offset": base,
        "end_marker_orig": offs[-1],
        "end_marker_new": new_offs[-1],
        "files": files,
    }
    return out, manifest


def verify(out, manifest):
    """Re-read the output table and check every file against the manifest."""
    base, ents = read_table(bytes(out))
    assert base == manifest["table_offset"] and len(ents) == len(manifest["files"]) + 1
    assert all(not e & FLAG for e in ents), "output must have no compressed entries"
    for f, o, n in zip(manifest["files"], ents, ents[1:]):
        assert (o, n - o) == (f["new_offset"], f["new_size"]), f"file {f['index']} moved"
        assert zlib.crc32(bytes(out[o:n])) == f["crc32"], f"file {f['index']} crc"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["decompress"])
    ap.add_argument("-i", "--input", required=True)
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("-m", "--manifest", required=True)
    a = ap.parse_args()
    rom = open(a.input, "rb").read()
    out, manifest = decompress(rom)
    verify(out, manifest)
    open(a.output, "wb").write(out)
    json.dump(manifest, open(a.manifest, "w"), indent=1)
    kinds = {}
    for f in manifest["files"]:
        kinds[f["kind"]] = kinds.get(f["kind"], 0) + 1
    print(f"{len(rom):,} -> {len(out):,} bytes; files: {kinds}")


if __name__ == "__main__":
    main()

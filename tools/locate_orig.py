# usage: python tools\locate_orig.py 0x5BA5E6 0x5BA615
import json, sys
m = json.load(open("config/rommy.json"))
orig = open("baserom.us.z64", "rb").read()
new = open("baserom.us.decompressed.z64", "rb").read()
for arg in sys.argv[1:]:
    x = int(arg, 0)
    for f in m["files"]:
        if f["orig_size"] and f["orig_offset"] <= x < f["orig_offset"] + f["orig_size"]:
            d = x - f["orig_offset"]
            n = f["new_offset"] + d
            print(f"orig 0x{x:X}: fichier {f['index']} kind={f['kind']} "
                  f"orig_off=0x{f['orig_offset']:X} orig_size=0x{f['orig_size']:X} "
                  f"new_off=0x{f['new_offset']:X} new_size=0x{f['new_size']:X}")
            print(f"  decalage dans le fichier 0x{d:X} (mod 4 = {d % 4}) -> decompresse 0x{n:X}")
            print(f"  orig  : {orig[x-8:x+12].hex(' ')}")
            print(f"  decomp: {new[n-8:n+12].hex(' ')}")
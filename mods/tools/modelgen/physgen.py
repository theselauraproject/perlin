"""Packs the Bedrock block physics registry (collision boxes, friction, flags) into resources/block_physics.bin.

Input: physics.json, extracted from cinnabar's block-physics-seed-v2193.bin
       (record = [seq, hash, flags, surface, friction, hspeed, vspeed, fluid_height, boxes]; 1e8 units).
Output format "PHY1": u32 count, then per record:
  i32 hash, u8 flags, u8 surface, f32 friction, f32 hspeed, f32 vspeed, f32 fluidHeight, u8 nBoxes, nBoxes * 6 f32
"""
import json
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "resources", "block_physics.bin")
S = 1e8

recs = json.load(open(os.path.join(HERE, "physics.json")))
out = bytearray(b"PHY1" + struct.pack("<I", len(recs)))
for seq, h, flags, surface, fr, hs, vs, fh, boxes in recs:
    h = h if h < 2 ** 31 else h - 2 ** 32
    out += struct.pack("<iBBffffB", h, flags, surface, fr / S, hs / S, vs / S, fh / S, len(boxes))
    for b in boxes:
        out += struct.pack("<6f", *[c / S for c in b])
open(OUT, "wb").write(out)
print("wrote", OUT, len(out), "bytes,", len(recs), "records")

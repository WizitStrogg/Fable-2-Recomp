#!/usr/bin/env python3
"""make_female_template.py - feature 001 T021a: a female co-op starter save made
from the male one, so a female host starts as a woman (Player.ChangePlayerEntityType
on the loaded hero returns true but leaves the hero male: launch 19, Gender.Get = M).

Edits a COPY (the source is only read):
  mainsave.bin   the hero's creature record names, stored as u32 BE length + text + NUL:
                 "CreatureHero" -> "CreatureHeroFemale",
                 "CreatureHeroChild" -> "CreatureHeroFemaleChild"
                 (the save is one zlib stream in [u32 csize][u32 usize] blocks of
                 0x2000 output bytes, research/f2save.py; repacked the same way)
  herosave.bin   <Gender type="int">2</Gender> -> 1   (same length)
  Fable2PubInfo.xml  IsFemale='0' -> '1'              (UTF-16, same length)
  template.txt   gender=F

  python tools/make_female_template.py <male template dir> <out dir>
Both dirs hold template.txt and slot/. Whether the game accepts the result is
proven on the rig (T021a).
"""

import os
import shutil
import struct
import sys
import zlib

BLOCK = 0x2000


def unpack(data):
    pos, do, out = 0, zlib.decompressobj(), bytearray()
    while pos + 8 <= len(data):
        c, u = struct.unpack(">II", data[pos:pos + 8])
        if c == 0 or c > 0x100000:
            break
        out += do.decompress(data[pos + 8:pos + 8 + c])
        pos += 8 + c
    if pos != len(data):
        raise ValueError(f"trailing bytes at {pos}/{len(data)}")
    return bytes(out)


def pack(raw):
    co = zlib.compressobj(9)
    out = bytearray()
    for off in range(0, len(raw), BLOCK):
        chunk = raw[off:off + BLOCK]
        comp = co.compress(chunk)
        comp += co.flush(zlib.Z_SYNC_FLUSH if off + BLOCK < len(raw) else zlib.Z_FINISH)
        out += struct.pack(">II", len(comp), len(chunk)) + comp
    return bytes(out)


def lp(text):  # u32 BE length (with NUL) + text + NUL
    b = text.encode() + b"\0"
    return struct.pack(">I", len(b)) + b


def lp_nonul(text):  # u32 BE length (text only) + text: the child record's form
    b = text.encode()
    return struct.pack(">I", len(b)) + b


def main():
    src, dst = sys.argv[1:3]
    if os.path.exists(dst):
        sys.exit(f"{dst} exists; not overwriting")
    shutil.copytree(src, dst)
    slot = os.path.join(dst, "slot")

    raw = unpack(open(os.path.join(slot, "mainsave.bin"), "rb").read())
    edited = raw
    for old, new, enc in (("CreatureHero", "CreatureHeroFemale", lp),
                          ("CreatureHeroChild", "CreatureHeroFemaleChild", lp_nonul)):
        n = edited.count(enc(old))
        print(f"mainsave: {old!r} x{n} -> {new!r}")
        if n != 1:
            sys.exit(f"expected exactly one {old!r} record in mainsave, found {n}")
        edited = edited.replace(enc(old), enc(new))
    packed = pack(edited)
    if unpack(packed) != edited:
        sys.exit("repack does not round-trip")
    open(os.path.join(slot, "mainsave.bin"), "wb").write(packed)
    print(f"mainsave: {len(raw)} -> {len(edited)} bytes raw, packed {len(packed)}")

    hp = os.path.join(slot, "herosave.bin")
    h = open(hp, "rb").read()
    old, new = b'<Gender type="int">2</Gender>', b'<Gender type="int">1</Gender>'
    if h.count(old) != 1:
        sys.exit("herosave: Gender 2 not found once")
    open(hp, "wb").write(h.replace(old, new))
    print("herosave: Gender 2 -> 1")

    pp = os.path.join(slot, "Fable2PubInfo.xml")
    p = open(pp, "rb").read()
    old, new = "IsFemale='0'".encode("utf-16-be"), "IsFemale='1'".encode("utf-16-be")
    if p.count(old) != 1:
        sys.exit("Fable2PubInfo: IsFemale='0' not found once")
    open(pp, "wb").write(p.replace(old, new))
    print("Fable2PubInfo: IsFemale 0 -> 1")

    tp = os.path.join(dst, "template.txt")
    lines = [l for l in open(tp, encoding="utf-8").read().splitlines() if not l.startswith("gender=")]
    lines.insert(0, "gender=F")
    lines.append("made_by=tools/make_female_template.py from " + src)
    open(tp, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    print("template.txt: gender=F")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""bft_font.py - read Fable II .bft bitmap fonts (data/language/<lang>/fonts/*.bft).

Layout (big-endian; worked out from gui_normal.bft, 2026-10-24):
  0x00 u32 version (1), u32 line height (27), u32 ?(400), u32 0
  0x10 u8 ?(0x1A), u32 ?(0x17 = ascent?), u16 glyph count (307)
  0x19 glyph records, 22 bytes each: f32 u0, v0, u1, v1; s16 x offset, width, advance
  then u16 ?, u16 ?, 64 bytes (page table, 0x34 = none), then u16 per character from
  U+0020 up: glyph index, 0x3434 = no glyph
  then the texture: u32 0xFFFFFFFE, ..., u32 width, u32 height, ... and 4 bpp BC4 blocks
  (single-channel coverage), Xbox 360 tiled, 16-bit big-endian, at file end - w*h/2.

  python tools/bft_font.py <font.bft> <out.png>   writes the glyph atlas (white on alpha)
and the module gives load(path) -> (atlas RGBA bytes, w, h, glyphs{char: (u0,v0,u1,v1,xoff,w,adv)}, line_h).
"""

import struct
import sys
import zlib


def bc4_block(b):
    a0, a1 = b[0], b[1]
    bits = int.from_bytes(b[2:8], "little")
    pal = [a0, a1]
    if a0 > a1:
        pal += [((6 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        pal += [((4 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    return [pal[(bits >> (3 * i)) & 7] for i in range(16)]


def xg_tiled_offset(x, y, width_blocks, log2_bpb):
    # XGAddress2DTiledOffset (Xbox 360), in blocks; log2_bpb = log2(bytes per block)
    aligned_w = (width_blocks + 31) & ~31
    macro = ((y >> 5) * (aligned_w >> 5) + (x >> 5)) << (log2_bpb + 7)
    micro = ((x & 7) + ((y & 0xE) << 2)) << log2_bpb
    offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4)
    return (((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
            (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F)) >> log2_bpb


def decode_bc4(data, w, h, tiled=True, swap=True):
    if swap:
        data = bytes(b for i in range(0, len(data), 2) for b in (data[i + 1], data[i]))
    bw, bh = w // 4, h // 4
    out = bytearray(w * h)
    for by in range(bh):
        for bx in range(bw):
            idx = xg_tiled_offset(bx, by, bw, 3) if tiled else by * bw + bx
            blk = data[idx * 8: idx * 8 + 8]
            if len(blk) < 8:
                continue
            px = bc4_block(blk)
            for j in range(16):
                out[(by * 4 + j // 4) * w + bx * 4 + j % 4] = px[j]
    return bytes(out)


def load(path, tiled=True, swap=True):
    d = open(path, "rb").read()
    line_h = struct.unpack(">I", d[4:8])[0]
    count = struct.unpack(">H", d[23:25])[0]
    recs = []
    for i in range(count):
        o = 25 + 22 * i
        recs.append(struct.unpack(">4f3h", d[o:o + 22]))
    t = 25 + 22 * count + 4 + 64
    glyphs = {}
    ch = 0x20
    i = t
    while i + 2 <= len(d):
        v = struct.unpack(">H", d[i:i + 2])[0]
        if v != 0x3434 and v >= count:
            break
        if v != 0x3434:
            glyphs[chr(ch)] = recs[v]
        ch += 1
        i += 2
    hdr = i
    w, h = struct.unpack(">II", d[hdr + 16:hdr + 24])
    tex = d[len(d) - w * h // 2:]
    cov = decode_bc4(tex, w, h, tiled, swap)
    rgba = bytearray()
    for c in cov:
        rgba += bytes((255, 255, 255, c))
    return bytes(rgba), w, h, glyphs, line_h


def write_png(path, rgba, w, h):
    raw = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


if __name__ == "__main__":
    src, dst = sys.argv[1:3]
    for tiled in (True, False):
        for swap in (True, False):
            rgba, w, h, glyphs, lh = load(src, tiled, swap)
            # black background so the white glyphs show in any viewer
            flat = bytearray()
            for i in range(0, len(rgba), 4):
                a = rgba[i + 3]
                flat += bytes((a, a, a, 255))
            name = dst.replace(".png", f"_{'tiled' if tiled else 'linear'}_{'swap' if swap else 'noswap'}.png")
            write_png(name, bytes(flat), w, h)
            print(name, w, h, len(glyphs), "glyphs, line", lh)

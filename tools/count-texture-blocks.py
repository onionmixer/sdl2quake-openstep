#!/usr/bin/env python3
"""Q2-1 -- how many texture blocks does a level actually need?

The driver's arena allocator keeps a fixed number of blocks whatever their
size, and Q2-0 measured that it runs out at thirty.  Raising that number is
a one-constant change, so the only real question is what to raise it TO --
and guessing would be the same mistake as the byte calculation this replaces.

So this counts, from the data:

  world textures    the distinct miptex the faces actually reference
  lightmap blocks   GLQuake packs lightmaps into 128x128 sheets with a
                    skyline allocator (gl_rsurf.c AllocBlock).  That packer
                    is reproduced here, including which surfaces it skips:
                    sky and turbulent water carry no lightmap

What it cannot count from the data is the fixed overhead -- the character
set, the console background, two scrap sheets, the skin of every model the
level spawns, and sprite frames.  Those are named in entities and loaded on
demand, so they are added as a stated allowance rather than measured.

    python3 count-texture-blocks.py <pak> [pak...]
"""
import math
import struct
import sys

BLOCK_W = 128          # gl_rsurf.c
BLOCK_H = 128
TEX_SPECIAL = 1


def read_pak(path):
    with open(path, "rb") as f:
        magic, dirofs, dirlen = struct.unpack("<4sii", f.read(12))
        if magic != b"PACK":
            raise SystemExit("%s is not a pak" % path)
        f.seek(dirofs)
        raw = f.read(dirlen)
        f.seek(0)
        blob = f.read()
    out = []
    for i in range(dirlen // 64):
        rec = raw[i * 64:(i + 1) * 64]
        name = rec[:56].split(b"\0")[0].decode("latin1")
        pos, ln = struct.unpack("<ii", rec[56:])
        out.append((name, pos, ln))
    return blob, out


def alloc_block(allocated, w, h):
    """gl_rsurf.c's AllocBlock, reproduced.  Returns the sheet index."""
    for texnum in range(len(allocated)):
        row = allocated[texnum]
        best = BLOCK_H
        bestx = -1
        for i in range(BLOCK_W - w):
            best2 = 0
            ok = True
            for j in range(w):
                if row[i + j] >= best:
                    ok = False
                    break
                if row[i + j] > best2:
                    best2 = row[i + j]
            if ok:
                bestx = i
                best = best2
        if bestx < 0 or best + h > BLOCK_H:
            continue
        for i in range(w):
            row[bestx + i] = best + h
        return texnum
    return -1


def level(blob, pos, ln):
    hdr = blob[pos:pos + 4 + 15 * 8]
    lump = lambda i: struct.unpack_from("<ii", hdr, 4 + i * 8)
    eo, el = lump(0)
    if b"info_player_start" not in blob[pos + eo:pos + eo + el]:
        return None
    to, tl = lump(2)
    vo, vl = lump(3)
    tio, til = lump(6)
    fo, fl = lump(7)
    edo, edl = lump(12)
    so, sl = lump(13)

    verts = [struct.unpack_from("<3f", blob, pos + vo + i * 12)
             for i in range(vl // 12)]
    edges = [struct.unpack_from("<HH", blob, pos + edo + i * 4)
             for i in range(edl // 4)]
    surfedges = list(struct.unpack_from("<%di" % (sl // 4), blob, pos + so)) \
        if sl else []
    texinfo = []
    for i in range(til // 40):
        v = struct.unpack_from("<8f2i", blob, pos + tio + i * 40)
        texinfo.append((v[0:4], v[4:8], v[8], v[9]))

    names = []
    if tl:
        nmip = struct.unpack_from("<i", blob, pos + to)[0]
        for i in range(nmip):
            d = struct.unpack_from("<i", blob, pos + to + 4 + i * 4)[0]
            names.append(None if d < 0 else
                         blob[pos + to + d:pos + to + d + 16]
                         .split(b"\0")[0].decode("latin1").lower())

    used = set()
    sheets = [[0] * BLOCK_W for _ in range(64)]
    lm = 0
    for i in range(fl // 20):
        _, _, firstedge, numedges, ti = struct.unpack_from(
            "<hhihh", blob, pos + fo + i * 20)
        if ti < 0 or ti >= len(texinfo):
            continue
        vecs0, vecs1, miptex, flags = texinfo[ti]
        used.add(miptex)
        name = names[miptex] if 0 <= miptex < len(names) else ""
        # GL_BuildLightmaps skips sky and turbulent surfaces
        if name and (name.startswith("sky") or name.startswith("*")):
            continue
        mins = [1e9, 1e9]
        maxs = [-1e9, -1e9]
        for k in range(numedges):
            e = surfedges[firstedge + k]
            v = verts[edges[e][0]] if e >= 0 else verts[edges[-e][1]]
            for j, vec in enumerate((vecs0, vecs1)):
                val = v[0] * vec[0] + v[1] * vec[1] + v[2] * vec[2] + vec[3]
                mins[j] = min(mins[j], val)
                maxs[j] = max(maxs[j], val)
        w = int((math.ceil(maxs[0] / 16) - math.floor(mins[0] / 16))) + 1
        h = int((math.ceil(maxs[1] / 16) - math.floor(mins[1] / 16))) + 1
        if w <= 0 or h <= 0 or w > BLOCK_W or h > BLOCK_H:
            continue
        n = alloc_block(sheets, w, h)
        if n < 0:
            continue
        lm = max(lm, n + 1)
    return len(used), lm


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    rows = []
    for path in argv[1:]:
        blob, ents = read_pak(path)
        for name, pos, ln in ents:
            if not name.lower().endswith(".bsp"):
                continue
            r = level(blob, pos, ln)
            if r:
                rows.append((name.split("/")[-1][:-4], r[0], r[1]))
    rows.sort(key=lambda r: -(r[1] + r[2]))
    print("\n  %-12s %8s %10s %8s" % ("map", "world", "lightmaps", "total"))
    for n, w, l in rows:
        print("  %-12s %8d %10d %8d" % (n, w, l, w + l))
    worst = max(w + l for _, w, l in rows)
    print("\n  worst level                        %8d" % worst)
    print("  + charset, conback, two scraps            4")
    print("  + model skins and sprites (allowance)    40")
    print("  ----------------------------------------------")
    print("  a level's working set                  %8d" % (worst + 44))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

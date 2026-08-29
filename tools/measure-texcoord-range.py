#!/usr/bin/env python3
"""How far a Quake level's texture coordinates actually reach.

The driver refuses a textured triangle whose coordinate leaves +-8 texture
repeats at a pixel it draws, and on the card that check -- the exact one,
the one that looks at drawn pixels and nothing else -- accounts for 374 of
GLQuake's 457 texture refusals.  That says the level uses coordinates past
eight repeats.  It does not say how far past, and the difference decides
whether the answer is to widen the kernel's policy or to re-base the
coordinate in the back end.

So read it off the map rather than the card.  A face's texture coordinate
is s = dot(v, vecs[0]) + vecs[0][3] in texels, and dividing by the
texture's own width gives repeats.  Every vertex of every face, offline,
no hardware.
"""
import struct
import sys
import os

LUMPS = ["entities", "planes", "textures", "vertexes", "visibility",
         "nodes", "texinfo", "faces", "lighting", "clipnodes", "leafs",
         "marksurfaces", "edges", "surfedges", "models"]


def pak_entries(path):
    with open(path, "rb") as f:
        magic, dirofs, dirlen = struct.unpack("<4sii", f.read(12))
        if magic != b"PACK":
            return
        f.seek(dirofs)
        d = f.read(dirlen)
        for i in range(0, dirlen, 64):
            rec = d[i:i + 64]
            name = rec[:56].split(b"\0")[0].decode("latin-1")
            pos, ln = struct.unpack("<ii", rec[56:])
            f.seek(pos)
            yield name, f.read(ln)


def lump(data, i):
    return struct.unpack_from("<ii", data, 4 + i * 8)


def measure(data, name, out):
    if struct.unpack_from("<i", data, 0)[0] != 29:
        return
    tofs, _ = lump(data, LUMPS.index("textures"))
    tiofs, tilen = lump(data, LUMPS.index("texinfo"))
    vofs, vlen = lump(data, LUMPS.index("vertexes"))
    fofs, flen = lump(data, LUMPS.index("faces"))
    eofs, elen = lump(data, LUMPS.index("edges"))
    sofs, slen = lump(data, LUMPS.index("surfedges"))

    # texture sizes, by miptex index
    nummiptex = struct.unpack_from("<i", data, tofs)[0]
    size = []
    for i in range(nummiptex):
        d = struct.unpack_from("<i", data, tofs + 4 + i * 4)[0]
        if d < 0:
            size.append((64, 64))       # absent; the engine substitutes
            continue
        size.append(struct.unpack_from("<II", data, tofs + d + 16))

    texinfo = [struct.unpack_from("<8f2i", data, tiofs + i * 40)
               for i in range(tilen // 40)]
    verts = [struct.unpack_from("<3f", data, vofs + i * 12)
             for i in range(vlen // 12)]
    edges = [struct.unpack_from("<HH", data, eofs + i * 4)
             for i in range(elen // 4)]
    surfedges = list(struct.unpack_from("<%di" % (slen // 4), data, sofs))

    worst = 0.0
    faces_over = [0] * 12          # how many faces reach past 1,2,4,8,...
    total = 0
    for i in range(flen // 20):
        _, _, firstedge, numedges, ti = struct.unpack_from(
            "<hhihh", data, fofs + i * 20)
        if ti < 0 or ti >= len(texinfo):
            continue
        t = texinfo[ti]
        mip = t[9 - 1] if False else t[8]
        if mip < 0 or mip >= len(size):
            continue
        tw, th = size[mip]
        if tw == 0 or th == 0:
            continue
        reach = 0.0
        for k in range(numedges):
            se = surfedges[firstedge + k]
            vi = edges[abs(se)][0 if se >= 0 else 1]
            v = verts[vi]
            s = (v[0] * t[0] + v[1] * t[1] + v[2] * t[2] + t[3]) / tw
            tt = (v[0] * t[4] + v[1] * t[5] + v[2] * t[6] + t[7]) / th
            reach = max(reach, abs(s), abs(tt))
        total += 1
        worst = max(worst, reach)
        for b in range(12):
            if reach > (1 << b):
                faces_over[b] += 1
    if not total:
        return
    out.append((name, total, worst, faces_over))


def main(argv):
    paks = argv[1:]
    if not paks:
        print("usage: measure-texcoord-range.py <pak0.pak> [pak1.pak ...]")
        return 2
    out = []
    for p in paks:
        if not os.path.exists(p):
            print("missing: %s" % p)
            continue
        for name, data in pak_entries(p):
            if name.endswith(".bsp"):
                measure(data, name, out)
    if not out:
        print("no maps found")
        return 1
    out.sort(key=lambda r: -r[2])
    print("%-28s %6s %10s   %s" % ("map", "faces", "max repeat",
                                   "faces past 1/2/4/8/16/32/64/128"))
    grand = 0
    gover = [0] * 12
    for name, total, worst, over in out[:12]:
        print("%-28s %6d %10.2f   %s" %
              (name, total, worst, "/".join(str(over[b]) for b in range(8))))
    for name, total, worst, over in out:
        grand += total
        for b in range(12):
            gover[b] += over[b]
    print()
    print("%d maps, %d faces total" % (len(out), grand))
    for b in range(12):
        if gover[b]:
            print("  past %5d repeats: %7d faces  %6.2f%%"
                  % (1 << b, gover[b], 100.0 * gover[b] / grand))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

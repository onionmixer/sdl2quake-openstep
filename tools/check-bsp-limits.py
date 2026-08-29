#!/usr/bin/env python3
"""Q1-0 -- will the 1996 engine load these maps, or die on one?

WHY THIS EXISTS.  The plan's riskiest milestone is the first map load.  A
BSP can carry the right version number and still kill the engine: the loader
calls Sys_Error -- which on this engine means the process ends -- in eleven
distinct places, and the map that trips one of them is not knowable from the
file size or the version word.

So this reads the engine's own loader and asks the same questions of every
map, on the host, before anything is built.  Each check below names the
line in sdlquake's model.c that it mirrors, so a reader can go and disagree
with it.

WHAT IT DOES NOT DO.  It cannot tell you a map will look right.  The
software renderer drops surfaces when it runs out of edges or surfaces at
run time (NUMSTACKEDGES 2400, NUMSTACKSURFACES 800, counted into
r_outofedges/r_outofsurfaces), and that is a different question asked in
Q1-6 with the engine running.  This one answers only: does it LOAD.

    python3 check-bsp-limits.py <pak-or-bsp> [...]
"""
import struct
import sys
import os
import math

BSPVERSION = 29
ALIAS_VERSION = 6        # modelgen.h:45
SPRITE_VERSION = 1       # spritegn.h:61
MAX_LBM_HEIGHT = 480     # d_iface.h:25
MAXALIASVERTS = 2000     # r_local.h:224
TEX_SPECIAL = 1          # bspfile.h:178 -- sky or slime; no lightmap, no 256 limit
MAX_MAP_HULLS = 4        # bspfile.h:24

# lump index -> (name, on-disk record size).  A record size of None means the
# lump is bytes rather than an array, so the "funny lump size" check does not
# apply to it.
LUMPS = [
    ("entities",     None),
    ("planes",         20),
    ("textures",     None),
    ("vertexes",       12),
    ("visibility",   None),
    ("nodes",          24),
    ("texinfo",        40),
    ("faces",          20),
    ("lighting",     None),
    ("clipnodes",       8),
    ("leafs",          28),
    ("marksurfaces",    2),
    ("edges",           4),
    ("surfedges",       4),
    ("models",         64),
]


class Fail(Exception):
    """One thing the engine would refuse.  Carries the engine's own words."""


def pak_entries(path):
    with open(path, "rb") as f:
        magic, dirofs, dirlen = struct.unpack("<4sii", f.read(12))
        if magic != b"PACK":
            raise Fail("not a pak file")
        f.seek(dirofs)
        raw = f.read(dirlen)
    out = []
    for i in range(dirlen // 64):
        rec = raw[i * 64:(i + 1) * 64]
        name = rec[:56].split(b"\0")[0].decode("latin1")
        pos, ln = struct.unpack("<ii", rec[56:])
        out.append((name, pos, ln))
    return out


def check_bsp(data, name, problems):
    """Every fatal check Mod_LoadBrushModel can reach, in its own order."""
    def bad(msg):
        problems.append((name, msg))

    if len(data) < 4 + 15 * 8:
        bad("shorter than a BSP header")
        return

    version = struct.unpack_from("<i", data, 0)[0]
    if version != BSPVERSION:
        # model.c:1155
        bad("wrong version number (%i should be %i)" % (version, BSPVERSION))
        return

    lump = {}
    for i, (lname, recsize) in enumerate(LUMPS):
        ofs, ln = struct.unpack_from("<ii", data, 4 + i * 8)
        if ofs < 0 or ln < 0 or ofs + ln > len(data):
            bad("lump %s runs past the end of the file (%d+%d > %d)"
                % (lname, ofs, ln, len(data)))
            return
        if recsize is not None and ln % recsize:
            # model.c:563 and its nine siblings
            bad("MOD_LoadBmodel: funny lump size (%s)" % lname)
            return
        lump[lname] = (ofs, ln, 0 if recsize is None else ln // recsize)

    n_verts = lump["vertexes"][2]
    n_edges = lump["edges"][2]
    n_surfedges = lump["surfedges"][2]
    n_faces = lump["faces"][2]
    n_texinfo = lump["texinfo"][2]
    n_leafs = lump["leafs"][2]
    n_nodes = lump["nodes"][2]
    n_clipnodes = lump["clipnodes"][2]
    n_marks = lump["marksurfaces"][2]
    n_planes = lump["planes"][2]
    lightlen = lump["lighting"][1]

    # ---- textures: Mod_LoadTextures, model.c:388 and 439/467
    tofs, tlen, _ = lump["textures"]
    n_textures = 0
    texnames = []
    if tlen:
        nummiptex = struct.unpack_from("<i", data, tofs)[0]
        n_textures = nummiptex
        for i in range(nummiptex):
            dataofs = struct.unpack_from("<i", data, tofs + 4 + i * 4)[0]
            if dataofs == -1:
                texnames.append(None)
                continue
            base = tofs + dataofs
            if base + 40 > len(data):
                bad("miptex %d points outside the texture lump" % i)
                return
            mname = data[base:base + 16].split(b"\0")[0].decode("latin1")
            w, h = struct.unpack_from("<II", data, base + 16)
            if (w & 15) or (h & 15):
                bad("Texture %s is not 16 aligned (%ux%u)" % (mname, w, h))
                return
            texnames.append(mname)

        # ---- animating textures: Mod_LoadTextures, model.c:415-495
        #
        # The engine UPPERCASES the frame letter first --
        #     if (max >= 'a' && max <= 'z') max -= 'a' - 'A';
        # -- so "+ablink" is frame A of the alternate set, not an error.
        # An earlier version of this checker did not, and called nine of
        # LibreQuake's ten maps broken.  The maps were fine.
        #
        # Frames group by strcmp(name+2), so "+0butn" and "+abutn" are the
        # same animation, and every slot from 0 to the highest used must be
        # filled or the engine dies with "Missing frame".
        groups = {}
        for mname in texnames:
            if not mname or not mname.startswith("+") or len(mname) < 2:
                continue
            c = mname[1]
            if "a" <= c <= "z":
                c = chr(ord(c) - (ord("a") - ord("A")))
            base = mname[2:]
            g = groups.setdefault(base, {"anim": set(), "alt": set()})
            if "0" <= c <= "9":
                g["anim"].add(ord(c) - ord("0"))
            elif "A" <= c <= "J":
                g["alt"].add(ord(c) - ord("A"))
            else:
                bad("Bad animating texture %s" % mname)
                return
        for base, g in sorted(groups.items()):
            for which, slots in (("", g["anim"]), ("alt ", g["alt"])):
                if not slots:
                    continue
                for j in range(max(slots) + 1):
                    if j not in slots:
                        bad("Missing %sframe %d of +%s" % (which, j, base))
                        return

    # ---- texinfo: model.c:696
    tiofs = lump["texinfo"][0]
    texinfo = []
    for i in range(n_texinfo):
        vals = struct.unpack_from("<8f2i", data, tiofs + i * 40)
        miptex, flags = vals[8], vals[9]
        if n_textures and miptex >= n_textures:
            bad("miptex >= loadmodel->numtextures (%d >= %d)"
                % (miptex, n_textures))
            return
        texinfo.append((vals[0:4], vals[4:8], flags))

    # ---- marksurfaces: model.c:1054.  Read as a SIGNED short by the engine,
    # so a value above 32767 arrives negative and slips past its own check.
    mofs = lump["marksurfaces"][0]
    for i in range(n_marks):
        j = struct.unpack_from("<h", data, mofs + i * 2)[0]
        if j < 0:
            bad("marksurface %d reads as %d -- above 32767 and signed" % (i, j))
            return
        if j >= n_faces:
            bad("Mod_ParseMarksurfaces: bad surface number (%d >= %d)"
                % (j, n_faces))
            return

    # ---- edges and surfedges: no engine check at all, which is why a bad
    # one is a wild read rather than an error message.
    eofs = lump["edges"][0]
    edges = []
    for i in range(n_edges):
        a, b = struct.unpack_from("<HH", data, eofs + i * 4)
        if a >= n_verts or b >= n_verts:
            bad("edge %d names vertex %d/%d of %d" % (i, a, b, n_verts))
            return
        edges.append((a, b))
    sofs = lump["surfedges"][0]
    surfedges = list(struct.unpack_from("<%di" % n_surfedges, data, sofs)) \
        if n_surfedges else []
    for i, e in enumerate(surfedges):
        if e == 0 or abs(e) >= n_edges:
            bad("surfedge %d is %d, outside 1..%d" % (i, e, n_edges - 1))
            return

    # ---- vertexes, for the extent computation below
    vofs = lump["vertexes"][0]
    verts = [struct.unpack_from("<3f", data, vofs + i * 12) for i in range(n_verts)]

    # ---- faces, and CalcSurfaceExtents: model.c:756, the classic one
    fofs = lump["faces"][0]
    worst = 0
    for i in range(n_faces):
        planenum, side, firstedge, numedges, ti = struct.unpack_from(
            "<hhihh", data, fofs + i * 20)
        lightofs = struct.unpack_from("<i", data, fofs + i * 20 + 16)[0]
        if planenum < 0 or planenum >= n_planes:
            bad("face %d names plane %d of %d" % (i, planenum, n_planes))
            return
        if ti < 0 or ti >= n_texinfo:
            bad("face %d names texinfo %d of %d" % (i, ti, n_texinfo))
            return
        if firstedge < 0 or firstedge + numedges > n_surfedges:
            bad("face %d spans surfedges %d..%d of %d"
                % (i, firstedge, firstedge + numedges, n_surfedges))
            return
        if lightofs != -1 and lightofs >= lightlen:
            bad("face %d lightofs %d beyond the %d-byte lighting lump"
                % (i, lightofs, lightlen))
            return

        vecs0, vecs1, flags = texinfo[ti]
        mins = [999999.0, 999999.0]
        maxs = [-99999.0, -99999.0]
        for k in range(numedges):
            e = surfedges[firstedge + k]
            v = verts[edges[e][0]] if e >= 0 else verts[edges[-e][1]]
            for j, vec in enumerate((vecs0, vecs1)):
                val = v[0] * vec[0] + v[1] * vec[1] + v[2] * vec[2] + vec[3]
                if val < mins[j]:
                    mins[j] = val
                if val > maxs[j]:
                    maxs[j] = val
        for j in range(2):
            ext = (math.ceil(maxs[j] / 16) - math.floor(mins[j] / 16)) * 16
            if ext > worst and not (flags & TEX_SPECIAL):
                worst = ext
            if not (flags & TEX_SPECIAL) and ext > 256:
                bad("Bad surface extents (face %d, extent %d > 256)" % (i, ext))
                return

    # ---- nodes, leafs, clipnodes, submodels: range only.  The engine does
    # not check these, so an out-of-range child is a wild pointer later.
    nofs = lump["nodes"][0]
    for i in range(n_nodes):
        pl, c0, c1 = struct.unpack_from("<ihh", data, nofs + i * 24)
        for c in (c0, c1):
            idx = c if c >= 0 else -1 - c
            limit = n_nodes if c >= 0 else n_leafs
            if idx >= limit:
                bad("node %d child %d is out of range" % (i, c))
                return
    cofs = lump["clipnodes"][0]
    for i in range(n_clipnodes):
        pl, c0, c1 = struct.unpack_from("<ihh", data, cofs + i * 8)
        for c in (c0, c1):
            if c >= 0 and c >= n_clipnodes:
                bad("clipnode %d child %d is out of range" % (i, c))
                return
    lofs = lump["leafs"][0]
    for i in range(n_leafs):
        # firstmarksurface sits at 20, not 24: contents(4) visofs(4)
        # mins[3](6) maxs[3](6).  Reading it four bytes late made one map
        # look as though it referenced mark 65280.
        fm, nm = struct.unpack_from("<HH", data, lofs + i * 28 + 20)
        if fm + nm > n_marks:
            bad("leaf %d marks %d..%d of %d" % (i, fm, fm + nm, n_marks))
            return
    mdofs = lump["models"][0]
    for i in range(lump["models"][2]):
        head = struct.unpack_from("<%di" % MAX_MAP_HULLS, data, mdofs + i * 64 + 36)
        ff, nf = struct.unpack_from("<ii", data, mdofs + i * 64 + 56)
        if ff < 0 or ff + nf > n_faces:
            bad("submodel %d spans faces %d..%d of %d" % (i, ff, ff + nf, n_faces))
            return

    return {"leafs": n_leafs, "faces": n_faces, "clipnodes": n_clipnodes,
            "marks": n_marks, "edges": n_edges, "textures": n_textures,
            "worst_extent": worst}


def check_alias(data, name, problems):
    """Mod_LoadAliasModel, model.c:1464-1609.  A .mdl that trips one of
    these kills the engine at precache time, which looks exactly like a bad
    map to anyone watching."""
    def bad(msg):
        problems.append((name, msg))

    if len(data) < 84:
        bad("shorter than an alias header")
        return
    ident, version = struct.unpack_from("<ii", data, 0)
    if ident != 0x4F504449:                      # "IDPO"
        bad("not an alias model (ident %08x)" % ident)
        return
    if version != ALIAS_VERSION:
        bad("wrong version number (%i should be %i)" % (version, ALIAS_VERSION))
        return
    (numskins, skinwidth, skinheight, numverts,
     numtris, numframes) = struct.unpack_from("<6i", data, 48)
    if skinheight > MAX_LBM_HEIGHT:
        bad("skin taller than %d (%d)" % (MAX_LBM_HEIGHT, skinheight))
    if numverts <= 0:
        bad("no vertices")
    if numverts > MAXALIASVERTS:
        bad("too many vertices (%d > %d)" % (numverts, MAXALIASVERTS))
    if numtris <= 0:
        bad("no triangles")
    if skinwidth & 3:
        bad("skinwidth not multiple of 4 (%d)" % skinwidth)
    if numskins < 1:
        bad("Invalid # of skins: %d" % numskins)
    if numframes < 1:
        bad("Invalid # of frames: %d" % numframes)
    return {"verts": numverts, "tris": numtris, "frames": numframes,
            "skin": (skinwidth, skinheight)}


def check_sprite(data, name, problems):
    """Mod_LoadSpriteModel, model.c:1792-1819."""
    def bad(msg):
        problems.append((name, msg))

    if len(data) < 36:
        bad("shorter than a sprite header")
        return
    ident, version = struct.unpack_from("<ii", data, 0)
    if ident != 0x50534449:                      # "IDSP"
        bad("not a sprite (ident %08x)" % ident)
        return
    if version != SPRITE_VERSION:
        bad("wrong version number (%i should be %i)" % (version, SPRITE_VERSION))
        return
    numframes = struct.unpack_from("<i", data, 24)[0]
    if numframes < 1:
        bad("Invalid # of frames: %d" % numframes)
    return {"frames": numframes}


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    problems = []
    rows = []
    others = {"mdl": 0, "spr": 0}
    for path in argv[1:]:
        if path.lower().endswith(".pak"):
            entries = pak_entries(path)
            with open(path, "rb") as f:
                blob = f.read()
            for name, pos, ln in entries:
                low = name.lower()
                if low.endswith(".bsp"):
                    info = check_bsp(blob[pos:pos + ln], name, problems)
                    if info:
                        rows.append((name, ln, info))
                elif low.endswith(".mdl"):
                    if check_alias(blob[pos:pos + ln], name, problems):
                        others["mdl"] += 1
                elif low.endswith(".spr"):
                    if check_sprite(blob[pos:pos + ln], name, problems):
                        others["spr"] += 1
        else:
            with open(path, "rb") as f:
                blob = f.read()
            info = check_bsp(blob, os.path.basename(path), problems)
            if info:
                rows.append((os.path.basename(path), len(blob), info))

    rows.sort(key=lambda r: -r[2]["leafs"])
    print("\n  %-20s %6s %6s %6s %7s %8s" %
          ("map", "MB", "leafs", "faces", "marksrf", "extent"))
    for name, ln, c in rows:
        print("  %-20s %6.2f %6d %6d %7d %8d" %
              (name.split("/")[-1], ln / 1048576.0, c["leafs"], c["faces"],
               c["marks"], c["worst_extent"]))
    print("\n  %d maps, %d models and %d sprites checked"
          % (len(rows), others["mdl"], others["spr"]))
    if problems:
        print("\n  the engine would refuse %d of them:\n" % len(problems))
        for name, msg in problems:
            print("    %-22s %s" % (name.split("/")[-1], msg))
        print("\nQ1_0_BSP_PRECHECK=fail")
        return 1
    print("\n  the widest surface extent is %d; the engine's limit is 256"
          % max((r[2]["worst_extent"] for r in rows), default=0))
    print("\nQ1_0_BSP_PRECHECK=pass")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

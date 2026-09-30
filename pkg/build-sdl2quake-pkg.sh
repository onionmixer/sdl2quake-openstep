#!/bin/sh
# Build the OPENSTEP Installer package for the engine binaries.
#
#   sh .../pkg/build-sdl2quake-pkg.sh [source-root] [outdir] [bindir]
#
# Runs ON the target: the package tool is OPENSTEP's, and the payload is
# i386 machine code.  The binaries are whatever the two build scripts
# last wrote -- build them first, this only packages.
set -e
SRC="${1:-/ndrv/openstep-quake}"
OUT="${2:-/tmp/pkgout}"
BIN="${3:-/usr/local/nxbuild/bin}"
NAME=sdl2quake
PKGTOOL=/NextAdmin/Installer.app/package

if [ ! -x "$PKGTOOL" ]; then
    echo "build-sdl2quake-pkg: $PKGTOOL not found (run on OPENSTEP)" >&2
    exit 1
fi
if [ "`/usr/bin/arch`" != i386 ]; then
    echo "build-sdl2quake-pkg: the payload is i386; package it on i386" >&2
    exit 1
fi
for f in "$BIN/squake" "$BIN/glquake" "$BIN/glquake_radeon" "$SRC/README.md" "$SRC/LICENSE" \
         "$SRC/pkg/$NAME.info"; do
    if [ ! -r "$f" ]; then
        echo "build-sdl2quake-pkg: missing input: $f" >&2
        exit 1
    fi
done

# The GL binary must be the ACCELERATED link, which is distinguishable
# from the stock one the same way the driver's own package tells its
# archives apart: by the hook symbols only libGL_mga carries.
hooks=`nm "$BIN/glquake" | grep OSMGAMesaHook | wc -l`
if [ "$hooks" -lt 1 ]; then
    echo "build-sdl2quake-pkg: $BIN/glquake carries no accel hooks --" >&2
    echo "build-sdl2quake-pkg: that is glquake_sw's link, not glquake's" >&2
    exit 1
fi

#
# glquake_radeon (1.3): the same engine linked against libGL_radeon.a through
# build/build-glquake.sh ACCEL=radeon.  Told apart the same way: the radeon
# back end's own symbols are in it and the Matrox hook is not, and glquake is
# the other way round -- so the two cannot be swapped by a misplaced copy.
#
n=`nm "$BIN/glquake_radeon" | grep OSRDNMesaHook | wc -l`
if [ "$n" -lt 1 ]; then
    echo "build-sdl2quake-pkg: $BIN/glquake_radeon carries no radeon back end" >&2
    exit 1
fi
n=`nm "$BIN/glquake_radeon" | grep 'T _OSMGAMesaHook' | wc -l`
if [ "$n" -gt 0 ]; then
    echo "build-sdl2quake-pkg: $BIN/glquake_radeon defines the Matrox hook" >&2
    exit 1
fi
n=`nm "$BIN/glquake" | grep 'T _OSRDNMesa' | wc -l`
if [ "$n" -gt 0 ]; then
    echo "build-sdl2quake-pkg: $BIN/glquake defines radeon symbols" >&2
    exit 1
fi

#
# 1.4: every binary must carry the SDL2 openstep.5 audio backend as released:
# SoundKit calls on one NSThread, submissions not waited for.  Two earlier
# builds of openstep.5 were each wrong in one of those -- the first made the
# calls on SDL's cthread audio thread and glquake died in 3 runs of 30 ("sent
# to freed object"); the second waited for every submission and the device
# ran dry twice as often (openstep-sdl20 docs/PLAN_RELEASE_OPENSTEP5.md 13,
# 15-19).  Only the released backend's close report says a submission time
# is "the queueing only", so that phrase is what is looked for.  strings, not
# grep: grep goes silent at the first NUL.  This does not tell a released
# library from a hand-built one with the same source; the install check does.
#
for b in squake glquake glquake_radeon; do
    n=`/bin/strings "$BIN/$b" | grep 'the queueing only' | wc -l`
    if [ "$n" -lt 1 ]; then
        echo "build-sdl2quake-pkg: $BIN/$b is not linked against the released SDL2 openstep.5 audio backend" >&2
        exit 1
    fi
done

STAGEPARENT=/tmp/_sdl2quakepkg
STAGE="$STAGEPARENT/p"
rm -rf "$STAGEPARENT" "$OUT/$NAME.pkg"
/bin/mkdirs "$STAGE/docs"

cp "$BIN/squake"  "$STAGE/squake"
# 1.3: each GL binary is named for its card.  The build still writes
# glquake (build/build-glquake.sh, the test scripts); only the installed name
# changes, so nobody runs the Matrox build on a Radeon machine by its
# generic name.
cp "$BIN/glquake" "$STAGE/glquake_g450"
cp "$BIN/glquake_radeon" "$STAGE/glquake_radeon"
chmod 555 "$STAGE/squake" "$STAGE/glquake_g450" "$STAGE/glquake_radeon"
cp "$SRC/README.md" "$STAGE/docs/README-sdl2quake.md"
cp "$SRC/LICENSE"   "$STAGE/docs/COPYING-sdl2quake"

long=`( cd "$STAGE" && find . -print ) | awk 'length($0) >= 100' | wc -l`
if [ "$long" -gt 0 ]; then
    echo "build-sdl2quake-pkg: $long payload paths reach installer_tar's" >&2
    echo "build-sdl2quake-pkg: silent 100-char limit" >&2
    exit 1
fi

test -d "$OUT" || /bin/mkdirs "$OUT"
"$PKGTOOL" "$STAGE" "$SRC/pkg/$NAME.info" -d "$OUT" < /dev/null
echo "build-sdl2quake-pkg: PASS $OUT/$NAME.pkg"

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
for f in "$BIN/squake" "$BIN/glquake" "$SRC/README.md" "$SRC/LICENSE" \
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

STAGEPARENT=/tmp/_sdl2quakepkg
STAGE="$STAGEPARENT/p"
rm -rf "$STAGEPARENT" "$OUT/$NAME.pkg"
/bin/mkdirs "$STAGE/docs"

cp "$BIN/squake"  "$STAGE/squake"
cp "$BIN/glquake" "$STAGE/glquake"
chmod 555 "$STAGE/squake" "$STAGE/glquake"
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

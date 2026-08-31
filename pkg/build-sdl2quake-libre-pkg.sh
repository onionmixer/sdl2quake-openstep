#!/bin/sh
# Build the OPENSTEP Installer package for LibreQuake's data.
#
#   sh .../pkg/build-sdl2quake-libre-pkg.sh [source-root] [outdir] [datadir]
#
# Runs ON the target for the package tool alone -- the payload is data and
# carries no architecture.  datadir must hold LibreQuake's id1 directory
# (pak0.pak, pak1.pak, docs/), taken from a LibreQuake release; this
# repository does not carry the data.
set -e
SRC="${1:-/ndrv/openstep-quake}"
OUT="${2:-/tmp/pkgout}"
DATA="${3:-/ndrv/scratch/quake/upload}"
NAME=sdl2quake-libre
PKGTOOL=/NextAdmin/Installer.app/package

if [ ! -x "$PKGTOOL" ]; then
    echo "build-sdl2quake-libre-pkg: $PKGTOOL not found (run on OPENSTEP)" >&2
    exit 1
fi
for f in "$DATA/id1/pak0.pak" "$DATA/id1/pak1.pak" \
         "$SRC/pkg/$NAME.info"; do
    if [ ! -r "$f" ]; then
        echo "build-sdl2quake-libre-pkg: missing input: $f" >&2
        exit 1
    fi
done
if [ ! -d "$DATA/id1/docs" ]; then
    echo "build-sdl2quake-libre-pkg: $DATA/id1/docs missing -- the" >&2
    echo "build-sdl2quake-libre-pkg: credits travel WITH the data" >&2
    exit 1
fi

STAGEPARENT=/tmp/_sdl2quakelibrepkg
STAGE="$STAGEPARENT/p"
rm -rf "$STAGEPARENT" "$OUT/$NAME.pkg"
/bin/mkdirs "$STAGE"
cp -r "$DATA/id1" "$STAGE/id1"

# LibreQuake 0.09 ships this sound as slmbrn2.wav; the ENGINE asks for
# slimbrn2.wav from C code (pr_cmds.c:1792), which no data file can
# override.  A loose alias beside the paks satisfies the lookup -- pak
# entries and loose files share one namespace and nothing else claims
# the name.  Guarded, so a fixed upstream release makes this a no-op.
if [ ! -f "$STAGE/id1/sound/player/slimbrn2.wav" ] && \
   [ -f "$STAGE/id1/sound/player/slmbrn2.wav" ]; then
    cp "$STAGE/id1/sound/player/slmbrn2.wav" \
       "$STAGE/id1/sound/player/slimbrn2.wav"
fi

long=`( cd "$STAGE" && find . -print ) | awk 'length($0) >= 100' | wc -l`
if [ "$long" -gt 0 ]; then
    echo "build-sdl2quake-libre-pkg: $long payload paths reach" >&2
    echo "build-sdl2quake-libre-pkg: installer_tar's silent 100-char limit" >&2
    exit 1
fi

test -d "$OUT" || /bin/mkdirs "$OUT"
"$PKGTOOL" "$STAGE" "$SRC/pkg/$NAME.info" -d "$OUT" < /dev/null
echo "build-sdl2quake-libre-pkg: PASS $OUT/$NAME.pkg"

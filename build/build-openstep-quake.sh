#!/bin/sh
# Build sdlquake for OPENSTEP, against our SDL2 port.  Q1-1.
#
#   sh /ndrv/openstep-quake/build/build-openstep-quake.sh
#
# Three stages, kept apart because they fail for different reasons:
#   1. the portable engine        -- 1996 C against gcc 2.7.2.1
#   2. the four platform files    -- SDL 1.2 against SDL2
#   3. the link                   -- whether the C twins of the assembly
#                                    really cover everything the engine calls
#
# id386 is off (USE_ASM is not defined), so every file takes its `#if !id386`
# branch.  That is not a fallback invented here: it is the path Quake shipped
# on Alpha, MIPS and PowerPC.  The 21 .S files DO assemble on this system --
# checked, 18 of 18 that we would ever build -- so this is a choice about
# where the time goes, not a limit of the toolchain.  At 640x480 the C span
# loop costs 1.18 ms and putting the frame on screen costs 41, so the
# assembly would buy back a few percent of a frame.
set -e
ROOT=${1:-/ndrv/openstep-quake}
SDLB=${2:-/tmp/SDL20/build/SDL-2.32.10-openstep}
MESA=${3:-/tmp/SDL20/mesa/Mesa-3.4.2}
OUT=${4:-/usr/local/nxbuild}
SRC=$ROOT/upstream/sdlquake
PORT=$ROOT/port/openstep
OBJ=/tmp/quake-obj
CFLAGS="-m486 -O -D__OPENSTEP__ -Dstricmp=strcasecmp -I$SRC -I$SDLB/include"

for need in "$SRC/quakedef.h" "$SDLB/libSDL2.a" "$MESA/lib/libGL.a"; do
    if [ ! -r "$need" ]; then
        echo "build-openstep-quake: missing $need" >&2
        exit 2
    fi
done
test -d $OBJ || mkdir $OBJ
test -d $OUT/bin || mkdir $OUT/bin

sh $ROOT/build/compile-core.sh $SRC $OBJ
if [ -s /tmp/quake-core-fails ]; then
    echo "build-openstep-quake: the engine core did not compile" >&2
    exit 1
fi

# The platform four.  cd_null comes from upstream unchanged -- SDL2 has no
# CD audio API at all, so there is nothing to port, only something to drop.
echo ""
echo "platform:"
rm -f $OBJ/cd_null.o
cc -c $CFLAGS $SRC/cd_null.c -o $OBJ/cd_null.o
echo "  ok    cd_null.c (upstream)"
for f in sys_sdl snd_sdl vid_sdl net_udp; do
    rm -f $OBJ/$f.o
    cc -c $CFLAGS $PORT/$f.c -o $OBJ/$f.o
    echo "  ok    $f.c (port)"
done

echo ""
echo "link:"
rm -f $OUT/bin/squake
cc -m486 -o $OUT/bin/squake $OBJ/*.o \
    $SDLB/libSDL2.a $MESA/lib/libGL.a -lm \
    -framework AppKit -framework Foundation -framework SoundKit
csh -f /tmp/SDL20/src/port/openstep/fix-macho-i486-subtype.csh $OUT/bin/squake
echo ""
echo "QUAKE_BUILD=pass $OUT/bin/squake"

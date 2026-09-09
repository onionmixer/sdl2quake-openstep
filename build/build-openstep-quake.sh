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
SDLB=${2:-/LocalDeveloper}
MESA=${3:-/LocalDeveloper}
OUT=${4:-/usr/local/nxbuild}
SRC=$ROOT/upstream/sdlquake
PORT=$ROOT/port/openstep
OBJ=/tmp/quake-obj

#
# AN INSTALLED PREFIX OR A BUILD TREE -- both, because they are different
# shapes and the default is now the installed one.
#
# A build tree keeps libSDL2.a and include/ side by side; the Installer
# packages put the library under Libraries/ and the headers under
# Headers/SDL2/.  Linking a build tree while believing you linked the
# package is how a measurement was once taken against a library that had
# been superseded hours earlier, so the choice is made here, once, from
# what is actually on disk, and printed.
#
if [ -r "$SDLB/Libraries/libSDL2.a" ]; then
    SDL_LIB=$SDLB/Libraries/libSDL2.a
    SDL_INC="-I$SDLB/Headers/SDL2 -I$SDLB/Headers"
else
    SDL_LIB=$SDLB/libSDL2.a
    SDL_INC="-I$SDLB/include"
fi
if [ -r "$MESA/Libraries/libGL.a" ]; then
    GL_LIB=$MESA/Libraries/libGL.a
else
    GL_LIB=$MESA/lib/libGL.a
fi
echo "build-openstep-quake: SDL  $SDL_LIB"
echo "build-openstep-quake: Mesa $GL_LIB"

CFLAGS="-m486 -O -D__OPENSTEP__ -Dstricmp=strcasecmp -I$SRC $SDL_INC"

for need in "$SRC/quakedef.h" "$SDL_LIB" "$GL_LIB"; do
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

# The platform files.  in_sdl.c is shared with the GL build -- Quake's habit
# is a key table per video backend, and this tree builds two engines, so the
# input lives once and each backend hands it the window it made.
#
# cd_null comes from upstream unchanged -- SDL2 has no
# CD audio API at all, so there is nothing to port, only something to drop.
echo ""
echo "platform:"
rm -f $OBJ/cd_null.o
cc -c $CFLAGS $SRC/cd_null.c -o $OBJ/cd_null.o
echo "  ok    cd_null.c (upstream)"
for f in sys_sdl snd_sdl vid_sdl net_udp in_sdl; do
    rm -f $OBJ/$f.o
    cc -c $CFLAGS $PORT/$f.c -o $OBJ/$f.o
    echo "  ok    $f.c (port)"
done

echo ""
echo "link:"
rm -f $OUT/bin/squake
cc -m486 -o $OUT/bin/squake $OBJ/*.o \
    $SDL_LIB $GL_LIB -lm \
    -framework AppKit -framework Foundation -framework SoundKit
csh -f /me/SDL20/src/port/openstep/fix-macho-i486-subtype.csh $OUT/bin/squake
echo ""
echo "QUAKE_BUILD=pass $OUT/bin/squake"

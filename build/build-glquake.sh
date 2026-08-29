#!/bin/sh
# Build GLQuake for OPENSTEP: the card draws, and the frame never crosses
# the bus.  Q2-2.
#
#   sh /ndrv/openstep-quake/build/build-glquake.sh
#
# The difference from build-openstep-quake.sh is the renderer.  Where the
# software build compiles d_* and r_*, this compiles gl_* -- eleven files
# that draw the same world with OpenGL -- and links the accelerated Mesa
# rather than the stock one.  r_part is in both: particles are the same
# code either way.
#
# The four assembly objects the upstream Makefile lists for this target
# (math, worlda, snd_mixa, sys_dosa) are not built, for the reason the
# software build does not build them: id386 is 0 and the C twins are the
# path Quake shipped on every non-x86 machine.
set -e
ROOT=${1:-/ndrv/openstep-quake}
SDLB=${2:-/me/SDL20/build/SDL-2.32.10-openstep}
MGA=${3:-/ndrv/openstep-matrox-remade}
OUT=${4:-/usr/local/nxbuild}
SRC=$ROOT/upstream/sdlquake
PORT=$ROOT/port/openstep
OBJ=/tmp/glquake-obj
#
# A debug build, when one is wanted.  gdb is on the target and it is the
# only way to see what the back end hands the kernel from inside the
# process that hands it; the kernel side cannot be debugged at all, which
# is why it counts instead.  Separate objects and a separate binary so the
# optimised one is never quietly replaced by a slower one.
#
#   DEBUG=1 sh build-glquake.sh
#
if [ "${DEBUG:-0}" = "1" ]; then
    OPT="-g"
    OBJ=/tmp/glquake-obj-g
    BINSUFFIX=_g
else
    OPT="-O"
    BINSUFFIX=""
fi
CFLAGS="-m486 $OPT -DGLQUAKE -D__OPENSTEP__ -Dstricmp=strcasecmp -I$SRC \
 -I$SDLB/include -I$SDLB/src/video/openstep -I$MGA/mesa -I$MGA/hw3d \
 -I$MGA/build/mesa/include"

for need in "$SRC/glquake.h" "$SDLB/libSDL2.a" "$MGA/build/mesa/libGL_mga.a"; do
    if [ ! -r "$need" ]; then
        echo "build-glquake: missing $need" >&2
        exit 2
    fi
done
test -d $OBJ || mkdir $OBJ
test -d $OUT/bin || mkdir $OUT/bin

# The GLQUAKE_OBJS list from Makefile.linuxi386, minus the platform files
# and minus the four assembly ones.
#
# nonintel is NOT here, and was, briefly.  It is in the software build's list
# and not in this one, because it includes r_local.h and d_local.h -- the
# software renderer's own headers -- and the GL build has no espan_t.  Adding
# it because the name sounded like a portability fallback was a guess, and
# the upstream list already said otherwise.
CORE="cl_demo cl_input cl_main cl_parse cl_tent chase cmd common console
crc cvar gl_draw gl_mesh gl_model gl_refrag gl_rlight gl_rmain
gl_rsurf gl_screen gl_test gl_warp host host_cmd keys menu mathlib
net_dgrm net_loop net_main net_vcr net_bsd pr_cmds pr_edict
pr_exec r_part sbar sv_main sv_phys sv_move sv_user zone view wad world
snd_dma snd_mem snd_mix"

ok=0
bad=0
for f in $CORE; do
    rm -f $OBJ/$f.o
    # NOT bare: under set -e a failing cc would end the script here, before
    # the report below, and the reason would sit unread in a file.  That
    # happened once and cost two round trips to the machine.
    cc -c $CFLAGS $SRC/$f.c -o $OBJ/$f.o 2> /tmp/glq-err || echo "  (cc failed)"
    if [ -f $OBJ/$f.o ]; then
        ok=`expr $ok + 1`
    else
        bad=`expr $bad + 1`
        echo "  FAIL  $f.c"
        sed -n '1,4p' /tmp/glq-err | sed 's/^/          /'
    fi
done
echo "GLQUAKE_CORE_COMPILE ok=$ok fail=$bad"
if [ $bad -gt 0 ]; then exit 1; fi

echo ""
echo "platform:"
rm -f $OBJ/cd_null.o
cc -c $CFLAGS $SRC/cd_null.c -o $OBJ/cd_null.o
echo "  ok    cd_null.c (upstream)"
for f in sys_sdl snd_sdl net_udp in_sdl gl_vidsdl gl_rmisc; do
    rm -f $OBJ/$f.o
    cc -c $CFLAGS $PORT/$f.c -o $OBJ/$f.o
    echo "  ok    $f.c (port)"
done

echo ""
echo "link:"
rm -f $OUT/bin/glquake$BINSUFFIX
cc -m486 $OPT -o $OUT/bin/glquake$BINSUFFIX $OBJ/*.o \
    $SDLB/libSDL2.a $MGA/build/mesa/libGL_mga.a -lm \
    -framework AppKit -framework Foundation -framework SoundKit
csh -f /me/SDL20/src/port/openstep/fix-macho-i486-subtype.csh $OUT/bin/glquake$BINSUFFIX
#
# The control.  Same engine, same backend, same data -- only the library
# differs.  A picture that is wrong in both is ours; a picture that is wrong
# only in the accelerated one belongs to the card's path.  Every demo pair in
# this workspace exists for the same reason.
#
if [ -r "$MGA/build/mesa/libGL.a" ]; then
    # OUTSIDE $OBJ on purpose: the list below is built from $OBJ/*.o, and an
    # object left in that directory is picked up again by the glob -- which
    # linked two copies of the video backend and every global in it twice.
    rm -f /tmp/glq-plain-vid.o $OUT/bin/glquake_sw$BINSUFFIX
    cc -c $CFLAGS -DOSMGA_GLQUAKE_PLAIN $PORT/gl_vidsdl.c -o /tmp/glq-plain-vid.o
    rm -f /tmp/glq-plain-objs
    for o in $OBJ/*.o; do
        if [ "$o" = "$OBJ/gl_vidsdl.o" ]; then
            :
        else
            echo "$o" >> /tmp/glq-plain-objs
        fi
    done
    cc -m486 $OPT -o $OUT/bin/glquake_sw$BINSUFFIX `cat /tmp/glq-plain-objs` /tmp/glq-plain-vid.o \
        $SDLB/libSDL2.a $MGA/build/mesa/libGL.a -lm \
        -framework AppKit -framework Foundation -framework SoundKit
    csh -f /me/SDL20/src/port/openstep/fix-macho-i486-subtype.csh $OUT/bin/glquake_sw
    echo "  control: $OUT/bin/glquake_sw (stock Mesa)"
else
    echo "  control SKIPPED: no stock libGL.a"
fi

echo ""
echo "GLQUAKE_BUILD=pass $OUT/bin/glquake$BINSUFFIX"

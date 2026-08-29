#!/bin/sh
# Q1-1, first half: does the ENGINE's portable C compile on OPENSTEP?
#
#   sh /ndrv/openstep-quake/build/compile-core.sh
#
# WHY THE CORE ALONE, FIRST.  Two very different kinds of problem are waiting
# in this port: the 1996 C that gcc 2.7.2.1 may not like, and the SDL 1.2 API
# that SDL2 no longer has.  Compiling the portable engine by itself separates
# them -- whatever breaks here is the compiler's complaint about Quake, with
# no SDL in the picture at all.
#
# id386 IS DELIBERATELY OFF.  It becomes 1 only when USE_ASM is defined, and
# it is not defined here, so every file takes its `#if !id386` branch -- the
# C twin of the hand-written assembly.  That path is not a fallback anyone
# invented for this port: it is how Quake shipped on Alpha, MIPS and
# PowerPC, and every symbol the assembly exports was checked to have a C
# definition before this script was written.
#
# The platform four -- video, sound, CD and system -- are NOT here.  They are
# the SDL2 work, and they come second on purpose.
set -e
SRC=${1:-/ndrv/openstep-quake/upstream/sdlquake}
OBJ=${2:-/tmp/quake-obj}
CFLAGS="-m486 -O -Dstricmp=strcasecmp"

if [ ! -r "$SRC/quakedef.h" ]; then
    echo "compile-core: no engine source at $SRC" >&2
    exit 2
fi
test -d $OBJ || mkdir $OBJ

# The squake object list from Makefile.linuxi386, minus the platform files
# (cd_linux sys_linux vid_svgalib snd_linux, and net_udp -- which is platform
# code too, and this port has its own) and minus d_copy, which exists only as
# assembly and is called only by the DOS video backends.
CORE="cl_demo cl_input cl_main cl_parse cl_tent chase cmd common console
crc cvar draw d_edge d_fill d_init d_modech d_part d_polyse d_scan d_sky
d_sprite d_surf d_vars d_zpoint host host_cmd keys menu mathlib model
net_dgrm net_loop net_main net_vcr net_bsd nonintel pr_cmds
pr_edict pr_exec r_aclip r_alias r_bsp r_light r_draw r_efrag r_edge
r_misc r_main r_sky r_sprite r_surf r_part r_vars screen sbar sv_main
sv_phys sv_move sv_user zone view wad world snd_dma snd_mem snd_mix"

ok=0
bad=0
rm -f /tmp/quake-core-fails
touch /tmp/quake-core-fails
for f in $CORE; do
    if [ ! -r "$SRC/$f.c" ]; then
        echo "  MISSING  $f.c"
        bad=`expr $bad + 1`
        echo "$f missing" >> /tmp/quake-core-fails
        continue
    fi
    rm -f $OBJ/$f.o
    cc -c $CFLAGS -I$SRC $SRC/$f.c -o $OBJ/$f.o 2> /tmp/quake-cc-err
    if [ -f $OBJ/$f.o ]; then
        ok=`expr $ok + 1`
    else
        bad=`expr $bad + 1`
        echo "  FAIL  $f.c"
        sed -n '1,4p' /tmp/quake-cc-err | sed 's/^/          /'
        echo "$f" >> /tmp/quake-core-fails
    fi
done

echo ""
echo "QUAKE_CORE_COMPILE ok=$ok fail=$bad"

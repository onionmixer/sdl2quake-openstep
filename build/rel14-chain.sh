#!/bin/sh
# sdl2quake 1.4 on the target, AFTER SDL2 openstep.5 is installed: all three
# engines relinked against the INSTALLED libSDL2.a (openstep-sdl20
# docs/PLAN_RELEASE_OPENSTEP5.md 5).  glquake against Matrox 1.4's
# libGL_mga.a, glquake_radeon against the released libGL_radeon.a (790662776).
step() { echo "=== $1"; shift; "$@"; rc=$?; echo "=== rc=$rc"; if [ $rc != 0 ]; then echo "CHAIN FAIL"; exit $rc; fi; }
P=/tmp/_q14pfx
O=/tmp/_q14out
N=/tmp/_q14neg
RLIB=/ndrv/openstep-radeon9250/build/m1b/790662776/libGL_radeon.a
Q=/ndrv/openstep-quake
cd /tmp
grep '^Version' /NextLibrary/Receipts/OpenStepSDL2Libraries.pkg/OpenStepSDL2Libraries.info
if grep '^Version 2.32.10-openstep.5$' /NextLibrary/Receipts/OpenStepSDL2Libraries.pkg/OpenStepSDL2Libraries.info > /dev/null; then :; else
    echo "SDL2 openstep.5 is not installed"; echo "CHAIN FAIL"; exit 1; fi
# the gate must refuse the installed binaries (the previous 1.4 build,
# linked against an openstep.5 build that was not released)
rm -rf $N; mkdir $N
cp /usr/local/quake/squake $N/squake; cp /usr/local/quake/glquake_g450 $N/glquake; cp /usr/local/quake/glquake_radeon $N/glquake_radeon
rm -rf /tmp/_q14negout; mkdir /tmp/_q14negout
if sh $Q/pkg/build-sdl2quake-pkg.sh $Q /tmp/_q14negout $N > /tmp/_q14neg.log 2>&1; then
    cat /tmp/_q14neg.log; echo "the SDL2 gate ACCEPTED the installed binaries"; echo "CHAIN FAIL"; exit 1; fi
grep 'released SDL2 openstep.5 audio backend' /tmp/_q14neg.log || { cat /tmp/_q14neg.log; echo "refused for another reason"; echo "CHAIN FAIL"; exit 1; }
rm -rf $N /tmp/_q14negout
rm -rf $P $O
mkdir $P $P/Libraries $O $O/bin
cp /LocalDeveloper/Libraries/libSDL2.a /LocalDeveloper/Libraries/libGL.a $P/Libraries/
cp /ndrv/openstep-matrox-remade/build/mesa/libGL_mga.a $P/Libraries/
ranlib $P/Libraries/libSDL2.a $P/Libraries/libGL.a $P/Libraries/libGL_mga.a
ln -s /LocalDeveloper/Headers $P/Headers
step squake sh $Q/build/build-openstep-quake.sh $Q $P /LocalDeveloper $O
step glquake-matrox sh $Q/build/build-glquake.sh $Q $P /ndrv/openstep-matrox-remade $O
ACCEL=radeon; RDN_LIB=$RLIB; export ACCEL RDN_LIB
step glquake-radeon sh $Q/build/build-glquake.sh $Q $P /ndrv/openstep-matrox-remade $O
ACCEL=; export ACCEL
/usr/bin/sum $O/bin/squake $O/bin/glquake $O/bin/glquake_radeon
rm -rf /usr/local/rel1/pkgout-q14; mkdir /usr/local/rel1/pkgout-q14
step pkg sh $Q/pkg/build-sdl2quake-pkg.sh $Q /usr/local/rel1/pkgout-q14 $O/bin
grep '^Version' /usr/local/rel1/pkgout-q14/sdl2quake.pkg/sdl2quake.info
cp $O/bin/squake $O/bin/glquake $O/bin/glquake_radeon /ndrv/_ndrv_scratch/sdl5/
cp /LocalDeveloper/Libraries/libSDL2.a /ndrv/_ndrv_scratch/sdl5/libSDL2-installed.a
( cd /usr/local/rel1/pkgout-q14 && tar cf - sdl2quake.pkg ) > $Q/build/release-pkgs/sdl2quake.pkg.tar
sync
echo "CHAIN PASS"

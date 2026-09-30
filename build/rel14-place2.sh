#!/bin/sh
# sdl2quake 1.4 (SoundKit-thread SDL2) into /me/packages/quake; the first 1.4 to old/quake-1.4-v1.
P=/me/packages
N=/usr/local/rel1/pkgout-q14/sdl2quake.pkg
O=$P/old/quake-1.4-v1
fail() { echo "PLACE FAIL $*"; exit 1; }
[ -d $O ] && fail "$O exists"
grep '^Version 1.4$' $N/sdl2quake.info > /dev/null || fail "new is not 1.4"
grep '^Version 1.4$' $P/quake/sdl2quake.pkg/sdl2quake.info > /dev/null || fail "placed is not 1.4 (v1)"
mkdir $O || fail mkdir
mv $P/quake/sdl2quake.pkg $O/ || fail mv
cp -r $N $P/quake/ || fail cp
bad=0
for f in `ls $N`; do cmp -s $N/$f $P/quake/sdl2quake.pkg/$f || { echo "DIFFERS $f"; bad=1; }; done
grep '^Version' $P/quake/sdl2quake.pkg/sdl2quake.info $P/quake/sdl2quake-libre.pkg/*.info
ls $O
[ $bad = 0 ] && echo "PLACE PASS" || echo "PLACE FAIL"

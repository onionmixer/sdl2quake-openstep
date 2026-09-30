#!/bin/sh
# sdl2quake 1.4, payload text only (README and .info name radeon 1.1): the
# package again from the binaries already installed and judged.  Installs nothing.
step() { echo "=== $1"; shift; "$@"; rc=$?; echo "=== rc=$rc"; if [ $rc != 0 ]; then echo "CHAIN FAIL"; exit $rc; fi; }
for e in 6966:/ndrv/openstep-quake/README.md 1063:/ndrv/openstep-quake/pkg/sdl2quake.info 4376:/ndrv/openstep-quake/pkg/build-sdl2quake-pkg.sh ; do
    want=`echo $e | sed 's/:.*//'`; f=`echo $e | sed 's/^[0-9]*://'`
    have=`wc -c < $f`
    if [ $have -ne $want ]; then echo "NFS STALE $f host $want target $have"; echo "CHAIN FAIL"; exit 1; fi
done
echo "nfs sizes agree"
X=/ndrv/_ndrv_scratch/sdl5
O=/tmp/_q14doc
rm -rf $O; mkdir $O $O/bin
cp $X/squake $X/glquake $X/glquake_radeon $O/bin/ && chmod 755 $O/bin/*
a=`/usr/bin/sum $O/bin/squake | awk '{print $1, $2}'`;         b=`/usr/bin/sum /usr/local/quake/squake | awk '{print $1, $2}'`
c=`/usr/bin/sum $O/bin/glquake | awk '{print $1, $2}'`;        d=`/usr/bin/sum /usr/local/quake/glquake_g450 | awk '{print $1, $2}'`
e=`/usr/bin/sum $O/bin/glquake_radeon | awk '{print $1, $2}'`; f=`/usr/bin/sum /usr/local/quake/glquake_radeon | awk '{print $1, $2}'`
echo "squake $a / installed $b; glquake $c / g450 $d; radeon $e / $f"
[ "$a" = "$b" ] && [ "$c" = "$d" ] && [ "$e" = "$f" ] || { echo "not the installed binaries"; echo "CHAIN FAIL"; exit 1; }
rm -rf /usr/local/rel1/pkgout-q14doc; mkdir /usr/local/rel1/pkgout-q14doc
step pkg sh /ndrv/openstep-quake/pkg/build-sdl2quake-pkg.sh /ndrv/openstep-quake /usr/local/rel1/pkgout-q14doc $O/bin
( cd /usr/local/rel1/pkgout-q14doc && tar cf - sdl2quake.pkg ) > /ndrv/openstep-quake/build/release-pkgs/sdl2quake.pkg.tar
P=/me/packages; OLD=$P/old/quake-1.4-predoc
[ -d $OLD ] && { echo "$OLD exists"; echo "CHAIN FAIL"; exit 1; }
mkdir $OLD && mv $P/quake/sdl2quake.pkg $OLD/ && cp -r /usr/local/rel1/pkgout-q14doc/sdl2quake.pkg $P/quake/ || { echo "CHAIN FAIL place"; exit 1; }
for g in `ls /usr/local/rel1/pkgout-q14doc/sdl2quake.pkg`; do cmp -s /usr/local/rel1/pkgout-q14doc/sdl2quake.pkg/$g $P/quake/sdl2quake.pkg/$g || { echo "DIFFERS $g"; echo "CHAIN FAIL"; exit 1; }; done
grep '^Version' $P/quake/sdl2quake.pkg/sdl2quake.info
sync
echo "CHAIN PASS"

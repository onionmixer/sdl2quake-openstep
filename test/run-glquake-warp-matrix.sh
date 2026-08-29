#!/bin/sh
# What the refusals look like with the WARP tier on and with it off.
#
# WARP takes a primitive before the trapezoid builder ever sees it
# (OpenStepMGAMesaHook.c around 2529), and its absence from the environment
# does NOT mean off -- the backend falls back to the Configure setting, which
# is on.  So every earlier measurement in this workspace was taken with WARP
# on without saying so, and the refusal distribution may be about what WARP
# declined rather than about what the application draws.
#
#   sh /ndrv/openstep-quake/test/run-glquake-warp-matrix.sh [seconds]
SECS=${1:-45}
PROBE=/usr/local/nxbuild/bin/mga-stats-probe
BIN=/usr/local/nxbuild/bin/glquake
cd /usr/local/quake || exit 1
for W in 0 1; do
    echo "########## OSMGA_MESA_WARP=$W"
    echo "--- before"
    $PROBE sites 2> /dev/null | grep -v lookup
    OSMGA_MESA_WARP=$W; export OSMGA_MESA_WARP
    $BIN -basedir /usr/local/quake -width 640 -height 480 \
         > /tmp/glq-warp$W.log 2>&1 &
    PID=$!
    sleep $SECS
    kill $PID 2> /dev/null
    sleep 2
    kill -9 $PID 2> /dev/null
    echo "--- after"
    $PROBE sites 2> /dev/null | grep -v lookup
    echo "--- revoked: `grep -c revoked /tmp/glq-warp$W.log`"
done

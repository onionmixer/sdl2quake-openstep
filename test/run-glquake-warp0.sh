#!/bin/sh
# One run with WARP explicitly off, counters differenced.  WARP off because
# the tier changes the primitive set and the batch shape, so the effect of a
# change to the trapezoid builder can only be read with it out of the way.
SECS=${1:-45}
TAG=${2:-run}
PROBE=/usr/local/nxbuild/bin/mga-stats-probe
cd /usr/local/quake || exit 1
echo "--- before ($TAG)"
$PROBE sites 2> /dev/null | grep -v lookup
OSMGA_MESA_WARP=${3:-0}; export OSMGA_MESA_WARP
OSMGA_STATS_EVERY=${5:-1}; export OSMGA_STATS_EVERY   # dump every N frames
/usr/local/nxbuild/bin/glquake -basedir /usr/local/quake -width 640 -height 480 \
    > /tmp/glq-$TAG.log 2>&1 &
PID=$!
sleep $SECS
kill $PID 2> /dev/null
sleep ${4:-20}   # grace for a frame to finish and the graceful quit to dump
kill -9 $PID 2> /dev/null
echo "--- after ($TAG)"
$PROBE sites 2> /dev/null | grep -v lookup
echo "--- revoked: `grep -c revoked /tmp/glq-$TAG.log`"

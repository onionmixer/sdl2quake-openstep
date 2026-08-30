#!/bin/sh
# Start glquake and leave it running, pid in /tmp/glq.pid, so that gdb can be
# attached from another session.  $2 = dump every N FRAMES; $3 = WARP (default 1).
BIN=${1:-/usr/local/nxbuild/bin/glquake_g}
cd /usr/local/quake || exit 1
OSMGA_MESA_WARP=${3:-1}; export OSMGA_MESA_WARP
OSMGA_STATS_EVERY=${2:-1}; export OSMGA_STATS_EVERY

$BIN -basedir /usr/local/quake -width 640 -height 480 > /tmp/glq-bg.log 2>&1 &
echo $! > /tmp/glq.pid
echo "started pid `cat /tmp/glq.pid`"

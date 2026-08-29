#!/bin/sh
# timedemo is the measurement GLQuake already has: it plays a recorded demo
# as fast as it can and prints frames, seconds and fps.  Poll for that line
# rather than sleeping a fixed span, so a fast run costs what it costs.
#   sh /ndrv/openstep-quake/test/run-glquake-timedemo.sh <binary> [w] [h] [cap]
B=${1:-glquake}
W=${2:-640}
H=${3:-480}
CAP=${4:-300}
BIN=/usr/local/nxbuild/bin/$B
LOG=/tmp/td-$B.log
cd /usr/local/quake || exit 1
[ -x $BIN ] || { echo "$B: not built"; exit 1; }
rm -f $LOG
$BIN -basedir /usr/local/quake -width $W -height $H \
     +timedemo demo1 > $LOG 2>&1 &
PID=$!
N=0
while [ $N -lt $CAP ]; do
    if grep -i frames $LOG > /dev/null 2>&1; then break; fi
    sleep 5
    N=`expr $N + 5`
done
echo "########## $B at ${W}x${H}, waited ${N}s"
grep -i frames $LOG | tail -3
echo "  revoked: `grep -c revoked $LOG`"
kill $PID 2> /dev/null
sleep 2
kill -9 $PID 2> /dev/null

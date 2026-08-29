#!/bin/sh
# Run GLQuake for a fixed span and keep what it said.  Run ON the target.
#   sh /ndrv/openstep-quake/test/run-glquake-measure.sh [seconds] [width] [height]
SECS=${1:-30}
W=${2:-640}
H=${3:-480}
LOG=/tmp/glq-measure.log
BIN=/usr/local/nxbuild/bin/glquake
cd /usr/local/quake || exit 1
rm -f $LOG
$BIN -basedir /usr/local/quake -width $W -height $H > $LOG 2>&1 &
PID=$!
sleep $SECS
kill $PID 2> /dev/null
sleep 2
kill -9 $PID 2> /dev/null
echo "########## ran ${SECS}s at ${W}x${H}, pid $PID"
echo "--- revoked ---"
grep -c revoked $LOG
echo "--- refus/verdict lines ---"
grep -i verdict $LOG | tail -5
echo "--- last 20 lines ---"
tail -20 $LOG

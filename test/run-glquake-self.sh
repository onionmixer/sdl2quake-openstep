#!/bin/sh
# A run that ends ITSELF.  No signal, no gdb: the process quits through
# Host_Shutdown after N frames, or -- if a frame never ends -- exits from the
# software path when the deadline passes.  Both close the device the ordinary
# way.  Run ON the target (through gcdsd, so the window server is reachable).
#   sh run-glquake-self.sh [binary] [frames] [deadline-secs] [warp]
BIN=${1:-/usr/local/nxbuild/bin/glquake}
FRAMES=${2:-100}
DEADLINE=${3:-35}
EXTRA=${5:-}
cd /usr/local/quake || exit 1
rm -f core
OSMGA_MESA_WARP=${4:-0}; export OSMGA_MESA_WARP
OSMGA_STATS_EVERY=1; export OSMGA_STATS_EVERY
OSMGA_QUIT_AFTER_FRAMES=$FRAMES; export OSMGA_QUIT_AFTER_FRAMES
OSMGA_MESA_DEADLINE_SECS=$DEADLINE; export OSMGA_MESA_DEADLINE_SECS
# csh, for the core-size limit sh cannot set; a core is the only
# account a crash leaves of itself.
csh -f -c "limit coredumpsize unlimited; exec $BIN -basedir /usr/local/quake -width 640 -height 480 $EXTRA" > /tmp/glq-self.log 2>&1
echo "exit=$?"

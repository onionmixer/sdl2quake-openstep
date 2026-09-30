#!/bin/sh
# Repeat the installed glquake_radeon run and keep each exit status: does the
# 1.4 (SDL2 openstep.5, stream audio) run end early, and how?  ARM=stream|sound.
X=/ndrv/_ndrv_scratch/sdl5
ARM=${1:-stream}
N=${2:-6}
SDL_OPENSTEP_AUDIO_REPORT=1; export SDL_OPENSTEP_AUDIO_REPORT
if [ "$ARM" = sound ]; then SDL_OPENSTEP_AUDIO_API=sound; export SDL_OPENSTEP_AUDIO_API; fi
cd /usr/local/quake || exit 1
i=0
while [ $i -lt $N ]; do
    i=`expr $i + 1`
    rm -f /usr/local/quake/core
    out=`sh /ndrv/openstep-quake/test/run-glquake-self.sh /usr/local/quake/glquake_radeon 300 110 0 +map start 2>&1`
    ticks=`grep 'mgastats tick' /tmp/glq-self.log | wc -l`
    echo "$ARM run $i: $out ticks=$ticks"
    cp /tmp/glq-self.log $X/rep-$ARM-$i-`whoami`.log
    if [ -r /usr/local/quake/core ]; then mv /usr/local/quake/core $X/core-$ARM-$i; echo "  core saved"; fi
done
sync
echo "REP DONE"

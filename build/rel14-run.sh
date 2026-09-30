#!/bin/sh
# sdl2quake 1.4 installed-run check, as the invoking (ordinary) user, WITH
# sound: glquake_radeon 300 frames through the self-run harness, then squake
# for ~45 s, both with the SDL2 audio report on.
whoami
X=/ndrv/_ndrv_scratch/sdl5
SDL_OPENSTEP_AUDIO_REPORT=1; export SDL_OPENSTEP_AUDIO_REPORT
cd /usr/local/quake || exit 1
RDNMesaTime=1 RDNMesaTimeSplit=150 sh /ndrv/openstep-quake/test/run-glquake-self.sh /usr/local/quake/glquake_radeon 300 110 0 +map start > /dev/null 2>&1
echo "glquake rc=$?"
cp /tmp/glq-self.log $X/glq14.log
# squake has no frame limit: run it, wait, and end it by name (not $! --
# gcds's intermediate sh makes $! the wrong process).  [s] keeps this
# script's own ps line out of the match.
./squake -basedir /usr/local/quake -width 640 -height 480 +map start > $X/sq14.log 2>&1 &
sleep 45
p=`ps -ax | awk '/[s]quake -basedir/ {print $1}'`
echo "squake pid(s): $p"
[ -n "$p" ] && kill $p
sleep 5
p=`ps -ax | awk '/[s]quake -basedir/ {print $1}'`
[ -n "$p" ] && { echo "squake still running, KILL"; kill -9 $p; }
sync
echo "RUN DONE"

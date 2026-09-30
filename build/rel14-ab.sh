#!/bin/sh
# A/B for the loading-time audio gap: the same run as rel14-run.sh's
# glquake part, with the 1.3 binary (SDL2 openstep.4, per-sound path) first,
# then the installed 1.4 again.  Both as the invoking user, sound on.
X=/ndrv/_ndrv_scratch/sdl5
SDL_OPENSTEP_AUDIO_REPORT=1; export SDL_OPENSTEP_AUDIO_REPORT
cd /usr/local/quake || exit 1
sh /ndrv/openstep-quake/test/run-glquake-self.sh $X/glquake_radeon-1.3 300 110 0 +map start > /dev/null 2>&1
echo "1.3 rc=$?"; cp /tmp/glq-self.log $X/glq13-ab.log
sh /ndrv/openstep-quake/test/run-glquake-self.sh /usr/local/quake/glquake_radeon 300 110 0 +map start > /dev/null 2>&1
echo "1.4 rc=$?"; cp /tmp/glq-self.log $X/glq14-ab.log
sync
echo "AB DONE"

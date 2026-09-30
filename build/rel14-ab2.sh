#!/bin/sh
# A/B continued, alternating order: 1.4, 1.3, 1.3, 1.4.
X=/ndrv/_ndrv_scratch/sdl5
SDL_OPENSTEP_AUDIO_REPORT=1; export SDL_OPENSTEP_AUDIO_REPORT
cd /usr/local/quake || exit 1
i=0
for b in /usr/local/quake/glquake_radeon $X/glquake_radeon-1.3 $X/glquake_radeon-1.3 /usr/local/quake/glquake_radeon; do
    i=`expr $i + 1`
    sh /ndrv/openstep-quake/test/run-glquake-self.sh $b 300 110 0 +map start > /dev/null 2>&1
    echo "run $i $b rc=$?"; cp /tmp/glq-self.log $X/ab2-$i.log
done
sync
echo "AB2 DONE"

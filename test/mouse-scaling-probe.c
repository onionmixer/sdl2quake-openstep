/*
 * mouse-scaling-probe.c -- READ-ONLY look at the event system's mouse
 * scaling (the acceleration curve Preferences sets).  Q4 section 8:
 * before the game may set a linear table for the grab, know what is
 * there, that the API answers, and which library carries it.
 *
 *   cc -o /tmp/msp mouse-scaling-probe.c        (try bare first)
 *   /tmp/msp
 */
#include <stdio.h>
#include <drivers/event_status_driver.h>

int main(void)
{
    NXEventHandle h;
    NXMouseScaling s;
    int i;

    h = NXOpenEventStatus();
    if (h == 0) {
        printf("PROBE: NXOpenEventStatus returned 0\n");
        return 1;
    }
    NXGetMouseScaling(h, &s);
    printf("PROBE: numScaleLevels=%d\n", s.numScaleLevels);
    for (i = 0; i < s.numScaleLevels && i < NX_MAXMOUSESCALINGS; i++)
        printf("PROBE:   threshold=%d factor=%d\n",
               (int)s.scaleThresholds[i], (int)s.scaleFactors[i]);
    NXCloseEventStatus(h);
    printf("PROBE: done\n");
    return 0;
}

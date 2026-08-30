/*
 * mouse-flush-probe.m -- the fix in isolation.  The four-cell probe showed
 * a buffered PSsetmouse landing at the NEXT DPS round trip (our own
 * position query), wiping the hand's movement just before it is read.
 * Ten seconds of the broken cycle, then ten with PSWait() flushing the
 * warp immediately: the second half's reads should carry the hand.
 */
#import <AppKit/AppKit.h>
#import <AppKit/psopsNeXT.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSWindow *win;
    NSView *view;
    int i;

    [NSApplication sharedApplication];
    win = [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, 300, 200)
                                      styleMask:NSTitledWindowMask
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
    [win setTitle:@"mouse-flush-probe"];
    [win makeKeyAndOrderFront:nil];
    view = [win contentView];
    for (i = 0; i < 40; i++) {
        NSPoint evs;
        int flush = (i >= 20);

        evs = [win mouseLocationOutsideOfEventStream];   /* read FIRST */
        printf("MFP i=%02d flush=%d evs=%.0f,%.0f\n", i, flush, evs.x, evs.y);
        fflush(stdout);
        [view lockFocus];
        PSsetmouse(150.0, 100.0);
        [view unlockFocus];
        if (flush)
            PSWait();          /* the warp lands NOW, not at the next read */
        usleep(1000000);
    }
    [win close];
    [pool release];
    return 0;
}

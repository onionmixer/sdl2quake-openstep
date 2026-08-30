/*
 * mouse-live-probe.m -- which position channel tracks the HAND?
 * Q4 section 9: with mouse-moved events undelivered, and
 * mouseLocationOutsideOfEventStream apparently event-fed (374/400 polls
 * frozen at the warped centre during a steady sweep), compare, twice a
 * second for thirty seconds, with a visible window on screen:
 *   A  PScurrentmouse            -- straight from the Window Server
 *   B  mouseLocationOutsideOfEventStream
 * Launch through gcdsd (needs the window server).  READ-ONLY.
 */
#import <AppKit/AppKit.h>
#import <AppKit/psopsNeXT.h>   /* PSsetmouse lives here in this AppKit */
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSWindow *win;
    int i;

    [NSApplication sharedApplication];
    win = [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, 300, 100)
                                      styleMask:NSTitledWindowMask
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
    [win setTitle:@"mouse-live-probe"];
    [win makeKeyAndOrderFront:nil];
    for (i = 0; i < 60; i++) {
        float px = 0.0, py = 0.0;
        NSPoint b;

        PScurrentmouse([win windowNumber], &px, &py);
        b = [win mouseLocationOutsideOfEventStream];
        printf("MLP i=%02d ps=%.0f,%.0f evs=%.0f,%.0f\n",
               i, px, py, b.x, b.y);
        fflush(stdout);
        usleep(1000000);   /* one sample a second for a minute */
    }
    [win close];
    [pool release];
    return 0;
}

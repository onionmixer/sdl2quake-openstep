/*
 * mouse-channels-probe.m -- every candidate position channel, one sweep.
 * Q4 section 9, designed with the cross-review.  Per second, after
 * draining pending AppKit events (counting NSMouseMoved):
 *     evs   [NSWindow mouseLocationOutsideOfEventStream]
 *     ps0   PScurrentmouse(0)             -- global screen coordinates
 *     psw   PScurrentmouse(windowNumber)  -- window base coordinates
 *     mask  PScurrenteventmask(windowNumber), and its moved bit
 * Iterations 0-14 do NOT warp (are the channels live at all?).
 * Iterations 15-29 warp to the window centre first (does the warp wipe
 * the hand's movement from any channel?).
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
    int i, movedCount = 0;

    [NSApplication sharedApplication];
    win = [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, 300, 200)
                                      styleMask:NSTitledWindowMask
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
    [win setTitle:@"mouse-channels-probe"];
    [win setAcceptsMouseMovedEvents:YES];
    [win makeKeyAndOrderFront:nil];
    view = [win contentView];
    for (i = 0; i < 30; i++) {
        NSPoint evs;
        float p0x = -1.0, p0y = -1.0, pwx = -1.0, pwy = -1.0;
        int mask = 0;
        NSEvent *ev;
        int warp = (i >= 15);

        while ((ev = [NSApp nextEventMatchingMask:NSAnyEventMask
                                        untilDate:nil
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES]) != nil) {
            if ([ev type] == NSMouseMoved)
                movedCount++;
            [NSApp sendEvent:ev];
        }
        if (warp) {
            [view lockFocus];
            PSsetmouse(150.0, 100.0);
            [view unlockFocus];
        }
        evs = [win mouseLocationOutsideOfEventStream];
        PScurrentmouse(0, &p0x, &p0y);
        PScurrentmouse((int)[win windowNumber], &pwx, &pwy);
        PScurrenteventmask((int)[win windowNumber], &mask);
        printf("MCP i=%02d warp=%d evs=%.0f,%.0f ps0=%.0f,%.0f "
               "psw=%.0f,%.0f mask=%04x movedbit=%d accepts=%d moved=%d\n",
               i, warp, evs.x, evs.y, p0x, p0y, pwx, pwy,
               (unsigned)mask, (mask >> 5) & 1,
               (int)[win acceptsMouseMovedEvents], movedCount);
        fflush(stdout);
        usleep(1000000);
    }
    [win close];
    [pool release];
    return 0;
}

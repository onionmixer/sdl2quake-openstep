/*
 * mouse-accepts-probe.m -- does ASKING for mouse-moved events break the
 * live position queries?  Q4 section 9.  The earlier probes differ in
 * exactly one setup call: the one WITHOUT setAcceptsMouseMovedEvents:YES
 * tracked the hand on every channel; the one WITH it froze.  Four cells,
 * ten seconds each, reads always BEFORE any warp (the game's order):
 *     A  accepts=NO,  no warp        C  accepts=NO,  warp after read
 *     B  accepts=YES, no warp        D  accepts=YES, warp after read
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
    [win setTitle:@"mouse-accepts-probe"];
    [win makeKeyAndOrderFront:nil];
    view = [win contentView];
    for (i = 0; i < 40; i++) {
        NSPoint evs;
        float p0x = -1.0, p0y = -1.0;
        NSEvent *ev;
        int cell = i / 10;             /* 0=A 1=B 2=C 3=D */
        int accepts = (cell == 1 || cell == 3);
        int warp = (cell >= 2);

        [win setAcceptsMouseMovedEvents:(accepts ? YES : NO)];
        while ((ev = [NSApp nextEventMatchingMask:NSAnyEventMask
                                        untilDate:nil
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES]) != nil) {
            if ([ev type] == NSMouseMoved)
                movedCount++;
            [NSApp sendEvent:ev];
        }
        /* READ FIRST -- one second of hand movement lies behind us. */
        evs = [win mouseLocationOutsideOfEventStream];
        PScurrentmouse(0, &p0x, &p0y);
        printf("MAP i=%02d cell=%c evs=%.0f,%.0f ps0=%.0f,%.0f moved=%d\n",
               i, 'A' + cell, evs.x, evs.y, p0x, p0y, movedCount);
        fflush(stdout);
        if (warp) {
            [view lockFocus];
            PSsetmouse(150.0, 100.0);
            [view unlockFocus];
        }
        usleep(1000000);
    }
    [win close];
    [pool release];
    return 0;
}

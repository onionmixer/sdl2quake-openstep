/*
 * mouse-warp-probe.m -- does a buffered PSsetmouse land LATE and wipe the
 * hand's movement?  Q4 section 9.  Mimic the game's cycle: warp to a fixed
 * spot the way the SDL backend does (lockFocus / PSsetmouse / unlockFocus),
 * read the position right after, sleep a second while the user sweeps,
 * read again just before the next warp.  Odd iterations add PSWait() after
 * the warp -- if only those track the hand, the unflushed warp is the
 * thief.  READ-ONLY apart from moving the pointer to 350,300-ish.
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
    [win setTitle:@"mouse-warp-probe"];
    [win makeKeyAndOrderFront:nil];
    view = [win contentView];
    for (i = 0; i < 30; i++) {
        NSPoint a, b, c;
        int waited = (i & 1);

        a = [win mouseLocationOutsideOfEventStream];
        [view lockFocus];
        PSsetmouse(150.0, 100.0);          /* view-local, as the backend */
        [view unlockFocus];
        if (waited)
            PSWait();
        b = [win mouseLocationOutsideOfEventStream];
        usleep(1000000);
        c = [win mouseLocationOutsideOfEventStream];
        printf("MWP i=%02d wait=%d before=%.0f,%.0f after=%.0f,%.0f "
               "later=%.0f,%.0f\n", i, waited, a.x, a.y, b.x, b.y, c.x, c.y);
        fflush(stdout);
    }
    [win close];
    [pool release];
    return 0;
}

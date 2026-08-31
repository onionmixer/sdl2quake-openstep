/*
 * cursor-show.m -- unwind a stuck PShidecursor.  A killed fullscreen app
 * can leave the WindowServer's cursor-hide count above zero; the count is
 * global, so any connection may put it back.  Shows are counted against
 * hides and extra shows are no-ops, so eight of them restore any depth a
 * game reached without overshooting.  READ-ONLY otherwise: no windows.
 */
#import <AppKit/AppKit.h>
#import <AppKit/psopsNeXT.h>
#include <stdio.h>

int main(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    int i;

    [NSApplication sharedApplication];
    for (i = 0; i < 8; i++)
        PSshowcursor();
    PSWait();
    printf("cursor-show: eight shows flushed\n");
    [pool release];
    return 0;
}

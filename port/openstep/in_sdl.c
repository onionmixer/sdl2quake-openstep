/*
 * in_sdl.c -- SDL2 keyboard and mouse for OPENSTEP, shared by both engines
 *
 * NEW FILE, 2026-08-30, GPL-2.0-or-later with the rest of Quake.
 *
 * Quake's habit is one video file per target carrying its own input code,
 * which is why sdlquake has the same key table in vid_sdl.c, gl_vidlinux.c,
 * gl_vidlinuxglx.c and vid_win.c.  This port builds TWO engines from one
 * tree -- squake and glquake -- and copying two hundred lines between them
 * would mean a fix reaching one and not the other.  So the input lives once,
 * here, and each video backend hands it the window it made.
 *
 * The translation itself was written for this port in Q1 and is unchanged;
 * what is new is that it has one home.
 */
#include <stdio.h>
#include <string.h>
#include <drivers/event_status_driver.h>   /* NXSet/GetMouseScaling */

#include "SDL.h"
#include "quakedef.h"

/*
 * PSWait, declared by hand: the pswrap-generated wrappers have plain C
 * linkage, and this is a C file.  Why it is here at all: PSsetmouse is
 * BUFFERED -- the probe measured it landing at the next DPS round trip,
 * which was our own position query, so every pump read a pointer that
 * had just been yanked back to the centre and the hand's whole frame of
 * movement was erased (18/18 samples at distance zero).  Flushing right
 * after the warp lands it NOW; the same probe then carried the full
 * movement in 18/18 samples.  docs/Q4_GRAB_SOUND_PLAN.md section 9.
 */
extern void PSWait (void);
/*
 * The flickering square under the crosshair was the SOFTWARE CURSOR's
 * save-under: an invisible NSCursor image still makes the Window Server
 * back up and restore the pixels beneath the pointer with the CPU, and
 * those restores race the engine's drawing (the driver documents exactly
 * this collision).  PShidecursor stops the machinery itself; the flag
 * keeps hide and show strictly balanced, and the cursor comes back on
 * ungrab, on focus loss and at shutdown.
 */
extern void PShidecursor (void);
extern void PSshowcursor (void);
static int in_psCursorHidden;

static void
IN_PSCursor (int hide)
{
    if (hide && !in_psCursorHidden) {
        PShidecursor ();
        in_psCursorHidden = 1;
    } else if (!hide && in_psCursorHidden) {
        PSshowcursor ();
        /*
         * FLUSHED, for the same reason the warp is (see PSWait above):
         * the show is buffered, and the one place it matters most is
         * shutdown -- where the process exits before any round trip
         * would have carried it, the hide having long since landed.
         * That is a desktop with a working, invisible pointer.  Shows
         * are rare; the flush costs nothing measurable.
         */
        PSWait ();
        in_psCursorHidden = 0;
    }
}

static SDL_Window *in_window;

static qboolean mouse_avail;
static float    mouse_x, mouse_y;
static int      mouse_oldbuttonstate;

/*
 * THE GRAB.  AppKit delivers mouseMoved only while the pointer is inside
 * the key view; dragged events arrive from anywhere.  So an ungrabbed
 * pointer that leaves the window goes silent, which is why the view only
 * turned while a button was held.  Grabbed play hides the cursor and
 * recentres the pointer once per event pump; ungrabbed play gives the OS
 * cursor back and feeds the game no mouse at all.  Shift+Ctrl+G toggles,
 * and the window title says so (docs/Q4_GRAB_SOUND_PLAN.md).
 */
static int      in_grabbed = 1;

/*
 * THE ACCELERATION.  The event system multiplies pointer deltas through a
 * five-step table (measured live: thresholds 1/6/7/8/9 -> factors
 * 1/2/3/5/7), so the polled position is up to seven times the hand
 * movement and the view lurches across the steps.  While grabbed the
 * table is set to one explicit linear step and the user's table is put
 * back on release -- saved anew at every grab so a Preferences change
 * made between grabs is never overwritten, and only put back if the
 * table still holds our linear entry, so another writer's setting is
 * never clobbered either.  All of it is optional: a failed open just
 * means grabbed play keeps the desktop acceleration.
 */
static NXEventHandle  in_evh;          /* 0: event status unavailable */
static NXMouseScaling in_savedScaling;
static int            in_savedValid;
static int            in_linearOn;

static int
IN_ScalingIsLinear (const NXMouseScaling *s)
{
    return s->numScaleLevels == 1 &&
           s->scaleThresholds[0] == 1 && s->scaleFactors[0] == 1;
}

static void
IN_LinearScalingOn (void)
{
    NXMouseScaling lin, back;

    if (in_evh == 0 || in_linearOn)
        return;
    memset (&in_savedScaling, 0, sizeof (in_savedScaling));
    NXGetMouseScaling (in_evh, &in_savedScaling);
    if (in_savedScaling.numScaleLevels < 0 ||
        in_savedScaling.numScaleLevels > NX_MAXMOUSESCALINGS)
        return;                       /* the read said nothing usable */
    in_savedValid = 1;
    memset (&lin, 0, sizeof (lin));
    lin.numScaleLevels = 1;
    lin.scaleThresholds[0] = 1;
    lin.scaleFactors[0] = 1;
    NXSetMouseScaling (in_evh, &lin);
    /* The calls return void; the read-back is the only receipt. */
    memset (&back, 0, sizeof (back));
    NXGetMouseScaling (in_evh, &back);
    if (IN_ScalingIsLinear (&back))
        in_linearOn = 1;
    else
        in_savedValid = 0;            /* nothing changed, keep nothing */
}

static void
IN_LinearScalingOff (void)
{
    NXMouseScaling cur;

    if (in_evh == 0 || !in_linearOn)
        return;
    in_linearOn = 0;
    if (!in_savedValid)
        return;
    memset (&cur, 0, sizeof (cur));
    NXGetMouseScaling (in_evh, &cur);
    /* Only put ours back if the table still holds our linear entry --
     * a Preferences change or a second instance owns it otherwise. */
    if (IN_ScalingIsLinear (&cur))
        NXSetMouseScaling (in_evh, &in_savedScaling);
    in_savedValid = 0;
}
static int      in_hotkeyLatch;   /* a G whose down was consumed */
static int      in_hadFocus;
static char     in_titleBase[128];

static void
IN_ApplyGrab (void)
{
    char title[192];

    if (!in_window)
        return;
    if (mouse_avail && in_grabbed) {
        IN_LinearScalingOn ();
        SDL_ShowCursor (SDL_DISABLE);
        sprintf (title, "[Shift+Ctrl+G frees mouse] %s", in_titleBase);
    } else if (mouse_avail) {
        IN_LinearScalingOff ();       /* before the cursor is shown */
        IN_PSCursor (0);
        SDL_ShowCursor (SDL_ENABLE);
        sprintf (title, "[Shift+Ctrl+G grabs mouse] %s", in_titleBase);
    } else {
        IN_LinearScalingOff ();
        IN_PSCursor (0);
        SDL_ShowCursor (SDL_ENABLE);
        sprintf (title, "%s", in_titleBase);
    }
    SDL_SetWindowTitle (in_window, title);
}

static void
IN_ToggleGrab (void)
{
    int i;

    in_grabbed = !in_grabbed;
    /* No K_MOUSEn may stay held across the edge: the button state is
     * polled (IN_Commands), and an ungrabbed game stops polling. */
    for (i = 0; i < 3; i++)
        if (mouse_oldbuttonstate & (1 << i))
            Key_Event (K_MOUSE1 + i, false);
    mouse_oldbuttonstate = 0;
    mouse_x = mouse_y = 0.0;
    in_hadFocus = 0;         /* the next focused poll only recentres */
    IN_ApplyGrab ();
}

/* Each video backend calls this once, with the window it created.  The
 * mouse warp needs a window in SDL2, and neither backend can pass its own
 * static to the other. */
void
IN_SetWindow (SDL_Window *w)
{
    const char *t;

    in_window = w;
    if (in_evh == 0)
        in_evh = NXOpenEventStatus ();
    /* IN_Init ran before VID_Init made this window (host.c:885), so the
     * initial cursor and title land here, and the engine's own title --
     * "glquake" or "sdlquake" -- is kept underneath the hint. */
    t = w ? SDL_GetWindowTitle (w) : 0;
    if (t == 0) t = "";
    strncpy (in_titleBase, t, sizeof (in_titleBase) - 1);
    in_titleBase[sizeof (in_titleBase) - 1] = 0;
    in_hadFocus = 0;
    IN_ApplyGrab ();
}

/*
================
Sys_SendKeyEvents
================
*/

void Sys_SendKeyEvents(void)
{
    SDL_Event event;
    int sym, state;
    int modstate;

    while (SDL_PollEvent(&event))
    {
        switch (event.type) {

            case SDL_KEYDOWN:
            case SDL_KEYUP:
                sym = event.key.keysym.sym;
                state = event.key.state;
                /*
                 * Shift+Ctrl+G toggles the grab.  Judged on the EVENT's own
                 * modifier snapshot: this pump drains the whole native
                 * queue first, so SDL_GetModState() can already be past a
                 * Ctrl-up that came after this G went down.  The down is
                 * consumed, the latch consumes autorepeats and the matching
                 * up -- Quake never sees half a keystroke -- and a G that
                 * went down as an ordinary key still delivers its up.
                 */
                if (sym == SDLK_g) {
                    if (event.type == SDL_KEYDOWN &&
                        (event.key.keysym.mod & KMOD_SHIFT) != 0 &&
                        (event.key.keysym.mod & KMOD_CTRL) != 0) {
                        if (event.key.repeat == 0 && mouse_avail)
                            IN_ToggleGrab ();
                        in_hotkeyLatch = 1;
                        break;
                    }
                    if (event.type == SDL_KEYUP && in_hotkeyLatch) {
                        in_hotkeyLatch = 0;
                        break;
                    }
                }
                modstate = SDL_GetModState();
                switch(sym)
                {
                   case SDLK_DELETE: sym = K_DEL; break;
                   case SDLK_BACKSPACE: sym = K_BACKSPACE; break;
                   case SDLK_F1: sym = K_F1; break;
                   case SDLK_F2: sym = K_F2; break;
                   case SDLK_F3: sym = K_F3; break;
                   case SDLK_F4: sym = K_F4; break;
                   case SDLK_F5: sym = K_F5; break;
                   case SDLK_F6: sym = K_F6; break;
                   case SDLK_F7: sym = K_F7; break;
                   case SDLK_F8: sym = K_F8; break;
                   case SDLK_F9: sym = K_F9; break;
                   case SDLK_F10: sym = K_F10; break;
                   case SDLK_F11: sym = K_F11; break;
                   case SDLK_F12: sym = K_F12; break;
                   /* SDLK_BREAK is gone from SDL2; PAUSE alone covers it. */
                   case SDLK_PAUSE: sym = K_PAUSE; break;
                   case SDLK_UP: sym = K_UPARROW; break;
                   case SDLK_DOWN: sym = K_DOWNARROW; break;
                   case SDLK_RIGHT: sym = K_RIGHTARROW; break;
                   case SDLK_LEFT: sym = K_LEFTARROW; break;
                   case SDLK_INSERT: sym = K_INS; break;
                   case SDLK_HOME: sym = K_HOME; break;
                   case SDLK_END: sym = K_END; break;
                   case SDLK_PAGEUP: sym = K_PGUP; break;
                   case SDLK_PAGEDOWN: sym = K_PGDN; break;
                   case SDLK_RSHIFT:
                   case SDLK_LSHIFT: sym = K_SHIFT; break;
                   case SDLK_RCTRL:
                   case SDLK_LCTRL: sym = K_CTRL; break;
                   case SDLK_RALT:
                   case SDLK_LALT: sym = K_ALT; break;
                   /* The keypad names gained an underscore in SDL2:
                    * SDLK_KP0 became SDLK_KP_0, and so on through the set. */
                   case SDLK_KP_0:
                       if(modstate & KMOD_NUM) sym = K_INS;
                       else sym = SDLK_0;
                       break;
                   case SDLK_KP_1:
                       if(modstate & KMOD_NUM) sym = K_END;
                       else sym = SDLK_1;
                       break;
                   case SDLK_KP_2:
                       if(modstate & KMOD_NUM) sym = K_DOWNARROW;
                       else sym = SDLK_2;
                       break;
                   case SDLK_KP_3:
                       if(modstate & KMOD_NUM) sym = K_PGDN;
                       else sym = SDLK_3;
                       break;
                   case SDLK_KP_4:
                       if(modstate & KMOD_NUM) sym = K_LEFTARROW;
                       else sym = SDLK_4;
                       break;
                   case SDLK_KP_5: sym = SDLK_5; break;
                   case SDLK_KP_6:
                       if(modstate & KMOD_NUM) sym = K_RIGHTARROW;
                       else sym = SDLK_6;
                       break;
                   case SDLK_KP_7:
                       if(modstate & KMOD_NUM) sym = K_HOME;
                       else sym = SDLK_7;
                       break;
                   case SDLK_KP_8:
                       if(modstate & KMOD_NUM) sym = K_UPARROW;
                       else sym = SDLK_8;
                       break;
                   case SDLK_KP_9:
                       if(modstate & KMOD_NUM) sym = K_PGUP;
                       else sym = SDLK_9;
                       break;
                   case SDLK_KP_PERIOD:
                       if(modstate & KMOD_NUM) sym = K_DEL;
                       else sym = SDLK_PERIOD;
                       break;
                   case SDLK_KP_DIVIDE: sym = SDLK_SLASH; break;
                   case SDLK_KP_MULTIPLY: sym = SDLK_ASTERISK; break;
                   case SDLK_KP_MINUS: sym = SDLK_MINUS; break;
                   case SDLK_KP_PLUS: sym = SDLK_PLUS; break;
                   case SDLK_KP_ENTER: sym = SDLK_RETURN; break;
                   case SDLK_KP_EQUALS: sym = SDLK_EQUALS; break;
                }
                /* Anything still above 255 is a key this engine has no
                 * number for.  SDL2 sets bit 30 on its non-ASCII keycodes,
                 * so they land here exactly as SDL 1.2's did. */
                if(sym > 255) sym = 0;
                Key_Event(sym, state);
                break;

            case SDL_MOUSEMOTION:
                /*
                 * Not used for view rotation.  On this AppKit the moved
                 * events simply do not arrive while the pointer crosses the
                 * view (traced: 1,400 events, 1,397 of them the warp's own
                 * synthetics), so the rotation is POLLED from the current
                 * hardware position after the drain -- see below.  Dragged
                 * and button events still work the ordinary way.
                 */
                break;

            case SDL_QUIT:
                CL_Disconnect ();
                Host_ShutdownServer(false);
                Sys_Quit ();
                break;
            default:
                break;
        }
    }

    /*
     * THE POLLED GRAB.  Once per pump, while grabbed, keyboard-focused and
     * mouse-focused: read the CURRENT hardware position (the backend asks
     * mouseLocationOutsideOfEventStream, which needs no event flow), take
     * its distance from the window centre as this frame's hand movement,
     * and warp back to the centre.  SDL_GetGlobalMouseState returns GLOBAL
     * screen coordinates, so the window position is subtracted first; the
     * result is clamped to the window so an escaped pointer cannot
     * over-rotate.  The first focused poll only recentres -- there is no
     * guarantee the pointer started at the centre -- and losing focus, the
     * grab toggle and a new window all reset that reference.
     *
     * When the mouse focus is elsewhere (the pointer slipped out between
     * pumps and mouseExited ran) the poll is skipped for that pump: the
     * backend would report (0,0) with no focus window, and the warp below
     * still brings the pointer home for the next pump.
     */
    if (mouse_avail && in_grabbed && in_window != 0) {
        if ((SDL_GetWindowFlags (in_window) & SDL_WINDOW_INPUT_FOCUS) &&
            SDL_GetMouseFocus () == in_window) {
            int ww = 0, wh = 0, wx = 0, wy = 0, gx = 0, gy = 0;
            int px, py;

            SDL_GetWindowSize (in_window, &ww, &wh);
            SDL_GetWindowPosition (in_window, &wx, &wy);
            IN_PSCursor (1);          /* no save-under while we own it */
            if (ww > 0 && wh > 0) {
                SDL_GetGlobalMouseState (&gx, &gy);
                px = gx - wx;
                py = gy - wy;
                if (px < 0) px = 0;
                if (px >= ww) px = ww - 1;
                if (py < 0) py = 0;
                if (py >= wh) py = wh - 1;
                if (in_hadFocus) {
                    /*
                     * No amplifier.  The old *10 dated from the event path
                     * under desktop acceleration; on polled linear deltas
                     * it made ONE PIXEL two thirds of a degree, and slow
                     * aiming stepped like a robot.  A pixel is now the
                     * finest unit the engine sees (0.022 deg times
                     * sensitivity); speed is the player's sensitivity
                     * cvar, resolution is this.
                     */
                    mouse_x += (float)(px - ww / 2);
                    mouse_y += (float)(py - wh / 2);
                }
                SDL_WarpMouseInWindow (in_window, ww / 2, wh / 2);
                PSWait ();     /* see the declaration above */
                in_hadFocus = 1;
            }
        } else {
            IN_PSCursor (0);          /* another app owns the pointer */
            in_hadFocus = 0;
        }
    }
}

void IN_Init (void)
{
    if ( COM_CheckParm ("-nomouse") )
        return;
    mouse_x = mouse_y = 0.0;
    mouse_avail = 1;
}

void IN_Shutdown (void)
{
    mouse_avail = 0;
    IN_PSCursor (0);
    IN_LinearScalingOff ();
    if (in_evh != 0) {
        NXCloseEventStatus (in_evh);
        in_evh = 0;
    }
}

void IN_Commands (void)
{
    int i;
    int mouse_buttonstate;

    if (!mouse_avail || !in_grabbed) return;

    i = SDL_GetMouseState(NULL, NULL);
    /* Quake swaps the second and third buttons */
    mouse_buttonstate = (i & ~0x06) | ((i & 0x02)<<1) | ((i & 0x04)>>1);
    for (i=0 ; i<3 ; i++) {
        if ( (mouse_buttonstate & (1<<i)) && !(mouse_oldbuttonstate & (1<<i)) )
            Key_Event (K_MOUSE1 + i, true);

        if ( !(mouse_buttonstate & (1<<i)) && (mouse_oldbuttonstate & (1<<i)) )
            Key_Event (K_MOUSE1 + i, false);
    }
    mouse_oldbuttonstate = mouse_buttonstate;
}

void IN_Move (usercmd_t *cmd)
{
    if (!mouse_avail || !in_grabbed)
        return;

    mouse_x *= sensitivity.value;
    mouse_y *= sensitivity.value;

    if ( (in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1) ))
        cmd->sidemove += m_side.value * mouse_x;
    else
        cl.viewangles[YAW] -= m_yaw.value * mouse_x;
    if (in_mlook.state & 1)
        V_StopPitchDrift ();

    if ( (in_mlook.state & 1) && !(in_strafe.state & 1)) {
        cl.viewangles[PITCH] += m_pitch.value * mouse_y;
        if (cl.viewangles[PITCH] > 80)
            cl.viewangles[PITCH] = 80;
        if (cl.viewangles[PITCH] < -70)
            cl.viewangles[PITCH] = -70;
    } else {
        if ((in_strafe.state & 1) && noclip_anglehack)
            cmd->upmove -= m_forward.value * mouse_y;
        else
            cmd->forwardmove -= m_forward.value * mouse_y;
    }
    mouse_x = mouse_y = 0.0;
}

/*
================
Sys_ConsoleInput
================
*/
char *Sys_ConsoleInput (void)
{
    return 0;
}

void
Force_CenterView_f (void)
{
    cl.viewangles[PITCH] = 0;
}

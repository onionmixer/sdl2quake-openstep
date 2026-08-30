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

#include "SDL.h"
#include "quakedef.h"

static SDL_Window *in_window;

static qboolean mouse_avail;
static float    mouse_x, mouse_y;
static int      mouse_oldbuttonstate;

/* Each video backend calls this once, with the window it created.  The
 * mouse warp needs a window in SDL2, and neither backend can pass its own
 * static to the other. */
void
IN_SetWindow (SDL_Window *w)
{
    in_window = w;
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
    int warped = 0;

    while (SDL_PollEvent(&event))
    {
        switch (event.type) {

            case SDL_KEYDOWN:
            case SDL_KEYUP:
                sym = event.key.keysym.sym;
                state = event.key.state;
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
                if (!mouse_avail)
                    break;
                /*
                 * ACCUMULATED, where the original assigned.
                 *
                 * This function drains the queue once a frame, and the
                 * original kept only the LAST motion event's delta -- every
                 * earlier one in the same frame was thrown away.  AppKit
                 * delivers a mouseMoved for every step the pointer takes and
                 * a frame here takes long enough to collect dozens, so a
                 * whole sweep of the hand became one small step: the mouse
                 * looked dead, and the slower the frame, the deader.
                 *
                 * No test on the position before adding.  The original
                 * skipped an event that landed exactly on the centre, to
                 * ignore its own warp; but SDL2 resets its reference point
                 * when it warps, so the warp's synthetic event carries a
                 * zero delta and adds nothing -- while a REAL move that ends
                 * on the centre carries a real delta the old test threw away.
                 */
                mouse_x += event.motion.xrel*10;
                mouse_y += event.motion.yrel*10;
                /*
                 * Warp back to the centre once the pointer has strayed past a
                 * quarter of the window, measured in the WINDOW's pixels --
                 * event coordinates are the window's, and in the software
                 * build vid.width is the render size, which -fullscreen makes
                 * a different number.  At most once per drain: a slow frame
                 * can queue many events past the threshold, and each warp is
                 * a Window Server round trip that the next event undoes.
                 */
                if (!warped) {
                    int ww = 0, wh = 0;

                    SDL_GetWindowSize(in_window, &ww, &wh);
                    if (ww > 0 && wh > 0 &&
                        (event.motion.x < ww/2 - ww/4 ||
                         event.motion.x > ww/2 + ww/4 ||
                         event.motion.y < wh/2 - wh/4 ||
                         event.motion.y > wh/2 + wh/4)) {
                        /* SDL_WarpMouse -> SDL_WarpMouseInWindow: SDL2 warps
                         * within a named window rather than the screen.
                         * SDL_SetRelativeMouseMode would remove the warp
                         * entirely, but this backend has no native relative
                         * mode and would fall back to warping anyway. */
                        SDL_WarpMouseInWindow(in_window, ww/2, wh/2);
                        warped = 1;
                    }
                }
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
}

void IN_Commands (void)
{
    int i;
    int mouse_buttonstate;

    if (!mouse_avail) return;

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
    if (!mouse_avail)
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

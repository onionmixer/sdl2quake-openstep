/*
 * vid_sdl.c -- SDL2 video driver for OPENSTEP
 *
 * From sdlquake (https://github.com/mckayemu/sdlquake), which is
 * id Software's Quake under the GNU General Public License, version 2
 * or later.  MODIFIED for OPENSTEP 4.2 and SDL2 on 2026-08-30: the engine
 * side is unchanged, and everything SDL 1.2 provided that SDL2 does not has
 * been rewritten -- the video mode, the palette, the presentation, the key
 * names and the mouse warp.  The default size is 640x480 rather than
 * 640x400.  Each site is commented where it differs.
 *
 * The unmodified original is kept beside this tree in upstream/sdlquake.
 */
//
// THE ONE STRUCTURAL CHANGE: SDL2 HAS NO PALETTISED WINDOW.
//
// SDL 1.2 could hand Quake an 8-bit display surface with a real hardware
// palette, and the engine drew straight into it.  SDL2's window surface is
// always a packed RGB format, so the 8-bit picture becomes a surface of our
// own that gets blitted -- with the palette applied -- into the window's
// surface once a frame.  Measured on this machine at 640x480, that blit is
// 1.17 ms and the AppKit present that follows it is 41 ms, so the extra
// copy is not what costs anything here.
//
// The other differences are renames, and they are marked where they occur.

#include "SDL.h"
#include "quakedef.h"
#include "d_local.h"

viddef_t    vid;                // global video state
unsigned short  d_8to16table[256];

// 640x480 rather than upstream's 640x400.  The engine does not care, and
// 4:3 is what this port is aimed at; -winsize still overrides it.
#define    BASEWIDTH    640
#define    BASEHEIGHT   480

int    VGA_width, VGA_height, VGA_rowbytes, VGA_bufferrowbytes = 0;
byte    *VGA_pagebase;

static SDL_Window  *sdl_window  = NULL;
static SDL_Surface *sdl_screen  = NULL;   // the window's own surface
static SDL_Surface *sdl_indexed = NULL;   // what Quake draws into

static qboolean mouse_avail;
static float   mouse_x, mouse_y;
static int mouse_oldbuttonstate = 0;

// No support for option menus
void (*vid_menudrawfn)(void) = NULL;
void (*vid_menukeyfn)(int key) = NULL;

void    VID_SetPalette (unsigned char *palette)
{
    int i;
    SDL_Color colors[256];

    for ( i=0; i<256; ++i )
    {
        colors[i].r = *palette++;
        colors[i].g = *palette++;
        colors[i].b = *palette++;
        colors[i].a = 255;              // SDL2's SDL_Color has alpha
    }
    if (sdl_indexed)
        // SDL_SetColors -> SDL_SetPaletteColors, and it takes the palette
        // rather than the surface.
        SDL_SetPaletteColors(sdl_indexed->format->palette, colors, 0, 256);
}

void    VID_ShiftPalette (unsigned char *palette)
{
    VID_SetPalette(palette);
}

void    VID_Init (unsigned char *palette)
{
    int pnum, chunk;
    byte *cache;
    int cachesize;
    Uint32 flags;

    // SDL_INIT_CDROM is gone from SDL2 along with the whole CD API; this
    // build uses cd_null.c, so there is nothing to ask for.
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) < 0)
        Sys_Error("VID: Couldn't load SDL: %s", SDL_GetError());

    vid.width = BASEWIDTH;
    vid.height = BASEHEIGHT;
    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    if ((pnum=COM_CheckParm("-winsize")))
    {
        if (pnum >= com_argc-2)
            Sys_Error("VID: -winsize <width> <height>\n");
        vid.width = Q_atoi(com_argv[pnum+1]);
        vid.height = Q_atoi(com_argv[pnum+2]);
        if (!vid.width || !vid.height)
            Sys_Error("VID: Bad window width/height\n");
    }

    flags = 0;
    if ( COM_CheckParm ("-fullscreen") )
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    sdl_window = SDL_CreateWindow("sdlquake",
                                  SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED,
                                  vid.width, vid.height, flags);
    if (!sdl_window)
        Sys_Error("VID: Couldn't create window: %s\n", SDL_GetError());
    sdl_screen = SDL_GetWindowSurface(sdl_window);
    if (!sdl_screen)
        Sys_Error("VID: Couldn't get window surface: %s\n", SDL_GetError());

    sdl_indexed = SDL_CreateRGBSurfaceWithFormat(0, vid.width, vid.height,
                                                 8, SDL_PIXELFORMAT_INDEX8);
    if (!sdl_indexed)
        Sys_Error("VID: Couldn't create the 8-bit surface: %s\n",
                  SDL_GetError());

    VID_SetPalette(palette);

    /*
     * Say what was actually created, not what was asked for.
     *
     * "The window looks too small" is not a question anyone can settle by
     * looking, because a 640x480 window on a 1600x1200 screen covers a
     * sixth of it and looks exactly like a mistake.  The two numbers that
     * separate those cases belong in the log, where they survive the window
     * being closed.
     */
    {
        int gotw = 0, goth = 0;
        SDL_DisplayMode dm;

        SDL_GetWindowSize(sdl_window, &gotw, &goth);
        if (SDL_GetDesktopDisplayMode(0, &dm) == 0)
            Con_Printf("VID: asked %dx%d, got %dx%d, on a %dx%d desktop\n",
                       vid.width, vid.height, gotw, goth, dm.w, dm.h);
        else
            Con_Printf("VID: asked %dx%d, got %dx%d\n",
                       vid.width, vid.height, gotw, goth);
    }

    VGA_width = vid.conwidth = vid.width;
    VGA_height = vid.conheight = vid.height;
    vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);
    vid.numpages = 1;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));
    VGA_pagebase = vid.buffer = sdl_indexed->pixels;
    // The PITCH, not the width: SDL aligns rows, and assuming otherwise
    // shears the picture by a few pixels a line.
    VGA_rowbytes = vid.rowbytes = sdl_indexed->pitch;
    vid.conbuffer = vid.buffer;
    vid.conrowbytes = vid.rowbytes;
    vid.direct = 0;

    // allocate z buffer and surface cache
    chunk = vid.width * vid.height * sizeof (*d_pzbuffer);
    cachesize = D_SurfaceCacheForRes (vid.width, vid.height);
    chunk += cachesize;
    d_pzbuffer = Hunk_HighAllocName(chunk, "video");
    if (d_pzbuffer == NULL)
        Sys_Error ("Not enough memory for video mode\n");

    cache = (byte *) d_pzbuffer
            + vid.width * vid.height * sizeof (*d_pzbuffer);
    D_InitCaches (cache, cachesize);

    SDL_ShowCursor(0);
}

void    VID_Shutdown (void)
{
    if (sdl_indexed) { SDL_FreeSurface(sdl_indexed); sdl_indexed = NULL; }
    if (sdl_window)  { SDL_DestroyWindow(sdl_window); sdl_window = NULL; }
    sdl_screen = NULL;
    SDL_Quit();
}

void    VID_Update (vrect_t *rects)
{
    vrect_t *rect;

    if (!sdl_window || !sdl_indexed)
        return;
    // The window surface can be replaced under us -- a resize, a mode
    // change -- and SDL says to fetch it again rather than keep it.
    sdl_screen = SDL_GetWindowSurface(sdl_window);
    if (!sdl_screen)
        return;

    // Only the rectangles Quake actually drew are converted from 8-bit to
    // the window's format.  The present that follows is a whole-window one
    // on purpose: this port's backend repaints the entire view regardless of
    // what it is told, so asking it for less buys nothing (measured), while
    // converting less does.
    for (rect = rects; rect; rect = rect->pnext)
    {
        SDL_Rect r;
        r.x = rect->x;
        r.y = rect->y;
        r.w = rect->width;
        r.h = rect->height;
        SDL_BlitSurface(sdl_indexed, &r, sdl_screen, &r);
    }
    SDL_UpdateWindowSurface(sdl_window);
}

/*
================
D_BeginDirectRect
================
*/
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
    Uint8 *offset;

    if (!sdl_indexed) return;
    if ( x < 0 ) x = sdl_indexed->w+x-1;
    offset = (Uint8 *)sdl_indexed->pixels + y*sdl_indexed->pitch + x;
    while ( height-- )
    {
        memcpy(offset, pbitmap, width);
        offset += sdl_indexed->pitch;
        pbitmap += width;
    }
}

/*
================
D_EndDirectRect
================
*/
void D_EndDirectRect (int x, int y, int width, int height)
{
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
                if ( (event.motion.x != (vid.width/2)) ||
                     (event.motion.y != (vid.height/2)) ) {
                    mouse_x = event.motion.xrel*10;
                    mouse_y = event.motion.yrel*10;
                    if ( (event.motion.x < ((vid.width/2)-(vid.width/4))) ||
                         (event.motion.x > ((vid.width/2)+(vid.width/4))) ||
                         (event.motion.y < ((vid.height/2)-(vid.height/4))) ||
                         (event.motion.y > ((vid.height/2)+(vid.height/4))) ) {
                        /* SDL_WarpMouse -> SDL_WarpMouseInWindow: SDL2 warps
                         * within a named window rather than the screen.
                         *
                         * SDL_SetRelativeMouseMode would be the modern way
                         * and would avoid the warp entirely, but whether it
                         * works on this port is unproven, so the faithful
                         * translation stays until it is measured. */
                        SDL_WarpMouseInWindow(sdl_window,
                                              vid.width/2, vid.height/2);
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

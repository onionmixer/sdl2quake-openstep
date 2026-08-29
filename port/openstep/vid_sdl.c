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
    IN_SetWindow(sdl_window);
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

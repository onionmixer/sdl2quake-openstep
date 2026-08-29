/*
 * gl_vidsdl.c -- an SDL2 OpenGL video backend for GLQuake on OPENSTEP
 *
 * NEW FILE.  sdlquake carries GL video backends for GLX, 3dfx and Windows
 * and none for SDL, so this implements the same contract against SDL2 and
 * this workspace's Matrox G450 driver.  It is not derived from any of them;
 * what it shares with them is the set of functions the engine calls.
 *
 * Quake is GPL-2.0-or-later and so is this file, 2026-08-30.
 *
 *
 * WHY THIS EXISTS.  The software engine reached 16.0 fps at 640x480 and
 * could not go faster: 41 of its 62.5 ms were AppKit moving pixels at a
 * measured 126 ns each, which caps that path at 25.8 fps however quick the
 * renderer is.  OpenGL goes around it -- the card draws, and the finished
 * frame is stamped video memory to video memory without crossing the bus.
 *
 * TWO ENGINE SETTINGS ARE REQUIRED, and they were measured rather than
 * assumed (Q2-0):
 *
 *     gl_texturemode GL_LINEAR   the driver refuses all four mipmap
 *                                filters, and GLQuake's default is
 *                                GL_LINEAR_MIPMAP_NEAREST -- so with the
 *                                default every world surface is drawn in
 *                                software while looking perfectly correct
 *     -lm_4                      lightmaps are GL_LUMINANCE by default and
 *                                the driver takes RGB and RGBA only
 *
 * This file sets the first itself, because a default that silently costs
 * all the acceleration is not a default worth keeping here.  The second is
 * a command-line argument the engine reads before this file runs.
 *
 * FIXED SIZE, ONE WINDOW, ONE CONTEXT.  The driver owns one surface at one
 * size; SDL2's backend releases its stamp on a resize and does not rebind.
 * Rather than pretend otherwise, the window is not resizable.
 */
#include <stdio.h>
#include <stdlib.h>

#include "SDL.h"
#include "SDL_openstepglpresent.h"
#include "quakedef.h"
/* NOT glquake.h: quakedef.h:263 includes it already when GLQUAKE is defined,
 * and it has no include guard, so naming it here redefines every type in it. */

/* The driver's own functions.  This file is the only place in the engine
 * that names them, and it hands them to SDL2 rather than calling them:
 * SDL2 cannot name them itself, because libSDL2.a must keep linking against
 * a stock Mesa where they do not exist. */
#include "OpenStepMGAMesaBuffer.h"
#include "OpenStepMGAMesaHook.h"

#define BASEWIDTH  640
#define BASEHEIGHT 480

/*
 * The warp buffer's bound.  Each GL video backend defines this for itself --
 * the GLX and 3dfx ones both do -- because it is the software renderer's
 * header that carries it and the GL build does not include that.
 */
#define WARP_WIDTH  320
#define WARP_HEIGHT 200

viddef_t vid;

unsigned short d_8to16table[256];
unsigned       d_8to24table[256];
unsigned char  d_15to8table[65536];

int scr_width, scr_height;

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions;

static qboolean is8bit = false;
qboolean gl_mtexable = false;

cvar_t vid_mode = {"vid_mode", "0", false};
cvar_t gl_ztrick = {"gl_ztrick", "1"};

/*
 * Globals every GL video backend owns.  The engine declares them in
 * glquake.h and expects whichever backend is linked to define them.
 */
int   texture_extension_number = 1;
float gldepthmin, gldepthmax;
qboolean isPermedia = false;

/*
 * GL_LINEAR, and the GLX backend chooses the same -- its file has the five
 * mipmap alternatives sitting commented out above this line.  Here it is
 * not a preference: the driver's state gate refuses all four mipmap
 * filters, so any other value draws the whole world in software.  Setting
 * `gl_texturemode' at the console changes this, and changing it to a
 * mipmap mode will cost the acceleration.
 */
int   texture_mode = GL_LINEAR;

static SDL_Window   *sdl_window;
static SDL_GLContext sdl_context;

void (*vid_menudrawfn)(void) = NULL;
void (*vid_menukeyfn)(int key) = NULL;

/*
 * The three driver entries SDL2 will call.  Static, and its lifetime is the
 * program's: SDL keeps the pointer rather than a copy, so a struct on the
 * stack would dangle by the first swap.
 */
static const SDL_OpenStepGLPresent present_hooks = {
    SDL_OPENSTEP_GLPRESENT_ABI,
    sizeof(SDL_OpenStepGLPresent),
    OSMGAMesaBufferOrigin,
    OSMGAMesaBufferPresentMode,
    OSMGAMesaBufferPresentRect
};

/*
 * The 8-bit palette, expanded two ways.
 *
 * d_8to24table is what every texture upload reads: Quake's art is 8-bit and
 * the card is not.  d_15to8table goes the other way, and is used where the
 * engine has to find the palette entry nearest a colour it computed.
 *
 * Entry 255 loses its alpha because that is Quake's transparent index.
 */
void
VID_SetPalette (unsigned char *palette)
{
    unsigned char *pal = palette;
    unsigned *table = d_8to24table;
    int i;

    for (i = 0; i < 256; i++) {
        unsigned r = pal[0], g = pal[1], b = pal[2];
        pal += 3;
        *table++ = (255U << 24) | (b << 16) | (g << 8) | r;
    }
    d_8to24table[255] &= 0x00ffffffU;

    for (i = 0; i < (1 << 15); i++) {
        int r = ((i & 0x001f) << 3) + 4;
        int g = ((i & 0x03e0) >> 2) + 4;
        int b = ((i & 0x7c00) >> 7) + 4;
        const unsigned char *p = (const unsigned char *)d_8to24table;
        int best = 0, bestdist = 0x7fffffff, v;

        for (v = 0; v < 256; v++, p += 4) {
            int dr = r - (int)p[0], dg = g - (int)p[1], db = b - (int)p[2];
            int dist = dr * dr + dg * dg + db * db;
            if (dist < bestdist) { bestdist = dist; best = v; }
        }
        d_15to8table[i] = (unsigned char)best;
    }
}

void
VID_ShiftPalette (unsigned char *palette)
{
    /* Nothing: the GL path re-uploads nothing on a palette shift, and the
     * GLX backend leaves this empty for the same reason. */
    (void)palette;
}

qboolean
VID_Is8bit (void)
{
    return is8bit;
}

void
VID_Init8bitPalette (void)
{
    /*
     * Not taken, and the reason is the driver rather than Mesa.
     *
     * GLQuake's 8-bit path uploads GL_COLOR_INDEX8_EXT through
     * GL_EXT_shared_texture_palette, and this driver's texture gate accepts
     * GL_RGB and GL_RGBA only -- so a paletted texture would be correct and
     * entirely software.  Mesa 3.4.2 does carry the extension; that is not
     * what decides it.
     */
    is8bit = false;
}

void
CheckMultiTextureExtensions (void)
{
    /*
     * GLQuake looks for GL_SGIS_multitexture and Mesa 3.4.2 offers
     * GL_ARB_multitexture, which is a different name and a different entry
     * point.  The engine reaches its multitexture path only through function
     * pointers that stay NULL, so leaving them alone selects the two-pass
     * path -- and two passes of one texture unit is exactly the shape this
     * driver accelerates.
     */
    gl_mtexable = false;
}

/*
 * `mgastats' -- what the card actually did.
 *
 * WITHOUT THIS THERE IS NO EVIDENCE.  A GLQuake drawing every triangle in
 * software renders the same picture at a worse frame rate, and nothing on
 * the screen says which happened.  These are the driver's own counters, and
 * the two that matter are `drawn' and `state software': a level is thousands
 * of triangles a frame, so if the card drew the world `drawn' is in the
 * thousands and if it did not, `drawn' is a handful of interface pieces.
 *
 * Printed as totals rather than rates; call it twice and subtract.
 */
static void
MGA_Stats_f (void)
{
    Con_Printf ("surface       : %s\n",
                OSMGAMesaBufferOrigin () ? "the engine's" : "the caller's");
    Con_Printf ("drawn         : %lu   (warp %lu, trapezoid %lu)\n",
                OSMGAMesaHookDrawn (), OSMGAMesaHookWarp (),
                OSMGAMesaHookDrawn () - OSMGAMesaHookWarp ());
    Con_Printf ("state hard    : %lu   soft %lu\n",
                OSMGAMesaHookHardState (), OSMGAMesaHookSoftState ());
    Con_Printf ("to Mesa       : %lu   declined %lu\n",
                OSMGAMesaHookSoftware (), OSMGAMesaHookDeclined ());
    Con_Printf ("texture       : no room %lu, not affine %lu\n",
                OSMGAMesaHookTexAbsent (), OSMGAMesaHookTexPersp ());
    Con_Printf ("read back     : %lu copies\n", OSMGAMesaBufferCopies ());
}

static void
GL_Init (void)
{
    gl_vendor     = (const char *)glGetString (GL_VENDOR);
    gl_renderer   = (const char *)glGetString (GL_RENDERER);
    gl_version    = (const char *)glGetString (GL_VERSION);
    gl_extensions = (const char *)glGetString (GL_EXTENSIONS);
    Con_Printf ("GL_VENDOR: %s\n", gl_vendor ? gl_vendor : "(none)");
    Con_Printf ("GL_RENDERER: %s\n", gl_renderer ? gl_renderer : "(none)");
    Con_Printf ("GL_VERSION: %s\n", gl_version ? gl_version : "(none)");

    glClearColor (1.0f, 0.0f, 0.0f, 0.0f);
    glCullFace (GL_FRONT);
    glEnable (GL_TEXTURE_2D);
    glEnable (GL_ALPHA_TEST);
    glAlphaFunc (GL_GREATER, 0.666f);
    glShadeModel (GL_FLAT);

    /*
     * GL_LINEAR, not a mipmap filter, and this is the setting the whole
     * exercise turns on.  Measured on this hardware: with
     * GL_LINEAR_MIPMAP_NEAREST the driver's state gate selects software for
     * every textured triangle; with GL_LINEAR it selects hardware and the
     * card draws them.  Distant textures shimmer without mip levels, and
     * that is the trade this build makes deliberately.
     */
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

void
GL_BeginRendering (int *x, int *y, int *width, int *height)
{
    *x = 0;
    *y = 0;
    *width = scr_width;
    *height = scr_height;
}

void
GL_EndRendering (void)
{
    /*
     * SDL_GL_SwapWindow is where the frame goes to the screen, and with the
     * hooks registered it never enters system memory: SDL2 asks the driver
     * to blit video memory to video memory instead.  Measured with another
     * program at 800x600: 8.03 ms against 60.5 for the ordinary path.
     *
     * No glFinish here.  SDL2's swap does one itself before it stamps.
     */
    glFlush ();
    SDL_GL_SwapWindow (sdl_window);
}

void
VID_Shutdown (void)
{
    if (sdl_window)
        SDL_SetWindowData (sdl_window, SDL_OPENSTEP_GLPRESENT_KEY, NULL);
    if (sdl_context) { SDL_GL_DeleteContext (sdl_context); sdl_context = NULL; }
    if (sdl_window)  { SDL_DestroyWindow (sdl_window); sdl_window = NULL; }
    SDL_Quit ();
}

void
VID_Init (unsigned char *palette)
{
    int pnum;

    /* SDL_INIT_CDROM is gone from SDL2 with the whole CD API; this build
     * links cd_null.c, so there is nothing to ask for. */
    if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
        Sys_Error ("VID: Couldn't load SDL: %s", SDL_GetError ());

    vid.width  = BASEWIDTH;
    vid.height = BASEHEIGHT;
    vid.maxwarpwidth  = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    if ((pnum = COM_CheckParm ("-winsize"))) {
        if (pnum >= com_argc - 2)
            Sys_Error ("VID: -winsize <width> <height>\n");
        vid.width  = Q_atoi (com_argv[pnum + 1]);
        vid.height = Q_atoi (com_argv[pnum + 2]);
        if (!vid.width || !vid.height)
            Sys_Error ("VID: Bad window width/height\n");
    }

    /*
     * Not resizable, and no fullscreen flag.  The driver holds one surface
     * at one size and SDL2's stamp is released on a resize without being
     * rebound, so a resizable window would quietly stop being accelerated.
     */
    sdl_window = SDL_CreateWindow ("glquake",
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   vid.width, vid.height,
                                   SDL_WINDOW_OPENGL);
    if (!sdl_window)
        Sys_Error ("VID: Couldn't create window: %s\n", SDL_GetError ());
    sdl_context = SDL_GL_CreateContext (sdl_window);
    if (!sdl_context)
        Sys_Error ("VID: Couldn't create GL context: %s\n", SDL_GetError ());

    /*
     * Hand SDL2 the driver's present entries.  It calls them through plain
     * function pointers and never names them, which is what lets one
     * libSDL2.a serve both an accelerated Mesa and a stock one.  A program
     * that registers nothing gets the ordinary AppKit path and is correct,
     * only slower.
     */
    SDL_SetWindowData (sdl_window, SDL_OPENSTEP_GLPRESENT_KEY,
                       (void *)&present_hooks);

    {
        int gotw = 0, goth = 0;
        SDL_DisplayMode dm;

        SDL_GetWindowSize (sdl_window, &gotw, &goth);
        if (SDL_GetDesktopDisplayMode (0, &dm) == 0)
            Con_Printf ("VID: asked %dx%d, got %dx%d, on a %dx%d desktop\n",
                        vid.width, vid.height, gotw, goth, dm.w, dm.h);
    }

    scr_width  = vid.width;
    scr_height = vid.height;

    vid.aspect = ((float)vid.height / (float)vid.width) * (320.0f / 240.0f);
    vid.numpages = 2;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));
    vid.conwidth  = vid.width;
    vid.conheight = vid.height;
    vid.conrowbytes = 0;
    vid.rowbytes = 0;
    vid.buffer = vid.conbuffer = NULL;
    vid.direct = 0;

    Cvar_RegisterVariable (&gl_ztrick);
    Cmd_AddCommand ("mgastats", MGA_Stats_f);

    GL_Init ();
    VID_SetPalette (palette);
    SDL_ShowCursor (0);
    IN_SetWindow (sdl_window);      /* in_sdl.c, shared with the software build */

    vid.recalc_refdef = 1;
}

/*
 * The engine calls these to put the loading disc on the screen.  The GL
 * backends leave them empty -- there is no 8-bit buffer to write into --
 * and so does this one.
 */
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
    (void)x; (void)y; (void)pbitmap; (void)width; (void)height;
}

void D_EndDirectRect (int x, int y, int width, int height)
{
    (void)x; (void)y; (void)width; (void)height;
}


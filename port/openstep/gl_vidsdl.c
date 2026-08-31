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
#include <signal.h>
#include <stdarg.h>
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
#ifndef OSMGA_GLQUAKE_PLAIN
#include "OpenStepMGAMesaBuffer.h"
#include "OpenStepMGAMesaHook.h"
#include "OpenStepMGAMesaTexture.h"
#include "OpenStepMGAMesaTriangle.h"
#include "OpenStepMGAMesaWarp.h"
#endif

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
/*
 * TRUE, and it is the lightmap format that this decides -- nothing else.
 * In the files this build compiles, gl_rsurf.c reads it in exactly one
 * place: to make the lightmap atlases GL_RGBA instead of GL_LUMINANCE.
 * (The name is from the Permedia card, whose driver could not blend a
 * luminance texture either.)
 *
 * The Matrox hook accepts RGB and RGBA textures and nothing with fewer
 * channels, so a luminance lightmap sends the SECOND PASS OF EVERY LIT
 * SURFACE to Mesa's software rasteriser -- drawing into video memory,
 * with a blend that reads it back, one span at a time -- however good the
 * first pass was.  With RGBA the atlas stores 255-light in alpha, black
 * in colour, and the ordinary source-alpha blend darkens the same amount;
 * that is the -lm_4 path GLQuake has always had.  -lm_1 on the command
 * line still selects luminance, since the parameters are read after this.
 */
qboolean isPermedia = true;

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
#ifndef OSMGA_GLQUAKE_PLAIN
static const SDL_OpenStepGLPresent present_hooks = {
    SDL_OPENSTEP_GLPRESENT_ABI,
    sizeof(SDL_OpenStepGLPresent),
    OSMGAMesaBufferOrigin,
    OSMGAMesaBufferPresentMode,
    OSMGAMesaBufferPresentRect
};
#endif

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
#ifndef OSMGA_GLQUAKE_PLAIN
/*
 * Everything the back end counts, through a printf-shaped sink so that the
 * console command and the stderr dump print the same lines.
 *
 * The lines are arranged as a PARTITION of the triangles the hook saw:
 * drawn on the card, or one of five ways to software -- the state gate
 * refused the state (gated), the vertex had no usable w or texture q
 * (persp), the texture could not be made resident (absent), the trapezoid
 * builder could not express it (unsupported), or the kernel refused the
 * batch and the triangle was replayed (replayed).  "to Mesa" is the sum of
 * the last four, counted where they are handed over; gated is counted
 * separately because under a refused state the hook never sees the
 * hand-over at all.
 */
static void
MGA_Stats_Dump (void (*out)(char *fmt, ...))
{
    unsigned long fl[4], sb[6], bk[2];
    int i, any;

    OSMGAMesaHookFlushCounts (fl);
    OSMGAMesaHookSubmitStats (sb);
    OSMGAMesaHookBracketStats (bk);

    out ("surface       : %s\n",
         OSMGAMesaBufferOrigin () ? "the engine's" : "the caller's");
    out ("drawn         : %lu   (warp %lu, trapezoid %lu)\n",
         OSMGAMesaHookDrawn (), OSMGAMesaHookWarp (),
         OSMGAMesaHookDrawn () - OSMGAMesaHookWarp ());
    out ("gated         : %lu   (state changes: hard %lu soft %lu)\n",
         OSMGAMesaHookGated (),
         OSMGAMesaHookHardState (), OSMGAMesaHookSoftState ());
    out ("to Mesa       : %lu   = persp %lu + absent %lu + unsupported %lu"
         " + replayed %lu\n",
         OSMGAMesaHookSoftware (), OSMGAMesaHookTexPersp (),
         OSMGAMesaHookTexAbsent (), OSMGAMesaHookUnsupported (),
         OSMGAMesaHookReplayed ());
    out ("kernel        : batches %lu, declined %lu, narrowed %lu,"
         " prevalidated %lu\n",
         OSMGAMesaHookBatches (), OSMGAMesaHookDeclined (),
         OSMGAMesaHookNarrowed (), OSMGAMesaHookPrevalidated ());
    out ("losses        : rescued %lu, dropped %lu (clipped %lu)\n",
         OSMGAMesaHookRescued (), OSMGAMesaHookDropped (),
         OSMGAMesaHookDroppedClipped ());
    any = 0;
    for (i = 0; i < OSMGA_MESA_VERDICTS; i++)
        if (OSMGAMesaHookVerdictCount (i)) {
            if (!any) out ("verdicts      :");
            out (" %d=%lu", i, OSMGAMesaHookVerdictCount (i));
            any = 1;
        }
    if (any) out ("   (last %lu site %lu)\n",
                  OSMGAMesaHookLastRefusal ()->verdict,
                  OSMGAMesaHookLastRefusalSite ());
    any = 0;
    for (i = 0; i < OSMGA_MESA_VERDICTS; i++)
        if (OSMGAMesaHookLocalVerdictCount (i)) {
            if (!any) out ("local verdicts:");
            out (" %d=%lu", i, OSMGAMesaHookLocalVerdictCount (i));
            any = 1;
        }
    if (any) out ("   (last %lu site %lu)\n",
                  OSMGAMesaHookLocalLastVerdict (), OSMGAMesaHookLocalLastSite ());
    {
        unsigned long gl[OSMGA_MESA_GATE_WHY], gc[OSMGA_MESA_GATE_WHY];
        OSMGAMesaHookGateWhy (gl, gc);
        any = 0;
        for (i = 0; i < OSMGA_MESA_GATE_WHY && gl[i]; i++) {
            if (!any) out ("gate refused  :");
            out (" Hook.c:%lu x%lu", gl[i], gc[i]);
            any = 1;
        }
        if (any) out ("\n");
    }
    {
        unsigned long rb[7];
        OSMGAMesaRebaseStats (rb);
        out ("rebase        : seen %lu, moved %lu, no whole K %lu, q!=1 %lu,"
             " repeatU %lu, over %lu (worst %lu repeats)\n",
             rb[0], rb[1], rb[2], rb[3], rb[4], rb[5], rb[6]);
    }
    {
        unsigned long bl[OSMGA_MESA_BUILD_WHY], bc[OSMGA_MESA_BUILD_WHY];
        OSMGAMesaBuildWhy (bl, bc);
        any = 0;
        for (i = 0; i < OSMGA_MESA_BUILD_WHY && bl[i]; i++) {
            if (!any) out ("builder refused:");
            out (" Triangle.c:%lu x%lu", bl[i], bc[i]);
            any = 1;
        }
        if (any) out ("\n");
    }
    {
        unsigned long wb[7];
        OSMGAMesaHookWhyBatch (wb);
        out ("flush         : bracket %lu, key %lu, full %lu, other %lu,"
             " clip %lu\n", fl[0], fl[1], fl[2], fl[3], wb[0]);
        out ("  key was       : texture %lu, gradients %lu\n", wb[4], wb[5]);
        out ("warp declined : state %lu, vertex %lu, forced %lu,"
             " rhw ratio %lu\n", wb[1], wb[2], wb[3], wb[6]);
        {
            static const char *wn[OSMGA_WARP_NO_COUNT] = {
                "null", "xbits", "xrange", "ybits", "yrange", "zbits",
                "zrange", "qsign", "rhwbits", "rhwrange", "ubits", "vbits" };
            unsigned long wc[OSMGA_WARP_NO_COUNT];
            OSMGAMesaWarpNoCounts (wc);
            any = 0;
            for (i = 0; i < OSMGA_WARP_NO_COUNT; i++)
                if (wc[i]) {
                    if (!any) out ("warp vertex   :");
                    out (" %s %lu", wn[i], wc[i]);
                    any = 1;
                }
            if (any) out ("\n");
        }
    }
    out ("submit        : %lu calls, %lu us, %lu dwords, spins %lu (max %lu)\n",
         sb[0], sb[1], sb[2], sb[3], sb[4]);
    out ("bracket       : %lu opens, %lu us\n", bk[0], bk[1]);
    out ("texture       : uploads %lu, refused %lu, evicted %lu\n",
         OSMGAMesaTexUploads (), OSMGAMesaTexRefused (), OSMGAMesaTexEvicted ());
    out ("read back     : %lu copies\n", OSMGAMesaBufferCopies ());
    out ("present mode  : on %lu, off %lu;  refused busy %lu dst %lu"
         " src %lu geom %lu latch %lu mode %lu\n",
         OSMGAMesaBufferPresentOnCount (), OSMGAMesaBufferPresentOffCount (),
         OSMGAMesaBufferPresentRefused (5), OSMGAMesaBufferPresentRefused (3),
         OSMGAMesaBufferPresentRefused (2), OSMGAMesaBufferPresentRefused (4),
         OSMGAMesaBufferPresentRefused (6), OSMGAMesaBufferPresentRefused (7));
}

static void
MGA_Stats_f (void)
{
    MGA_Stats_Dump (Con_Printf);
}

/*
 * The same to stderr every OSMGA_STATS_EVERY frames.  Each dump carries the frame count so the counters can be read
 * per frame, which is the only way two runs of different length compare.
 * The final dump is made at shutdown whatever the clock says.
 */
static void
MGA_Stats_Err (char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
}

static unsigned long mga_stats_every = 0;
static unsigned long mga_frames = 0;
static volatile sig_atomic_t mga_quit_asked = 0;
static unsigned long mga_quit_frames = 0;

/*
 * A harness that ends a run does it with SIGTERM, and Quake has no handler
 * for it -- the process just stops, and the final dump with it.  So the
 * signal only raises a flag, and the frame loop turns the flag into the
 * ordinary quit, which runs Host_Shutdown and so VID_Shutdown, where the
 * final dump is.  Nothing is done inside the handler itself.
 */
static void
MGA_OnTerm (int sig)
{
    (void)sig;
    mga_quit_asked = 1;
}

static void
MGA_Stats_Tick (int final)
{
    double now;

    if (mga_stats_every < 1)
        return;
    now = Sys_FloatTime ();
    /*
     * Every N FRAMES, not every N seconds.  A frame here can take minutes
     * once acceleration is gone, and a clock-driven tick then fires far
     * too rarely to catch anything; and a run cut short by a harness has
     * no last frame to dump on.  Frames are what the counters are about
     * in any case.  The first frame always dumps.
     */
    if (!final && mga_frames != 1 &&
        (mga_frames % mga_stats_every) != 0)
        return;
    fprintf (stderr, "==== mgastats %s t=%.1f frames=%lu\n",
             final ? "final" : "tick", now, mga_frames);
    MGA_Stats_Dump (MGA_Stats_Err);
}

#endif

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
     * (Historical: this once forced GL_LINEAR because the driver drew no
     * mipmap filter at all.  Since driver 1.3 the WARP path draws every
     * mip filter -- the *_MIPMAP_NEAREST pair exactly, the LINEAR pair
     * as a documented approximation -- and the audit below pins
     * MAX_LEVEL instead.)
     */
    /*
     * THE FILTER THAT MATTERS IS THE UPLOAD'S, NOT THIS ONE.
     *
     * glTexParameterf here sets the filter on whatever texture happens to be
     * bound now.  Every texture the engine uploads later gets gl_filter_min
     * instead (gl_draw.c:1079), and that variable's default is
     * GL_LINEAR_MIPMAP_NEAREST -- which this driver's state gate refuses, so
     * every world surface would be drawn in software while looking correct
     * and running at a crawl.  That is exactly what happened before this
     * line existed.
     *
     * Set at VID_Init, before any texture is loaded, so nothing has to be
     * re-uploaded.  `gl_texturemode' still changes it at the console; asking
     * for a mipmap mode there gives up the acceleration, knowingly.
     */
    {
        extern int gl_filter_min, gl_filter_max;
        gl_filter_min = GL_LINEAR;
        gl_filter_max = GL_LINEAR;
    }
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

/*
 * Keep the upload filter to something the card can draw.
 *
 * VID_Init runs BEFORE the configs, so any default set there is overwritten
 * by whatever they say -- and LibreQuake's default.cfg says
 * `gl_texturemode gl_nearest_mipmap_linear'.  The driver refuses all four
 * mipmap filters, so that one line sends every world surface to software
 * while the picture stays correct and the frame rate collapses.  Measured
 * with gdb: the only geometry reaching the card was the sky.
 *
 * Checked once a frame because there is no other moment that is reliably
 * after the configs and before the drawing.  It is two integer comparisons.
 *
 * SAID OUT LOUD, once.  Quietly overriding what someone typed is worse than
 * being slow; this way the console explains why the setting did not stick.
 */
static void
GL_KeepFilterDrawable (void)
{
    extern int gl_filter_min, gl_filter_max;
    static qboolean told = false;

    if (gl_filter_min == GL_LINEAR || gl_filter_min == GL_NEAREST ||
        gl_filter_min == GL_NEAREST_MIPMAP_NEAREST ||
        gl_filter_min == GL_LINEAR_MIPMAP_NEAREST ||
        gl_filter_min == GL_NEAREST_MIPMAP_LINEAR ||
        gl_filter_min == GL_LINEAR_MIPMAP_LINEAR)
        return;

    if (!told) {
        Con_Printf ("gl_texturemode: not a GL minification filter this\n"
                    "                driver knows; GL_LINEAR_MIPMAP_NEAREST\n"
                    "                is used instead.\n");
        told = true;
    }
    /*
     * Through the engine's own command, not by assigning the two globals.
     *
     * The globals only decide the filter of textures uploaded LATER.  The
     * command (Draw_TextureMode_f) also walks every mipmapped texture
     * object already created and re-sets its filters, which is what a
     * repair has to do: LibreQuake's default.cfg runs
     * "gl_texturemode gl_nearest_mipmap_linear" at startup, and anything
     * uploaded under that mode kept a minification filter the driver
     * refuses -- so those textures were drawn in software, by Mesa's
     * slowest textured path at that (it takes the lambda route whenever
     * min and mag filters differ), for the life of the process.
     */
    Cmd_ExecuteString ("gl_texturemode GL_LINEAR_MIPMAP_NEAREST",
                       src_command);
    if (gl_filter_max != GL_LINEAR && gl_filter_max != GL_NEAREST)
        gl_filter_max = GL_LINEAR;
}

/*
 * How many texture objects carry a filter the driver refuses, counted when
 * a level has just been loaded -- the moment its textures exist and nothing
 * has been drawn with them yet.  Printed to stderr so a harness sees it.
 */
static void
GL_FilterAudit (void)
{
    static struct model_s *audited = NULL;
    extern int texture_extension_number;
    GLint was = 0, mn = 0, mg = 0;
    int id, off = 0, total = 0;

    if (cl.worldmodel == audited)
        return;
    audited = cl.worldmodel;
    glGetIntegerv (GL_TEXTURE_BINDING_2D, &was);
    for (id = 1; id < texture_extension_number; id++) {
        if (!glIsTexture ((GLuint)id))
            continue;
        glBindTexture (GL_TEXTURE_2D, (GLuint)id);
        glGetTexParameteriv (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &mn);
        glGetTexParameteriv (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &mg);
        total++;
        if ((mn != GL_NEAREST && mn != GL_LINEAR &&
             mn != GL_NEAREST_MIPMAP_NEAREST &&
             mn != GL_LINEAR_MIPMAP_NEAREST &&
             mn != GL_NEAREST_MIPMAP_LINEAR &&
             mn != GL_LINEAR_MIPMAP_LINEAR) ||
            (mg != GL_NEAREST && mg != GL_LINEAR))
            off++;
        /*
         * Pin GL's own level clamp to the hardware's: the engine walks
         * at most four maps below the base and no deeper than 8x8, so
         * MAX_LEVEL is set to that same map.  This is what makes the
         * mip acceleration EXACT rather than approximately right --
         * Mesa's lambda clamp and the chip's mapnb then name the same
         * last level (M12 section 8, review condition 3).
         */
        if (mn == GL_NEAREST_MIPMAP_NEAREST ||
            mn == GL_LINEAR_MIPMAP_NEAREST ||
            mn == GL_NEAREST_MIPMAP_LINEAR ||
            mn == GL_LINEAR_MIPMAP_LINEAR) {
            GLint tw = 0, th = 0, cap;

            glGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,
                                      &tw);
            glGetTexLevelParameteriv (GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT,
                                      &th);
            cap = 0;
            while ((8 << (cap + 1)) <= tw && (8 << (cap + 1)) <= th &&
                   cap < 4)
                cap++;
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, cap);
        }
    }
    glBindTexture (GL_TEXTURE_2D, (GLuint)was);
    fprintf (stderr, "==== filter audit: %d texture objects, %d with a filter"
             " the driver refuses\n", total, off);
}

void
GL_BeginRendering (int *x, int *y, int *width, int *height)
{
    /*
     * THE AUDIO THREAD IS HELD OFF FOR THE FRAME.
     *
     * S_PaintChannels runs in SDL's audio callback (snd_sdl.c:55) and
     * calls S_LoadSound (snd_mix.c:287), which calls Cache_Alloc
     * (snd_mem.c:139), which calls Cache_Move (zone.c:617) -- memcpy'ing
     * cache blocks to new addresses.  The renderer holds pointers into
     * those blocks for the length of a frame: R_DrawAliasModel takes
     * paliashdr from Mod_Extradata and walks it through R_SetupAliasFrame
     * and GL_DrawAliasFrame.  A core dump caught exactly that -- a view
     * model whose header had poseverts 0 and a commands offset pointing
     * outside the block -- and the frames here are long enough to make it
     * likely rather than rare.
     *
     * Quake's zone and cache are not thread-safe and cannot be made so
     * from the port, so the callback is held off while the frame runs.
     * The cost is that a frame longer than the audio buffer is a gap in
     * the sound; the alternative is a renderer reading freed memory.
     */
    SDL_LockAudio ();
    GL_KeepFilterDrawable ();
    GL_FilterAudit ();
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
    SDL_UnlockAudio ();      /* see GL_BeginRendering */
#ifndef OSMGA_GLQUAKE_PLAIN
    mga_frames++;
    MGA_Stats_Tick (0);
    /*
     * A run that ends itself.  OSMGA_QUIT_AFTER_FRAMES names a frame count
     * and the process quits through Host_Shutdown when it is reached --
     * the only way out that leaves the driver, the surface and the console
     * as they should be.  A harness must never end a run from outside:
     * a signal that lands while the process is inside the driver has
     * been followed by a frozen machine twice.
     */
    if (mga_quit_frames && mga_frames >= mga_quit_frames)
        mga_quit_asked = 1;
    if (OSMGAMesaHookDeadlineHit ())
        mga_quit_asked = 1;
    if (mga_quit_asked)
        Sys_Quit ();
#endif
}

void
VID_Shutdown (void)
{
#ifndef OSMGA_GLQUAKE_PLAIN
    MGA_Stats_Tick (1);
#endif
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
    /*
     * Sixteen bits of depth, said out loud.  The driver accelerates only a
     * context whose depth buffer is exactly sixteen bits -- it shares one
     * 16-bit depth surface in video memory -- and that is what this
     * backend's OSMesa gives by default today.  Asking for it explicitly
     * keeps the two from drifting apart in silence: if the default ever
     * changes, the request still names what the driver needs.
     */
    SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 16);
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
#ifndef OSMGA_GLQUAKE_PLAIN
    SDL_SetWindowData (sdl_window, SDL_OPENSTEP_GLPRESENT_KEY,
                       (void *)&present_hooks);
#endif

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
#ifndef OSMGA_GLQUAKE_PLAIN
    Cmd_AddCommand ("mgastats", MGA_Stats_f);
    {
        const char *e = getenv ("OSMGA_STATS_EVERY");
        if (e && atol (e) > 0)
            mga_stats_every = (unsigned long)atol (e);
        e = getenv ("OSMGA_QUIT_AFTER_FRAMES");
        if (e && atol (e) > 0)
            mga_quit_frames = (unsigned long)atol (e);
        /* Timing instrumentation costs ~4%% of a frame (the hook's own
         * note), so it is opt-in: the submit-microseconds line in
         * mgastats reads zero without it. */
        if (getenv ("OSMGA_STATS_TIME"))
            OSMGAMesaHookInstrument (1);
        /* Test only: refuse every batch (corrupt magic, judged before
         * encoding) so the replay and drop paths run for real. */
        if (getenv ("OSMGA_INJECT_REFUSAL"))
            OSMGAMesaHookInjectRefusal (1);
        /* Always: a harness ends a run with SIGTERM, and a run that quits
         * through Host_Shutdown leaves the console and the driver tidy
         * whether or not it was counting. */
        signal (SIGTERM, MGA_OnTerm);
    }
#endif

    GL_Init ();

    /*
     * The mesh cache needs somewhere to live, and I had left this out.
     *
     * All three upstream GL backends make this directory right after
     * GL_Init -- gl_vidlinuxglx.c:897, gl_vidlinux.c and gl_vidnt.c all do
     * the same two lines.  Without it, gl_mesh.c's fopen(..., "wb") returns
     * NULL, the .ms2 files are never written, and every run rebuilds every
     * alias model's strips from scratch.  The failure is silent: the engine
     * checks the pointer and simply carries on.
     */
    {
        char gldir[MAX_OSPATH];

        sprintf (gldir, "%s/glquake", com_gamedir);
        Sys_mkdir (gldir);
    }

    VID_SetPalette (palette);
    /* The cursor is the input code's: IN_SetWindow hides or shows it
     * with the grab state, and -nomouse keeps it visible. */
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


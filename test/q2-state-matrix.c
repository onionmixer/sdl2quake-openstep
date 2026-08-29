/*
 * Q2-0 -- would the driver accelerate what GLQuake actually asks for?
 *
 * WHY BEFORE gl_vidsdl.c.  A GLQuake that renders entirely in software looks
 * exactly like one that does not, and runs slower than the software engine
 * it replaced.  The cheapest place to find that out is here: one OSMesa
 * context, no window, no SDL, no engine -- just the state combinations
 * GLQuake sets, put to the driver one at a time.
 *
 * The instrument is the driver's own chooser.  HardState and SoftState count
 * how it answered -- selections, not triangles -- which is precisely the
 * question this asks.  TexAbsent counts textures it had no video memory for,
 * which is the other thing that can quietly send the world to software.
 *
 * Two of the expectations below come from reading the driver and were wrong
 * once already, which is why they are measured rather than asserted.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/osmesa.h>

#include "OpenStepMGAMesaHook.h"
#include "OpenStepMGAMesaBuffer.h"
#include "OpenStepMGAMesaProbe.h"

#define W 640
#define H 480
#define TRIS 32

static unsigned long b_hard, b_soft, b_drawn, b_warp, b_sw, b_absent, b_persp;

static void
baseline(void)
{
    b_hard   = OSMGAMesaHookHardState();
    b_soft   = OSMGAMesaHookSoftState();
    b_drawn  = OSMGAMesaHookDrawn();
    b_warp   = OSMGAMesaHookWarp();
    b_sw     = OSMGAMesaHookSoftware();
    b_absent = OSMGAMesaHookTexAbsent();
    b_persp  = OSMGAMesaHookTexPersp();
}

static void
verdict(const char *what, const char *expected)
{
    unsigned long hard   = OSMGAMesaHookHardState()  - b_hard;
    unsigned long soft   = OSMGAMesaHookSoftState()  - b_soft;
    unsigned long drawn  = OSMGAMesaHookDrawn()      - b_drawn;
    unsigned long sw     = OSMGAMesaHookSoftware()   - b_sw;
    unsigned long absent = OSMGAMesaHookTexAbsent()  - b_absent;
    unsigned long persp  = OSMGAMesaHookTexPersp()   - b_persp;
    const char *got;

    /* The chooser's answer is the headline.  Triangles are shown beside it
     * because a hardware state that then refuses every triangle is not a
     * pass, and the two numbers together say which happened. */
    if (drawn > 0UL && hard > 0UL)      got = "HARDWARE";
    else if (soft > 0UL && drawn == 0UL) got = "software";
    else                                 got = "mixed/none";

    printf("  %-22s %-9s  hard %2lu soft %2lu | drawn %3lu sw %3lu"
           " absent %2lu persp %2lu   %s\n",
           what, got, hard, soft, drawn, sw, absent, persp,
           strcmp(got, expected) == 0 ? "" : "<-- not what was expected");
}

/* A texture GLQuake's size and shape.  level 0 only unless mips is true, in
 * which case the whole chain is built -- an incomplete mipmapped texture
 * makes GL disable texturing altogether, which would test the wrong thing. */
static void
maketex(GLuint name, GLenum format, int size, int mips, GLenum minf)
{
    unsigned char *px = (unsigned char *)malloc((size_t)size * size * 4);
    int comp = (format == GL_LUMINANCE) ? 1 : (format == GL_RGB ? 3 : 4);
    int lvl, s, i;

    glBindTexture(GL_TEXTURE_2D, name);
    for (lvl = 0, s = size; s >= 1; lvl++, s /= 2) {
        for (i = 0; i < s * s * comp; i++)
            px[i] = (unsigned char)((i * 7 + lvl * 31) & 0xff);
        glTexImage2D(GL_TEXTURE_2D, lvl, comp, s, s, 0, format,
                     GL_UNSIGNED_BYTE, px);
        if (!mips) break;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)minf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    free(px);
}

/*
 * The texture coordinate range is an argument because Quake's is not 0..1.
 * A world surface tiles its texture across itself, so the s and t the engine
 * emits run to whatever the surface is wide in texture repeats -- tens, on a
 * long wall.  Every arm above used 0..1, which is why they all passed while
 * the engine's own drawing did not.
 */
/*
 * `sz' is how big the triangle is on the screen, and it matters as much as
 * uv does.  The trapezoid's anchor is an extrapolation, so what decides it
 * is the GRADIENT -- texture units per pixel -- not the coordinate range on
 * its own.  A wide triangle with uv 0..256 has a gentle gradient; a narrow
 * one with uv 0..4 can have a far steeper one.  The first version of this
 * varied only uv, which is why every arm passed.
 */
static void
drawuv2(float uv, float sz)
{
    int i;
    for (i = 0; i < TRIS; i++) {
        float o = (float)i * 0.001f;
        glBegin(GL_TRIANGLES);
          glTexCoord2f(0.0f, 0.0f);   glVertex3f(-sz + o, -sz, -1.0f);
          glTexCoord2f(uv,   0.0f);   glVertex3f( sz + o, -sz, -1.0f);
          glTexCoord2f(uv*0.5f, uv);  glVertex3f( 0.0f + o, sz, -1.0f);
        glEnd();
    }
    glFinish();
}

static void
drawuv(float uv)
{
    drawuv2(uv, 0.8f);
}

static void
draw(void)
{
    drawuv(1.0f);
}

int
main(int argc, char **argv)
{
    OSMesaContext ctx;
    void *buf;
    OSMGAMesaProbe probe;
    GLuint tex[200];
    int i;

    (void)argc; (void)argv;
    buf = malloc((size_t)W * H * 4);
    if (!buf) { printf("no memory\n"); return 2; }
    ctx = OSMesaCreateContext(OSMESA_ARGB, NULL);
    if (!ctx) { printf("OSMesaCreateContext failed\n"); return 2; }
    if (!OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
        printf("OSMesaMakeCurrent failed\n"); return 2;
    }

    OSMGAMesaProbeRun(&probe);
    printf("\n  surface is the engine's : %s\n",
           OSMGAMesaBufferOrigin() ? "yes" : "NO -- everything below is software");
    printf("  the probe says          : %s\n\n",
           OSMGAMesaProbeVerdictString(probe.verdict));
    if (!OSMGAMesaBufferOrigin()) {
        printf("  Without an accelerated surface this test cannot answer\n"
               "  anything.  Stopping rather than printing zeros.\n");
        return 1;
    }

    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_LIGHTING); glDisable(GL_DITHER);
    glEnable(GL_TEXTURE_2D);
    glShadeModel(GL_FLAT);
    glGenTextures(200, tex);

    printf("  %-22s %-9s  %s\n", "arm", "result", "counters");
    printf("  ----------------------------------------"
           "----------------------------------\n");

    /* 1. The world as GLQuake ships it: a full mip chain and
     *    GL_LINEAR_MIPMAP_NEAREST, which is gl_texturemode's default. */
    maketex(tex[0], GL_RGB, 64, 1, GL_LINEAR_MIPMAP_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glDisable(GL_BLEND);
    baseline(); draw(); verdict("world / mipmap", "software");

    /* 2. The same world with the one thing changed that the driver's gate
     *    objects to. */
    maketex(tex[1], GL_RGB, 64, 0, GL_LINEAR);
    baseline(); draw(); verdict("world / GL_LINEAR", "HARDWARE");

    /* 3. The lightmap pass as GLQuake ships it. */
    maketex(tex[2], GL_LUMINANCE, 64, 0, GL_LINEAR);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
    baseline(); draw(); verdict("lightmap / LUMINANCE", "software");

    /* 4. The lightmap pass with -lm_4, which uploads RGBA instead. */
    maketex(tex[3], GL_RGBA, 64, 0, GL_LINEAR);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    baseline(); draw(); verdict("lightmap / RGBA", "HARDWARE");

    /* 5. Alias models are smooth-shaded by default.  The review said the
     *    driver refuses that; reading it said otherwise. */
    glDisable(GL_BLEND);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    maketex(tex[4], GL_RGB, 64, 0, GL_LINEAR);
    glShadeModel(GL_SMOOTH);
    baseline(); draw(); verdict("alias / GL_SMOOTH", "HARDWARE");
    glShadeModel(GL_FLAT);
    baseline(); draw(); verdict("alias / GL_FLAT", "HARDWARE");

    /*
     * Arms the first pass did not test: the sizes the data actually uses,
     * and the two-pass sequence in the order the engine performs it.  The
     * world's base pass sets no texture environment of its own -- it
     * inherits whatever the lightmap pass left -- so testing it in
     * isolation was testing something the engine never does.
     */
    glDisable(GL_BLEND);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    maketex(tex[5], GL_RGB, 128, 0, GL_LINEAR);
    baseline(); draw(); verdict("world 128x128", "HARDWARE");
    maketex(tex[6], GL_RGB, 256, 0, GL_LINEAR);
    baseline(); draw(); verdict("world 256x256", "HARDWARE");
    maketex(tex[7], GL_RGB, 512, 0, GL_LINEAR);
    baseline(); draw(); verdict("world 512x512", "HARDWARE");

    /* the engine's order: base pass, then the blended lightmap, then the
     * next surface's base pass with MODULATE still current */
    maketex(tex[8], GL_RGB, 128, 0, GL_LINEAR);
    baseline(); draw(); verdict("pass 1 base", "HARDWARE");
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    maketex(tex[9], GL_RGBA, 128, 0, GL_LINEAR);
    baseline(); draw(); verdict("pass 2 lightmap", "HARDWARE");
    glDisable(GL_BLEND);
    maketex(tex[10], GL_RGB, 128, 0, GL_LINEAR);
    baseline(); draw(); verdict("pass 1 again (MODULATE)", "HARDWARE");

    /* 6. How many textures can be resident at once.  The allocator keeps a
     *    fixed number of blocks whatever their size, and a level references
     *    more distinct textures than that -- 27 at the least, 65 at the
     *    most, counted from the BSPs.  This finds where it stops. */
    /*
     * The coordinate range, which nothing above varied.  Verdict 13 from the
     * kernel is E_TEXCOORD, and it is what revoked acceleration in the
     * engine, so this is the arm that should reproduce it.
     */
    glDisable(GL_BLEND);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    maketex(tex[10], GL_RGB, 64, 0, GL_LINEAR);
    baseline(); drawuv(1.0f);   verdict("uv 0..1", "HARDWARE");
    baseline(); drawuv(4.0f);   verdict("uv 0..4", "HARDWARE");
    baseline(); drawuv(16.0f);  verdict("uv 0..16", "HARDWARE");
    baseline(); drawuv(64.0f);  verdict("uv 0..64", "HARDWARE");
    baseline(); drawuv(256.0f); verdict("uv 0..256", "HARDWARE");

    /*
     * The same coordinate ranges on triangles a few pixels across.  If the
     * anchor is an extrapolation, these are where it runs away.
     */
    baseline(); drawuv2(1.0f,   0.01f); verdict("uv 1  on 1% screen",   "HARDWARE");
    baseline(); drawuv2(16.0f,  0.01f); verdict("uv 16 on 1% screen",   "HARDWARE");
    baseline(); drawuv2(64.0f,  0.01f); verdict("uv 64 on 1% screen",   "HARDWARE");
    baseline(); drawuv2(64.0f,  0.002f); verdict("uv 64 on 0.2% screen", "HARDWARE");

    printf("\n  texture residency: binding distinct 64x64 textures until"
           " one has no room\n");
    {
        unsigned long firstAbsent = 0UL;
        /* Past 133, which is what the worst single level needs -- 65 world
         * textures, up to 24 lightmap sheets, the fixed four, and an
         * allowance for model skins.  A test that stopped at 59 would not
         * reach the number this change was made for. */
        for (i = 11; i < 200; i++) {
            unsigned long before = OSMGAMesaHookTexAbsent();
            maketex(tex[i], GL_RGB, 64, 0, GL_LINEAR);
            draw();
            if (OSMGAMesaHookTexAbsent() > before && firstAbsent == 0UL) {
                firstAbsent = (unsigned long)(i - 4);
                printf("      texture %lu was the first with no room\n",
                       firstAbsent);
                break;
            }
        }
        if (firstAbsent == 0UL)
            printf("      all %d fitted (a level needs about 133)\n", 200 - 5);
    }

    printf("\n  Q2_0_STATE_MATRIX=done\n");
    OSMesaDestroyContext(ctx);
    return 0;
}

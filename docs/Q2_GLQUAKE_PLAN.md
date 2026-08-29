# Q2 — GLQuake on OPENSTEP, on the card

`Q1` put software Quake on the screen at 16.0 fps and found the wall: the
frame is 62.5 ms and **41 of those are AppKit moving pixels**, at a measured
126 ns each.  That is a ceiling of 25.8 fps at 640x480 however fast the
renderer gets.

`Q2` goes around it.  GLQuake draws with OpenGL, this workspace's Mesa is
accelerated by its own Matrox G450 driver, and the frame need never enter
system memory.

---

## 1. Why this is smaller than it looks

Three of the four pieces already exist and are measured.

### 1.1 The engine is already here

The sdlquake tree carries **all fourteen `gl_*.c` files**.  GLQuake is not
being ported; it is being built.

### 1.2 The driver already accelerates what GLQuake draws

`osmgaMesaTexStateOK` accepts:

```
one 2D texture unit, no texgen        GLQuake's world pass
GL_REPLACE or GL_MODULATE             GLQuake's two env modes
GL_SINGLE_COLOR                       GLQuake never asks for more
minification and magnification filters may differ
```

That is the shape of GLQuake's world rendering, not a coincidence -- it is
the shape of every 1997 OpenGL program.

### 1.3 SDL2 already knows how to present from video memory

This is the part that would otherwise be the whole job.  `openstep-sdl2`
openstep.2 takes three function pointers from the application and delivers
GL frames video-memory to video-memory:

```c
static const SDL_OpenStepGLPresent hooks = {
    SDL_OPENSTEP_GLPRESENT_ABI, sizeof(hooks),
    OSMGAMesaBufferOrigin, OSMGAMesaBufferPresentMode,
    OSMGAMesaBufferPresentRect
};
SDL_SetWindowData(window, SDL_OPENSTEP_GLPRESENT_KEY, (void *)&hooks);
```

Measured with a teapot: **800x600 at 43.9 fps with no read-back at all**,
against 0.54 fps for the same frames delivered the ordinary way.  The
present costs 8.03 ms where AppKit costs 60.5.

**GLQuake calls `SDL_GL_SwapWindow` like any other SDL2 program**, so
registering those hooks is all it takes.  No new SDL2 work.

### 1.4 So the missing piece is one file

`gl_vidsdl.c` -- a GL video backend.  The tree has X11/GLX, 3dfx and NT
versions and no SDL one.

**Most of it is already written.**  `port/openstep/vid_sdl.c` (Q1) contains
the SDL2 input half -- `Sys_SendKeyEvents`, `IN_Init`, `IN_Shutdown`,
`IN_Commands`, `IN_Move` -- and that half is identical for GL.  What is new
is the video half:

```
VID_Init              SDL_CreateWindow(SDL_WINDOW_OPENGL) + GL context
                      + register the present hooks + GL_Init
VID_Shutdown          give them back
GL_BeginRendering     hand back x/y/width/height
GL_EndRendering       SDL_GL_SwapWindow
VID_SetPalette        GLQuake keeps its own 8-bit palette for 2D pieces;
VID_ShiftPalette      the GL path uses it to build 24-bit textures
VID_Is8bit            false -- see 3.2
VID_Init8bitPalette   a no-op with it
D_BeginDirectRect     no-op in GL builds (the GLX one is too)
D_EndDirectRect       no-op
```

---

## 2. What is genuinely unknown

Being honest about which of these is measured and which is not.

```
1. ~~Does Mesa 3.4.2 have what GLQuake asks for?~~  CHECKED -- yes.
   Every gl* identifier in the fourteen gl_*.c files (64 of them) against
   every gl* symbol libGL_mga.a defines (808).  Nine did not match, and
   none of them is a gap:
       glXChooseVisual glXCreateContext glXDestroyContext
       glXMakeCurrent glXSwapBuffers      X11.  We build gl_vidsdl.c
                                           instead, so these are never
                                           referenced
       glRect_s glRect_t                   not functions.  glRect_t is a
                                           lightmap rectangle struct; my
                                           pattern caught a type name
       glMTexCoord2fSGIS
       glSelectTextureSGIS                 reached ONLY through function
                                           pointers (qglMTexCoord2fSGIS,
                                           qglSelectTextureSGIS), which
                                           stay NULL unless the extension
                                           string advertises multitexture.
                                           No undefined symbol either way
   So the link has nothing missing.  That was the largest thing that could
   have made this plan collapse at Q2-1.

2. ~~Does the card have room for GLQuake's textures?~~  MEASURED -- yes,
   with about five times the room needed.

   What a level wants, counted from the miptex lump of all 23 BSPs and
   expanded the way GLQuake expands it (8-bit through the palette to 32
   bits, plus a mip chain, which is 4/3 of the top level):

       lq_e0m1   108 textures   735,488 texels   3.74 MB   <- the largest
       lq_e0m3    39            584,192          2.97 MB
       lq_e0m6    61            535,808          2.73 MB

   What the card has, from the driver's own arena test at each mode:

       512x384    18.86 MB       800x600   16.35 MB
       1024x768   14.61 MB       1600x1200  8.12 MB

   640x480 falls between the first two, so roughly 18 MB against 3.74.
   Lightmaps and model skins are added to that and are not counted here,
   but they would have to be four times the world's textures to matter.

3. Is the lightmap pass accelerated?
   GLQuake draws lightmaps as a second blended pass when it has no
   multitexture.  The hook's blend acceptance was tested (tbf, tsa) but
   NOT against this combination.

4. What does a GLQuake frame actually cost here?
   The teapot's 8.03 ms is the PRESENT.  It says nothing about uploading
   textures, changing state, or drawing a level's worth of geometry.

5. Does the software fallback stay correct?
   Anything the hook declines goes to Mesa's software rasteriser drawing
   into the same VRAM surface.  Correct by construction, and slow.  A
   GLQuake that silently falls back would be slower than squake and look
   identical.  This needs the same "is it really the card" reporting the
   teapot has.
```

**Number 5 is the one that decides whether this is worth finishing**, and it
is answerable early: the driver's counters (`drawn`, `warp`, `traps`,
`software`, `refused`) already exist and the teapot prints them.

---

## 3. Decisions taken up front

### 3.1 One binary, and it says which path it took

`glquake` links `libGL_mga.a` and prints the same evidence block the teapot
does -- surface origin, drawn, warp, trapezoids, software, refused,
read-backs.  **"It ran" is not evidence**; this project has written that
sentence before and meant it.

### 3.2 `VID_Is8bit` returns false

GLQuake's 8-bit path wants `GL_EXT_shared_texture_palette`, a 3dfx
extension.  Mesa 3.4.2 may expose it; it is not worth finding out first,
because the 24-bit path is the one the driver's texture acceptance was
written against.

### 3.3 The present hooks are registered by the engine, not by SDL2

That is the contract openstep.2 defines and the reason it needed no new
public symbol.  `gl_vidsdl.c` registers them in `VID_Init` after the
context exists, and clears them in `VID_Shutdown`.

### 3.4 Row order is SDL2's problem, not ours

SDL2 stamps a row at a time in reverse precisely so an application does not
have to flip its projection.  GLQuake's projection is left alone.

---

## 4. Milestones

```
Q2-1  gl_vidsdl.c compiles and the GL build links
Q2-2  it starts, makes a context, and reports GL_VERSION
Q2-3  a map draws
Q2-4  the evidence block says the card drew it, not Mesa
Q2-5  measure, against squake's 16.0 fps and the teapot's 43.9
Q2-6  the textures fit -- or say what does not
```

`Q2-4` is the milestone that matters.  A GLQuake drawing entirely in
software would pass `Q2-3` and look right.

---

## 5. Not doing

- **Not touching the software renderer.**  `squake` stays buildable and is
  the control: same data, same machine, a number to compare against.
- **Not chasing multitexture.**  If Mesa has no `GL_SGIS_multitexture`,
  GLQuake uses two passes, which is the path the driver's single-unit
  acceptance was written for anyway.
- **Not making it fullscreen.**  126 ns a pixel is the AppKit path's cost;
  the stamp's is 16.7.  But the operator's screen is 1600x1200 and that is
  1.9 million pixels of geometry either way -- a question for after Q2-5.

---

## 6. 교차검토 판정 (2026-08-30)

codex 회신을 주장 단위로 열어 확인했다.  **내가 둘 틀렸고, codex 가 하나
틀렸다.**

| 주장 | 검증 | 결과 |
|---|---|---|
| 아레나가 크기와 무관하게 32 블록만 받는다 | 코드 열람 | ✅ 사실 |
| 월드 텍스처의 기본 밉맵 필터를 거절한다 | 코드 열람 | ✅ 사실 |
| 라이트맵의 `GL_LUMINANCE` 를 거절한다 | 코드 열람 | ✅ 사실 |
| alias 모델의 `GL_SMOOTH` 를 거절한다 | 코드 열람 | ❌ **허위** |
| Mesa 가 `GL_EXT_shared_texture_palette` 를 노출한다 | 미검증 | ⏭ 행동이 안 바뀐다 |

### 6.1 내가 틀린 것 — §1.2 는 함수의 절반만 읽었다

`osmgaMesaTexStateOK` 의 텍스처 **환경** 조건(단일 2D · REPLACE/MODULATE ·
SINGLE_COLOR)을 읽고 "GLQuake 가 그리는 것을 드라이버가 받는다" 고 적었다.
같은 함수의 아래쪽에 **필터** 조건이 있다:

```c
if (t->MinFilter != GL_NEAREST && t->MinFilter != GL_LINEAR)
    return 0;
```

주석까지 달려 있다 -- *"네 가지 밉맵 필터는 여전히 거절된다: 그 레벨들이
어디 사는지 재지 않았다."*  GLQuake 의 기본 최소화 필터는
`GL_LINEAR_MIPMAP_NEAREST` 이므로, **월드의 모든 표면이 거절된다.**

텍스처 포맷도 같다: `GL_RGB`/`GL_RGBA` 만 받는데 GLQuake 의 기본 라이트맵은
`GL_LUMINANCE` 다.  **라이트맵 패스도 전부 거절된다.**

즉 기본 설정의 GLQuake 는 월드를 통째로 소프트웨어로 그린다.  §1.2 의
"드라이버가 이미 GLQuake 가 그리는 것을 가속한다" 는 **틀렸다.**

### 6.2 내가 틀린 것 — 바이트를 쟀는데 제약은 개수였다

§2-2 에서 "가장 큰 맵 3.74 MB, 아레나 18 MB, 다섯 배 여유" 라고 적었다.
아레나 할당기는 이렇다:

```c
#define OSMGA_TEX_BLOCKS 32
static OSMGATexBlock blocks[OSMGA_TEX_BLOCKS];
...
if (blockCount >= (unsigned long)OSMGA_TEX_BLOCKS)
    return 0;
```

**크기와 무관하게 32 개다.**  내가 편안하게 보이는 숫자를 하나 찾고 멈춘
것이고, 묶이는 자원이 바이트인지 확인하지 않았다.

직접 세어 확인했다 -- face 가 실제로 참조하는 서로 다른 월드 텍스처:

```
lq_e0m5 27   lq_e0m4 27   lq_e0m2 29   lq_e0m3 30   lq_e0m7 31
lq_e0m8 35   dev 38   lq_e0m6 42   start 46   lq_e0m1 65
```

열 맵 중 **다섯이 월드 텍스처만으로 32 를 넘는다.**  가장 적은 맵도 27 이라
라이트맵 아틀라스 한 장 올리면 끝이다.  UI·문자셋·모델 스킨·스프라이트는
세지도 않았다.

**32 는 하드웨어 한계가 아니라 정적 배열 크기다.**  그리고 그 파일의 머리
주석이 왜 32 인지 말한다 -- *"텍스처는 적고 크다... 할당기는 소수의 블록을
위해 쓰였다."*  GLQuake 는 그 전제를 정확히 뒤집는다: **많고 작다.**

### 6.3 codex 가 틀린 것 — `GL_SMOOTH` 는 거절되지 않는다

codex 는 드라이버가 smooth 다각형을 거절하므로 `gl_smoothmodels 0` 이
필요하다고 했다.  코드는 그렇지 않다.  `ctx->Light.ShadeModel == GL_FLAT`
은 **거절이 아니라 분기**이고, flat 이면 provoking vertex 를 넘기고 smooth
이면 `NULL` 을 넘긴다.  둘 다 `osmgaMesaWarpTriangle` 로 간다.  상태 게이트
전체에 ShadeModel 로 인한 `return 0` 이 없다.

**alias 모델은 그대로 두어도 된다.**

## 7. 그래서 계획이 바뀐다

`Q2` 는 "파일 하나만 쓰면 된다" 가 아니었다.  세 개의 선행 조건이 있고,
둘은 설정으로 풀리고 하나는 드라이버를 고쳐야 한다.

```
월드 텍스처 필터    gl_texturemode GL_LINEAR    설정.  화질 타협을 명시한다
라이트맵 포맷       -lm_4 (RGBA 라이트맵)       설정.  기본 LUMINANCE 대신
텍스처 블록 32 개   드라이버를 고쳐야 한다      배열 크기 상수 하나 +
                                                 libGL_mga.a 재빌드.
                                                 커널도 재부팅도 아니다
```

그리고 **검증 지점이 앞으로 온다.**  "조용한 소프트웨어 폴백" 은 `Q2-4` 가
아니라 **`Q2-0` 의 통과/중단 관문**이어야 한다: `gl_vidsdl.c` 를 쓰기 전에,
OSMesa 문맥 하나로 GLQuake 가 쓰는 상태 조합을 그대로 세워 놓고 하드웨어
삼각형이 늘어나는지 보는 작은 시험이면 된다.  창도 SDL 도 필요 없다.

```
Q2-0  상태 행렬 시험    월드(밉맵/LINEAR) · 라이트맵(LUMINANCE/RGBA) ·
                        alias(smooth/flat) · 텍스처 33 개.
                        각각에 대해 하드웨어 삼각형 증가분과 텍스처 거절을
                        센다.  여기서 막히면 gl_vidsdl.c 를 쓰지 않는다
Q2-1  블록 상한         32 를 올리고 재빌드.  얼마로 올릴지는 Q2-0 이 센
                        숫자가 정한다
Q2-2  gl_vidsdl.c       고정 640x480 · 크기변경 없음 · 창 하나 · 문맥 하나
Q2-3  한 맵             패스별로 하드웨어/소프트웨어를 따로 센다
Q2-4  측정              squake 의 16.0 fps 와 대조
```

`Q2-0` 이 이 계획에서 가장 값싼 지점이다.  틀린 전제 둘이 거기서 드러났을
것이고, 지금은 코드를 한 줄도 쓰기 전에 드러났다.

---

## 8. Q2-0 결과 (2026-08-30) — 코드를 쓰기 전에 답이 나왔다

`test/q2-state-matrix.c`.  창도 SDL 도 엔진도 없이 OSMesa 문맥 하나로
GLQuake 의 상태 조합을 하나씩 드라이버에 물었다.

```
  surface is the engine's : yes
  the probe says          : hardware

  arm                    result     counters
  world / mipmap         software   hard  0 soft  1 | drawn   0
  world / GL_LINEAR      HARDWARE   hard  1 soft  0 | drawn  43
  lightmap / LUMINANCE   software   hard  0 soft  1 | drawn   0
  lightmap / RGBA        HARDWARE   hard  1 soft  0 | drawn  43
  alias / GL_SMOOTH      HARDWARE   hard  1 soft  0 | drawn  43
  alias / GL_FLAT        HARDWARE   hard  1 soft  0 | drawn  43

  texture residency: 30번째 텍스처가 처음으로 자리를 못 찾았다
```

여섯 갈래가 전부 §6 의 판정과 일치한다.  **읽어서 내린 판정 셋이 측정으로
확인되었고, 논쟁이 있던 하나도 결판났다.**

### 8.1 `GL_SMOOTH` — codex 가 틀렸다는 것이 이제 측정으로도 확인됐다

읽기로는 "거절이 아니라 분기" 였고, 재 보니 `hard 1 · drawn 43` 이다.
**alias 모델은 손댈 필요가 없다.**  `gl_smoothmodels 0` 은 계획에서 뺀다.

### 8.2 두 설정이 월드를 되찾는다

```
gl_texturemode GL_LINEAR   world/mipmap 의 software 를 HARDWARE 로
-lm_4                      lightmap/LUMINANCE 의 software 를 HARDWARE 로
```

둘 다 엔진의 기존 설정이고 드라이버를 건드리지 않는다.  화질 타협은
명시한다: 밉맵 없는 텍스처는 멀리서 반짝인다(aliasing).  그것이 하드웨어
가속과 맞바꾸는 값이다.

### 8.3 남은 벽은 하나뿐이고, 그것은 우리 코드다

**30 번째 텍스처에서 자리가 떨어진다.**  앞선 갈래들이 이미 몇 개를 쥐고
있으므로 이는 32 블록 상한과 맞는 값이다.

레벨이 요구하는 것과 나란히 두면:

```
              월드 텍스처   + 라이트맵 아틀라스 · UI · 모델 스킨 · 스프라이트
lq_e0m5  27   가장 적은 맵조차 상한에 붙어 있다
lq_e0m1  65   두 배가 넘는다
```

`OSMGA_TEX_BLOCKS` 는 `static OSMGATexBlock blocks[32]` 의 크기이지 하드웨어의
한계가 아니다.  그 파일의 머리 주석이 왜 32 인지도 말한다 -- *"텍스처는 적고
크다... 할당기는 소수의 블록을 위해 쓰였다."*  **GLQuake 는 그 전제를
뒤집는다: 많고 작다.**

그러므로 `Q2-1` 은 그 상수를 올리고 `libGL_mga.a` 를 다시 세우는 것이다.
커널도 재부팅도 아니다.  얼마로 올릴지는 위의 65 와 라이트맵·UI·스킨을
더한 값이 정한다.  할당기가 원점 순 선형 탐색이므로 수백 개까지는
레벨 로딩 한 번의 비용으로 무시할 만하다 -- 다만 그것도 재고 나서 말한다.

### 8.4 이 단계가 값을 한 방식

`gl_vidsdl.c` 를 먼저 썼다면 이 여섯 줄을 얻는 데 파일 하나와 빌드 배관이
먼저 필요했고, 답이 나왔을 때는 이미 그것을 쓴 뒤였을 것이다.  지금은
**엔진 코드를 한 줄도 쓰지 않은 채** 세 개의 선행 조건과 그 크기를 안다.

---

## 9. Q2-1 결과 (2026-08-30) — 마지막 벽을 치웠다

`OSMGA_TEX_BLOCKS` 를 **32 에서 384 로** 올리고 `libGL_mga.a` 를 다시 세웠다.
커널도 재부팅도 아니다.

```
전:  texture 30 was the first with no room
후:  all 195 fitted (a level needs about 133)
```

### 9.1 숫자를 고른 방법

추정하지 않았다.  엔진의 라이트맵 패커(`gl_rsurf.c` 의 `AllocBlock`,
128x128 스카이라인)를 그대로 옮겨 각 레벨을 세었다 --
`tools/count-texture-blocks.py`:

```
              월드  라이트맵   합
lq_e0m1        65      12     77      <- 최악
lq_e0m6        42      21     63
lq_e0m2        29      24     53
...
```

여기에 고정 넷(문자셋·콘솔배경·스크랩 둘)과 모델 스킨·스프라이트 여유 40 을
더하면 **한 맵 133**.

그리고 **GLQuake 는 맵을 옮겨도 텍스처를 이름으로 재사용하며 해제하지
않는다.**  열 맵의 월드 텍스처 이름 합집합이 276 이므로 한 세션의 누적은
**344**.

```
N=384   배열 3,072 바이트   레벨 로딩 1회의 할당기 비용 0.60 ms
```

할당기는 원점 순 O(n) 탐색에 O(n) 삽입이라 384 에서도 로딩 한 번에
0.60 ms -- 16 fps 프레임 하나보다 작다.  그래서 **모양은 그대로 두고 크기만
키웠다**: 다른 자료구조로 바꾸면 그 자체로 정당성 논증이 필요해진다.

### 9.2 파일 머리 주석도 고쳤다

그 파일은 *"텍스처는 적고 크다... 할당기는 소수의 블록을 위해 쓰였다"* 고
적고 있었다.  그 문장은 이 변경 뒤에 **틀린 말**이 되므로 함께 고쳤다.
코드와 어긋난 주석을 남기는 것이 이 저장소가 피해 온 부채다.

### 9.3 시험이 요구 수준에 닿게 했다

처음 재빌드 뒤 시험은 `all 59 fitted` 라고 했다.  **59 는 133 보다 작다.**
요구 수준에 닿지 않는 증거로 "고쳤다" 고 적을 수는 없어서, 시험의 텍스처
수를 200 으로 올려 다시 돌렸다.  `all 195 fitted`.

### 9.4 남은 것

세 선행 조건이 모두 답을 얻었다:

```
월드 텍스처 필터   gl_texturemode GL_LINEAR   설정 -- Q2-0 이 측정
라이트맵 포맷      -lm_4                       설정 -- Q2-0 이 측정
텍스처 블록        32 -> 384                   고침 -- Q2-1 이 확인
```

다음은 `Q2-2`, `gl_vidsdl.c` 다.  이제 그 파일을 쓰는 것이 값이 있다는 근거가
있다.

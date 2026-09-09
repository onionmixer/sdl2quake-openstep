# Q1 — sdlquake 를 SDL2/OPENSTEP 으로, LibreQuake 데이터로

**목표**: [mckayemu/sdlquake](https://github.com/mckayemu/sdlquake) 엔진을
우리 SDL2/OPENSTEP 포트 위로 옮기고,
[LibreQuake](https://github.com/lavenderdotpet/LibreQuake) 데이터로 구동한다.
기본 해상도 **640×480**.

이 문서는 **계획**이다.  아래 §1 의 숫자는 전부 실측이고, 추정은 추정이라고
적었다.

---

## 1. 재고 나서 시작한다 — 이미 잰 것

### 1.1 기계

```
hostinfo          NeXT Mach 4.2, RELEASE_I386, "Processor type: I386 (Intel 486)"
메모리            504 MB          <- 486 보드가 담을 수 있는 양이 아니다.
                                     "486" 은 Mach 의 거친 분류이지 부품이 아니다
정수               441 Mops/s
memset            1.68 GB/s
텍스처 스팬       640×480 한 프레임에 1.18 ms
```

**CPU 는 이 작업의 제약이 아니다.**  Quake 의 소프트웨어 래스터라이저가
쓰는 모양(텍셀 읽기 + 픽셀 쓰기 + 고정소수점 전진)으로 640×480 을 채우는 데
1.18 ms 다.  원본 Quake 가 목표로 한 Pentium 75 보다 한참 위다.

### 1.2 화면에 올리는 값 — 여기가 문제다

우리 SDL2 포트의 2D 경로를 640×480 에서 쟀다 (120 프레임):

```
8비트 → 창 표면 blit        1.17 ms
SDL_UpdateWindowSurface    43.06 ms
wall                       44.41 ms   =  22.5 fps 천장
```

**Quake 가 아무것도 그리기 전에 22.5 fps 다.**

그 43 ms 를 갈랐다.  백엔드는 더티 사각형만 32→24 로 변환하고 나서
`displayRect:` 에 **뷰 전체**를 넘긴다.  그래서 1픽셀 갱신은 변환을 거의 안
하면서 AppKit 비용은 그대로 낸다:

```
전체 창 갱신      38.29 ms
1픽셀 갱신        45.45 ms     <- AppKit 만
차이              -7.16 ms     <- 변환은 잡음에 묻힌다
같은 루프, 로컬     1.51 ms     <- 증인
```

**변환은 프레임의 4% 다.  고쳐도 아무것도 얻지 못한다.**

**이 실험의 약한 곳을 적어 둔다.**  1픽셀 갱신이 전체 갱신보다 **7 ms
느리게** 나왔다.  전체가 하는 일을 다 하지 않는데 더 비쌀 수는 없으므로,
이 두 값의 차이는 신호가 아니라 **잡음**이다 (실행간 편차가 ±10 ms 쯤 된다는
뜻이다).  그러므로 "AppKit 이 지배한다"는 결론은 **뺄셈이 아니라 세 번째
줄이 떠받친다**: 같은 변환을 같은 크기로 로컬에서 돌리면 1.51 ms 이고,
전체 프레임은 41 ms 다.  그 둘은 서로 독립적인 측정이다.

크기를 바꿔가며 재니 완전히 선형이다:

```
 320×200     8.15 ms   122.7 fps   127.4 ns/화소
 320×240     9.74 ms   102.7 fps   126.8 ns/화소
 400×300    15.09 ms    66.3 fps   125.7 ns/화소
 512×384    24.61 ms    40.6 fps   125.2 ns/화소
 640×480    41.18 ms    24.3 fps   134.1 ns/화소
 800×600    60.50 ms    16.5 fps   126.0 ns/화소
```

**AppKit 의 전달은 화소당 126 ns 다**, 크기와 무관하게.

대조군: 우리가 이번 주에 만든 **VRAM 도장은 800×600 에 8.03 ms** —
화소당 **16.7 ns**.  **7.5 배 싸다.**  640×480 이면 약 5.1 ms 이므로 천장이
약 195 fps 가 된다.

### 1.3 그런데 도장은 GL 경로에만 붙어 있다

도장이 성립하는 이유는 드라이버가 **OSMesa 문맥 바인드 때 VRAM 표면을
대입**하기 때문이다.  그림이 이미 VRAM 에 있으니 VRAM→VRAM 으로 옮긴다.

Quake 의 소프트웨어 렌더러는 GL 을 한 줄도 쓰지 않는다.  8비트 팔레트
픽셀을 시스템 메모리에 쓴다.  **그래서 SDL1 이든 SDL2 든, 소프트웨어
Quake 를 그대로 옮기면 가속은 붙지 않는다.**  SDL1/SDL2 는 이 문제를
결정하지 않는다.

### 1.4 엔진

```
저장소            mckayemu/sdlquake, GPL-2.0, 252 파일
SDL 버전          1.2 -- configure 가 sdl-config 를 찾고(SDL2 는 sdl2-config),
                  SDL_byteorder.h 를 include 하며, SDL_SetVideoMode /
                  SDL_SetColors / SDL_WM_SetCaption / SDL_WarpMouse /
                  SDL_CD* 를 쓴다.  전부 SDL2 에서 없어졌다
어셈블리          **.S 파일 21 개** (d_draw.S, d_polysa.S, r_edgea.S ...).
                  처음에 "0 개" 라고 적었는데 소문자 .s 만 셌다 -- 없다는
                  주장을 전수로 확인하지 않은 것이고, 교차검토가 잡았다.
                  quakedef.h 가 id386 을 0 으로 둘 수 있으므로 C 대체
                  구현은 있지만, **빌드 구성에서 .S 를 빼는 것이 Q1-1 의
                  전건**이다 -- id386=0 만으로는 Makefile 이 여전히
                  어셈블한다
GL 렌더러         gl_*.c 14 개가 전부 들어 있다 -- 저장소 설명은
                  "rasterizer only" 라고 하지만 사실이 아니다.
                  **그러나 gl_vidsdl.c 는 없다**: gl_vidlinux.c(3dfx),
                  gl_vidlinuxglx.c(GLX), gl_vidnt.c 뿐이다.  Route C 는
                  SDL 창·문맥·스왑·입력을 묶는 **GL 비디오 백엔드를 새로
                  써야 한다**
NeXT 흔적         snd_next.c 가 있다 (원본 Quake 의 NeXTSTEP 백엔드)
```

SDL 을 부르는 파일은 **넷뿐**이다:

```
vid_sdl.c    389 줄   SetVideoMode(8bpp HWPALETTE) · SetColors · UpdateRects
                      · 이벤트 · 마우스           <- 대부분의 일이 여기 있다
                      기본 해상도가 BASEWIDTH(320*2) x BASEHEIGHT(200*2)
                      = **640x400** 이다.  640x480 은 명시적 변경이다
snd_sdl.c    112 줄   OpenAudio · PauseAudio · CloseAudio
                      **그대로 쓸 수 없다** -- §3.2
cd_sdl.c     223 줄   SDL_CD*                     <- SDL2 에 없다.  버린다
sys_sdl.c    454 줄   SDL_Delay 하나.  memsize 가 8 MiB 로 고정이고
                      -heapsize 를 읽지 않는다 (`sys_sdl.c:382`)
```

### 1.5 데이터

```
LibreQuake        v0.09-beta (2025-11-05), 예술자산 BSD
lite.zip          56 MB -> id1/pak0.pak 44 MB + pak1.pak 5.9 MB
BSP 버전          전 23 맵이 29 -- 바닐라 포맷이다.  BSP2 가 아니다
가장 큰 맵        lq_e0m2.bsp 2.24 MB  (원본 Quake 최대는 e4m2 약 1.9 MB)
내용              pak0 에 wav 190, mdl 63, lmp 56, bsp 22, lit 8
                  pak1 에 bsp 1 -- **합쳐 23 맵**
```

**BSP 29 라는 것이 이 계획을 가능하게 한다.**  1996년 엔진이 여는 포맷이다.

**그리고 한계 안에 들어온다 — 재 봤다.**  23 맵의 lump 표를 전부 파싱해
엔진의 상수와 대조했다.  `MAX_MAP_LEAFS 8192` 는 장식이 아니라
`model.c` 의 `Mod_DecompressVis` 가 `static byte decompressed[MAX_MAP_LEAFS/8]`
로 실제로 쓰는 값이다.

```
                   최대치     한계    비율   (가장 큰 맵)
  leafs             3284     8192    40.1%   lq_e0m2
  clipnodes        15403    32767    47.0%   lq_e0m2
  texinfo            949     4096    23.2%   lq_e0m1
  nodes             5613    32767    17.1%   lq_e0m2
  marksurfaces     10800    65535    16.5%   lq_e0m2
  planes            5178    32767    15.8%   lq_e0m2
  vertexes          9453    65535    14.4%   lq_e0m2
  faces             9044    65535    13.8%   lq_e0m2
  edges            19132   256000     7.5%   lq_e0m2
```

가장 빡빡한 것이 절반이 안 된다.  **파일이 원본보다 큰 것은 텍스처와
조명이지 기하가 아니었다.**  Q1-2 의 위험은 이것으로 크게 줄었다 — 남는
것은 런타임 쪽(`MAX_EDICTS 600`, `MAX_MODELS 256` 프리캐시)이고, 그것은
맵을 열어봐야 안다.

---

## 2. 그래서 무엇을 만드는가 — 세 갈래

```
A  소프트웨어 Quake, 2D 경로       오늘 된다.  640×480 에서 천장 24 fps,
                                   Quake 자체 비용을 빼면 15~18 fps 로 본다(추정)
B  소프트웨어 Quake, 작은 모드     320×240 이면 천장 103 fps.  넉넉하다.
                                   그러나 640×480 이라는 목표를 버린다
C  GLQuake                         카드가 래스터라이즈하고 VRAM 도장으로
                                   전달한다.  우리가 만든 것이 전부 적용된다
```

`A` 와 `B` 는 같은 코드다 — 해상도만 다르다.  `C` 는 다른 렌더러다.

### 2.1 순서: **최소 `A` → 바로 `C` 측정**, `B` 는 제품 폴백

처음에 "`A` 를 완성하고 그 다음 `C`" 라고 적었다.  **과했다.**  두 가지가
그 순서를 무너뜨린다:

- **`C` 는 `A` 의 `vid_sdl.c` 를 재사용하지 않는다.**  이 트리의 GL 비디오
  코드는 3dfx/GLX/NT 뿐이고 SDL 판이 없다(§1.4).  `C` 에는 창·문맥·스왑·
  입력을 묶는 **새 백엔드**가 필요하다.  `A` 를 다듬는다고 `C` 가 앞당겨지지
  않는다.
- **22.5 fps 벽은 이미 측정됐다.**  아직 모르는 것에 시간을 쓰는 것이 맞지,
  이미 아는 벽을 다듬는 데 쓸 이유가 없다.

그러나 `A` 를 건너뛰지도 않는다.  `A` 만이 답하는 것이 있다: pak 이 열리는가,
맵이 로드되는가, 게임 로직·입력·소리가 이 플랫폼에서 도는가.  그래서:

```
공통          빌드, pak, BSP 사전검사, 오디오 형식      <- A 와 C 가 함께 쓴다
최소 A        콘솔 · 맵 하나 로드 · 한 프레임 · 키/마우스
              여기까지만.  튜닝하지 않는다
바로 C        SDL-GL 백엔드 스파이크 + 대표 맵 프레임 측정
B             C 가 목표를 못 내면 320x240 을 제품 기본값으로
```

**`C` 가 8 ms 라는 증거는 아직 없다.**  VRAM 도장이 800×600 에 8.03 ms 인
것은 **표시** 경로의 값이지, GLQuake 의 텍스처 업로드·상태 변경·월드 렌더를
포함한 값이 아니다.  그래서 `C` 도 측정 대상이지 결론이 아니다.

---

## 3. `A` 의 작업 — SDL1 → SDL2

### 3.1 `vid_sdl.c` — 다시 쓴다

SDL2 에는 **팔레트 창 표면이 없다.**  Quake 는 8비트로 그린다.  그래서:

```c
/* 창은 32비트.  Quake 의 8비트 표면은 우리가 들고 blit 한다. */
window  = SDL_CreateWindow(..., 640, 480, 0);
screen  = SDL_GetWindowSurface(window);          /* RGB888, 실측 확인 */
vid8    = SDL_CreateRGBSurfaceWithFormat(0, w, h, 8, SDL_PIXELFORMAT_INDEX8);
vid.buffer = vid8->pixels;                       /* Quake 가 여기에 그린다 */

/* VID_SetPalette */
SDL_SetPaletteColors(vid8->format->palette, colors, 0, 256);

/* VID_Update */
SDL_BlitSurface(vid8, NULL, screen, NULL);       /* 실측 1.17 ms */
SDL_UpdateWindowSurface(window);                 /* 실측 41 ms */
```

`vid.rowbytes` 는 **`vid8->pitch` 로 잡는다**.  폭과 pitch 가 같다고
가정하면 안 된다 -- SDL 은 행을 정렬한다.

그리고 **전체 프레임에는 rect 판을 쓰지 않는다.**  백엔드가 어차피 뷰
전체를 다시 그리므로(§1.2), rect 는 변환량만 줄이고 AppKit 전달량은 줄이지
못한다.  Quake 는 매 프레임 화면 전체를 바꾸므로 얻을 것이 없다.

바뀌는 호출:

```
SDL_SetVideoMode        -> SDL_CreateWindow + SDL_GetWindowSurface
SDL_SetColors           -> SDL_SetPaletteColors
SDL_UpdateRects         -> SDL_UpdateWindowSurfaceRects
SDL_WM_SetCaption       -> SDL_SetWindowTitle
SDL_WarpMouse           -> SDL_WarpMouseInWindow
SDL_GetModState         -> 그대로 있다
키 이벤트                SDLKey -> SDL_Keycode, event.key.keysym.sym 은 유지되나
                         상수 이름이 SDLK_* 그대로다.  대부분 그대로 컴파일된다
SDL_MOUSEMOTION          그대로.  상대 이동은 SDL_SetRelativeMouseMode 가 낫다
```

### 3.2 `snd_sdl.c` — 헤더 이름 하나와, 내가 틀린 진단 하나

**이 절의 첫 판은 "모노인데 스테레오를 못박아서 소리가 절반 속도로
어긋난다" 고 적었다.  틀렸다.**  줄 17 만 읽고 뒤의 협상 코드를 안 읽은
것이다.

`SNDDMA_Init` 은 스테레오 S16LSB 를 요청하고, 장치가 그것을 못 주면
`default:` 갈래로 떨어져 닫았다가 **`obtained == NULL` 로 다시 연다**.
SDL2 원본을 열어 확인했다 (`SDL_audio.c:1587`): 그 경우
`allowed_changes = 0` 으로 열어 **변환 스트림을 붙여 요청한 형식 그대로**
준다.  그리고 `memcpy(&obtained, &desired, ...)` 로 `shm->channels` 가 2 가
된다.  즉 `/2` 는 맞고, 엔디안도 SDL 이 처리한다.

실제로 필요한 변경은 **헤더 이름 하나**다:

```
SDL_byteorder.h  ->  SDL_endian.h     SDL_BYTEORDER / SDL_LIL_ENDIAN 등
                                       이름은 그대로다
```

그 밖에 둘을 손봤다:

```
samplepos 의 /2 -> /shm->channels     결함 수정이 아니라, 구조체가
                                       말하는 대로 쓴 것.  두 값은 오늘
                                       일치한다
SNDDMA_Submit           추가          snd_dma.c 가 무조건 부르는데
                                       snd_sdl.c 는 정의한 적이 없다.
                                       리눅스 빌드는 snd_linux.c 를 나란히
                                       링크해서 가려져 있었다.  상류의
                                       그것도 빈 함수다 (콜백이 끌어가는
                                       모델이라 보낼 것이 없다)
```

### 3.3 `cd_sdl.c` — 버린다

SDL2 에 CD 오디오 API 가 **없다.**  `cd_null.c` 로 링크한다.  LibreQuake 는
사운드트랙을 데이터에 넣지 않으므로(README 가 명시) 잃는 것이 없다.

### 3.4 `sys_sdl.c` — `SDL_Delay` 하나

그대로.  다만 OPENSTEP 쪽 손질이 필요하다 — §4.

### 3.5 링크

**GL 을 안 써도 Mesa 를 링크해야 한다.**  실측으로 확인했다: 순수 2D
프로그램조차 `libSDL2.a` 안의 GL 백엔드 때문에 `_OSMesaCreateContext` 등
8 개 심볼을 요구한다.

```
cc -O -m486 -D__OPENSTEP__ -I<sdl>/include ... \
   libSDL2.a libGL.a -lm \
   -framework AppKit -framework Foundation -framework SoundKit
그리고 fix-macho-i486-subtype.csh
```

---

## 4. OPENSTEP 이 물리는 곳

이 워크스페이스가 이미 값을 치른 것들이다.  Quake 소스에 그대로 적용된다:

```
.S 21 개          빌드 구성에서 빼고 C 대체 구현만 고른다.  id386=0 은
                  소스 쪽 스위치일 뿐, Makefile 은 여전히 어셈블한다
                  -- 자세한 것은 §10
memsize 8 MiB     sys_sdl.c:382 가 고정하고 -heapsize 를 읽지 않는다.
                  RAM 이 504 MB 인 것은 이 문제를 풀지 않는다.
                  -heapsize 를 실제로 지원하거나 16~32 MiB 로 올린다
Sys_FloatTime     gettimeofday 는 벽시계다.  뒤로 갈 수 있다.  단조 시계를
                  쓰거나 마지막 값으로 clamp 한다.  분해능도 재야 한다
상대 마우스       SDL_SetRelativeMouseMode 가 이 포트에서 되는지 증명된 바
                  없다.  안 되면 warp 로 폴백하되 warp 가 만든 가짜 이동을
                  버려야 하고, 초점을 잃을 때 버튼·델타를 정리해야 한다
프레임 리미터     이 루프에 없다.  C 가 빨라지면 CPU 를 다 쓰고 게임 속도와
                  입력 감도가 드러난다
cc 2.7.2.1        C89 만.  선언은 블록 앞.  // 주석 없음
long long         -O 에서 두 long long 을 &&/|| 로 묶으면 결과가 뒤집힌다.
                  Quake 는 long long 을 거의 안 쓰지만 확인해야 한다
`out`             커널 빌드에서 매크로.  유저랜드에서는 문제 없을 것으로 보나
                  Quake 에 out 이라는 파라미터가 많다 -- 확인 항목
-mpentiumpro      Makefile 이 요구한다.  우리는 -m486 이어야 한다
                  (cpusubtype 이 오염되면 실행 파일이 안 뜬다)
-O6 -ffast-math   gcc 2.7 에 없다.  -O 로 내린다
stricmp           Makefile 이 strcasecmp 로 정의한다.  OPENSTEP 에 있는지 확인
파일 경로         pak 안은 소문자.  OPENSTEP 파일시스템은 대소문자 구분
```

`snd_next.c` 가 있다는 것은 원본 Quake 가 NeXTSTEP 에서 빌드된 적이 있다는
뜻이고, 그 파일이 그 시절 이식의 흔적을 담고 있다.  **읽어볼 값이 있다** —
우리가 이제 마주칠 문제를 1996년에 누군가 이미 만났다.

---

## 5. 이정표

```
Q1-0  사전검사              **끝났다** -- tools/check-bsp-limits.py, §9
Q1-1  빌드가 선다           .S 를 빼고 C 대체로 컴파일·링크된다
Q1-2  데이터가 읽힌다       pak 이 열리고 콘솔이 뜬다
Q1-3  한 프레임이 보인다    640×480 창에 뭔가 그려진다 (기본값이 640×400
                            이므로 명시적 변경)
Q1-4  움직인다              입력·타이밍·게임 루프.  fps 를 잰다
Q1-5  소리가 난다           모노 samplepos 와 MSB 변환을 고친 뒤
Q1-6  전 맵 로드            23 맵을 하나씩 열고 r_reportsurfout /
                            r_reportedgeout 를 읽는다.  로드되는 것과
                            온전히 그려지는 것은 다른 질문이다
--- 여기까지가 "최소 A" 다.  튜닝하지 않는다 ---
Q1-7  C 스파이크            SDL-GL 비디오 백엔드를 새로 쓴다
Q1-8  C 를 잰다             대표 맵에서 프레임 비용.  여기서 A/B/C 를 정한다
```

`Q1-6` 이 가장 위험하다.  정적 한계는 통과했지만(§1.5), 소프트웨어
렌더러는 **자체 런타임 한계**를 갖는다 — `NUMSTACKEDGES 2400`,
`NUMSTACKSURFACES 800` 에서 시작해 부족하면 `r_outofedges` /
`r_outofsurfaces` 를 세고 **표면을 그냥 버린다**.  맵이 열려도 덜 그려질
수 있고, 엔진은 그것을 세고 있으므로 물어보면 답한다.

---

## 6. 새 저장소로 만든다

`openstep-quake/` 를 이 워크스페이스의 다른 프로젝트와 같은 규칙으로 만든다:
자립 구성, 실 IP 없음, 공개는 subtree split.

**upstream 은 손대지 않고 overlay 로 유지한다** — `openstep-sdl20` 이
`upstream/SDL-2.32.10/` 과 `port/openstep/` 을 가르는 것과 같은 모양이다.

**GPL-2.0 의 의무를 릴리즈 전에 정리한다.**  실행 파일을 배포하면
대응하는 완전한 소스, 변경 고지, 빌드·설치 스크립트, GPL 사본을 함께
내야 한다.  정적으로 링크되는 우리 SDL2 와 Mesa 도 그 "대응 소스" 범위에
들어오는지 판정해야 한다(둘 다 공개돼 있으므로 어렵지 않으나, 판정은
해야 한다).  LibreQuake 데이터는 저장소에 넣지 않는다(56 MB); 배포한다면
그 자산의 라이선스·버전·해시·취득 방법을 릴리즈 문서에 적는다.

---

## 7. 전건 — 닫힌 것과 남은 것

교차검토가 이 중 다섯을 바꿔 놓았다.  **닫힌 것**:

```
LibreQuake 맵이 1996년 정적 한계 안에 드는가   들어온다.  가장 빡빡한
                                               clipnodes 가 47% (§1.5)
어셈블리가 문제인가                            .S 21 개가 있다.  내가 "0 개"
                                               라고 적었던 것이 틀렸다 -- 소문자만
                                               셌다.  빌드에서 빼면 된다
C 가 A 의 vid 를 재사용하는가                  안 한다.  gl_vidsdl.c 가 없다
시스템->VRAM 이 커널 작업인가                  아니다.  드라이버가 이미 유저
                                               태스크에 VRAM 을 mmap 한다
snd_sdl.c 를 그대로 쓸 수 있는가               없다.  samplepos 가 스테레오를
                                               못박았는데 장치는 모노다
```

**남은 것**:

```
1. 소리의 변환을 어디서 하나
   장치가 AUDIO_S16MSB 모노다.  믹서 포맷과 장치 포맷 사이를 SDL 에
   맡길지 우리가 할지, 그리고 samplepos 를 shm->channels 로 나누도록
   고치는 것
2. 소프트웨어 렌더러의 런타임 한계
   NUMSTACKEDGES 2400 / NUMSTACKSURFACES 800 에서 시작해 부족하면
   표면을 버린다.  Q1-6 에서 r_reportsurfout / r_reportedgeout 로 본다
3. CPU -> 매핑된 VRAM 쓰기 대역폭
   Route D 를 판단하려면 필요한데 `tvr` 이 지금 그 창을 매핑하지 못한다
   ("the window will not map").  원인 미상
4. GLQuake 한 프레임이 실제로 얼마인가
   VRAM 도장 8.03 ms 는 표시 경로의 값이지 월드 렌더를 포함하지 않는다
5. SDL_SetRelativeMouseMode 가 이 포트에서 되는가
6. 640×480 에서 Quake 자체가 프레임당 얼마를 쓰는가
7. `out` 과 long long 이 Quake 소스에서 실제로 문제인가
```

---

## 8. 하지 않는 것

- **Route D (2D 경로에 VRAM 도장 붙이기) 를 지금 설계하지 않는다.**
  다만 처음에 적은 이유는 **틀렸다**: "시스템→VRAM 업로드는 커널 작업" 이
  아니다.  드라이버는 **이미 오프스크린 VRAM 창을 유저 태스크에 mmap 해
  준다** (S4a, 2026-08-19 실기 통과: mmap OK, 유저 R/W PASS).  그러므로
  유저 공간이 매핑된 VRAM 에 직접 CPU store 하는 것 자체는 가능하다.

  미루는 진짜 이유는 셋이다: (1) `tvr` 이 지금 그 창을 매핑하지 못한다
  (원인 미상), (2) CPU→매핑된 VRAM 쓰기 대역폭을 잰 적이 없다, (3) 현재
  Mesa 경로는 32bpp 를 전제하는데 Quake 는 8bpp 이므로 8→32 변환이 붙는다.
  **세 가지를 재고 나서 판단한다.**
- **AppKit 전달을 최적화하지 않는다.**  126 ns/화소 중 우리 몫은 1.51 ms,
  4% 다.  나머지는 창 서버 안에 있고 우리 코드가 아니다.
- **SDL1.2 포트를 쓰지 않는다.**  Mesa 연동이 없고, `C` 로 가는 길이 막힌다.
- **멀티플레이어를 목표로 하지 않는다.**  네트워크 코드는 컴파일만 되게 하고
  검증은 나중이다.

---

## 9. Q1-0 결과 (2026-08-29)

`tools/check-bsp-limits.py` 를 만들어 돌렸다.  **23 맵 · 81 모델 · 3
스프라이트 전부 통과.**

```
Q1_0_BSP_PRECHECK=pass
가장 넓은 surface extent 256 (엔진의 한계는 "> 256" 이므로 256 은 합법)
가장 큰 맵 lq_e0m2: leafs 3284 · faces 9044 · marksurfaces 10800
```

### 9.1 이 검사기가 무엇인가

**엔진의 로더가 실제로 죽는 자리를 옮겨 적은 것**이다.  `model.c` 의
`Sys_Error` 를 전수로 뽑아, BSP 를 여는 경로에서 도달 가능한 것을 전부
구현했다 — 버전, "funny lump size" 열한 곳, 텍스처 16 정렬, 애니메이션
프레임의 완결성, `miptex >= numtextures`, marksurface 범위, 그리고
`CalcSurfaceExtents` 의 `> 256`.  각 검사에 `model.c` 의 줄번호를 달아
두었으므로 읽는 사람이 반박할 수 있다.

엔진이 **검사하지 않는** 것도 몇 개 넣었다 — edge 의 정점 번호, surfedge
범위, node/clipnode 자식, leaf 의 marksurface 구간, submodel 의 face 구간.
엔진은 이것들을 믿고 읽으므로, 어긋나면 오류 메시지가 아니라 엉뚱한 읽기가
된다.  미리 보는 편이 낫다.

### 9.2 처음에 열 맵이 실패했고, 열 다 내 잘못이었다

**이것이 이 단계에서 가장 값진 부분이다.**  첫 판은 이렇게 보고했다:

```
Bad animating texture +ablink        (아홉 맵)
leaf 3 marks 65280..65280 of 5703    (한 맵)
```

둘 다 검사기의 버그였다:

- 엔진은 `+` 다음 글자를 **대문자로 바꾼 뒤에** 검사한다
  (`if (max >= 'a' && max <= 'z') max -= 'a' - 'A';`, model.c:421).
  `+ablink` 는 대체 애니메이션의 A 프레임이지 오류가 아니다.
- `dleaf_t` 의 `firstmarksurface` 는 오프셋 **20** 인데 24 에서 읽었다.
  contents(4) visofs(4) mins[3](6) maxs[3](6) 다음이다.

**데이터를 의심하기 전에 도구를 의심해야 한다는 것이 여기서 값을 치렀다.**
"열 맵 중 아홉이 깨졌다" 는 결과는 그 자체로 의심스러웠고, 의심이 맞았다.

### 9.3 통과가 공허하지 않다는 증거

일부러 망가뜨린 맵 다섯을 만들어 전부 잡히는지 확인했다:

```
version 30 으로            -> wrong version number
faces lump 길이 -1         -> funny lump size (faces)
marksurface 를 30000 으로   -> bad surface number
텍스처 폭을 17 로           -> Texture skip is not 16 aligned
정점을 40 배로              -> Bad surface extents (extent 8960 > 256)
```

다섯 다 잡혔다.  검사기가 무엇이든 통과시키는 것이 아니다.

### 9.4 이것이 답하지 않는 것

**맵이 제대로 그려지는지는 모른다.**  소프트웨어 렌더러는 실행 중에 edge 와
surface 가 모자라면 `r_outofedges` / `r_outofsurfaces` 를 세고 표면을 그냥
버린다 (`NUMSTACKEDGES 2400`, `NUMSTACKSURFACES 800` 에서 시작).  로드되는
것과 온전히 그려지는 것은 다른 질문이고, 그것은 Q1-6 에서 엔진을 돌려
`r_reportsurfout` / `r_reportedgeout` 로 묻는다.

또한 런타임 한계인 `MAX_EDICTS 600` 과 `MAX_MODELS 256` 프리캐시는 맵을
열어봐야 안다.

---

## 10. 어셈블리를 버리는 것인가 (2026-08-29)

**"어셈블러가 렌더링을 담당하니 살리면 안 되는 것 아닌가" 에 대한 답이다.**

### 10.1 그렇다, 저것이 빠른 렌더러다 — 그러나 유일한 것은 아니다

21 개를 셋으로 가른다:

```
렌더러 안쪽 고리 13
  d_draw.S      8bpp 수평 스팬          d_draw16.S  16화소 단위 스팬
  d_scana.S     난류 텍스처 매핑        d_spr8.S    투명 스팬
  d_parta.S     8bpp 입자              d_polysa.S  폴리곤 모델
  surf8.S       8bpp 표면 블록          surf16.S    16bpp 표면 블록
  r_drawa.S     엣지 클리핑·방출        r_edgea.S   엣지 처리
  r_aliasa.S    Alias 변환·투영         r_aclipa.S  Alias 클립
  worlda.S      서버 쪽

지원 3           math.S(mathlib.c) · snd_mixa.S(snd_mix.c) ·
                 d_varsa.S/r_varsa.S(변수 선언뿐)

우리와 무관 3    dosasm.S · sys_dosa.S · sys_wina.S -- DOS/Windows 전용.
                 애초에 빌드 대상이 아니다
```

**전부 C 쌍둥이가 있다 -- 표본이 아니라 전수로 확인했다.**  각 C 파일 안에
`#if !id386` 로 들어 있고 (`d_polyse.c`, `d_scan.c`, `mathlib.c` ...),
그것이 Quake 가 Alpha·MIPS·PowerPC 로 출하된 경로다.  버리는 것이 아니라
**다른 구현을 고르는 것**이다.

`.S` 가 `C(...)` 로 내보내는 심볼 **68 개**를 뽑아 C 쪽과 대조했다.  처음
24 개가 "C 정의 없음" 으로 나왔는데, **또 파서 탓이었다** -- 그리고 이번에도
데이터가 아니라 도구를 먼저 의심한 것이 맞았다:

```
13  변수      d_vars.c 가 전부 정의한다.  `float d_sdivzstepu,
              d_tdivzstepu, d_zistepu;` 처럼 쉼표로 묶여 있어 내
              정규식이 첫 이름만 잡았다.  열셋을 하나씩 대조했다
10  코드      R_EdgeCodeStart/End · R_Surf8Start/End · R_Surf16Start/End ·
              D_PolysetAff8Start/End · D_DrawSpans16 · D_Aff8Patch.
              전부 `#if id386` 안에서만 참조된다.  앞의 여덟은
              Sys_MakeCodeWriteable 에 넘기는 자기수정 코드 범위 표지다
 1  DOS 전용  VGA_UpdateLinearScreen -- vid_vga.c / vid_ext.c 에만 있고
              그 둘은 빌드 대상이 아니다
```

**실제 빈 곳은 0 이다.**  `id386=0` 이면 어셈블리 전용 심볼을 아무도
참조하지 않고, 변수는 전부 C 정의가 있다.

### 10.2 살릴 수 있는가 — 추측하지 않고 어셈블해 봤다

처음에 `.align` 의 뜻이 Mach-O 와 ELF 에서 다르다는 것과 NeXT 의 `as` 가 옛
GAS 파생이라는 것을 불리한 사실로 적고 "확인 안 한 추측" 이라고 달았다.
**추측할 이유가 없었다.  기계가 있다.**

DOS/Windows 전용 셋을 뺀 **18 개를 전수로 어셈블했다**
(`tools/check-asm-assembles.sh`):

```
ASM_ASSEMBLE ok=18 fail=0
```

```
math.S      -> _BoxOnPlaneSide · _Invert24To16 · _TransformVector
r_edgea.S   -> _R_StepActiveU · _R_SurfacePatch, 그리고 C 쪽 전역으로의
               미정의 참조 (_edge_head, _surfaces ...)
둘 다        Mach-O relocatable (for architecture i386)
```

`asm_i386.h` 가 ELF 가 아닐 때 `#define C(label) _##label` 로 줄어드는 것이
**Mach-O 의 밑줄 규약 그대로**였고, 쓰이는 지시어가
`.globl` · `.align` · `.text` · `.data` · `.long` 뿐이라 옛 `as` 도 받는다.

**그러나 어셈블되는 것과 도는 것은 다르다.**  `.S` 들은
`asm_i386.h` 안에 구조체 오프셋을 **손으로 적힌 상수**로 들고 있다.  상수는
우리 컴파일러의 배치와 맞든 안 맞든 어셈블된다.  그러므로 지금 말할 수 있는
것은 **문법이 통과한다**는 것뿐이고, 오프셋 일치는 별도로 확인해야 한다
(C 쪽에서 `offsetof` 와 대조하는 컴파일 시 단언이 자연스럽다).

### 10.3 그런데 지금은 살릴 이유가 없다 — 이것이 결정적이다

**쟀기 때문이다.**  C 스팬 고리가 640×480 한 프레임에 **1.18 ms** 이고,
그 프레임을 화면에 올리는 데 **41 ms** 다.  렌더러는 프레임의 몇 퍼센트다.

어셈블리로 렌더러를 두 배 빠르게 해도 프레임은 42.2 → 41.6 ms 다.
**AppKit 이 지배하는 동안 렌더러를 빠르게 하는 것은 아무것도 사지 못한다.**

그러므로 순서는 이렇다:

```
지금            id386=0 으로 간다.  어셈블러가 안 되어서가 아니라
                 렌더러가 병목이 아니어서다
전달을 고친 뒤   Route C 나 D 로 41 ms 가 사라지면, 그때 렌더러의 몫이
                 비로소 보인다.  어셈블리는 그때 꺼내 볼 선택지이고,
                 **문법은 이미 통과한 상태로 기다리고 있다**
```

측정 없이 어셈블리를 살리는 것은, 이 워크스페이스가 M19 에서 한 번 밟은
순서 — 병목을 확인하기 전에 빠른 쪽을 고르는 것 — 을 되풀이하는 일이다.

---

## 11. Q1-1 결과 (2026-08-30)

**엔진이 컴파일되고 링크된다.**

```
QUAKE_CORE_COMPILE ok=67 fail=0
platform: cd_null(상류) · sys_sdl · snd_sdl · vid_sdl
QUAKE_BUILD=pass /usr/local/nxbuild/bin/squake
  2,852,844 바이트 · Mach-O executable (for architecture i486)
```

빌드는 `build/build-openstep-quake.sh`, 코어만 따로 세우는 것은
`build/compile-core.sh` 다.

### 11.1 순서를 나눈 것이 값을 했다

코어 67 개를 **SDL 없이 먼저** 세웠다.  이 포트에는 성격이 다른 두 부류의
문제가 기다리고 있기 때문이다 — gcc 2.7.2.1 이 싫어할 1996년 C, 그리고
SDL2 에 없는 SDL 1.2 API.  코어만 먼저 세우면 거기서 나는 것은 전부
전자다.

**전자는 0 이었다.**  1996년 C 는 이 컴파일러에서 한 마디 불평도 없이
컴파일된다.  놀랄 일은 아니다 — 그 시절 컴파일러를 위해 쓰인 코드다.

`d_polyse.o` 가 `_D_DrawNonSubdiv` · `_D_PolysetDraw` 를 내보내는 것도
확인했다.  `d_polysa.S` 의 C 쌍둥이가 실제로 코드를 냈다는 뜻이고,
`#if !id386` 갈래가 빈 객체를 내지 않았다는 증거다.

### 11.2 막힌 것 넷, 전부 작았다

```
sys/ipc.h · sys/shm.h · sys/mman.h   sys_sdl.c 가 include 만 하고 쓰지
                                      않는다.  그 헤더들이 주는 이름
                                      (shmget, key_t, IPC_*, mmap ...) 을
                                      전수 grep 해서 하나도 없음을 확인한
                                      뒤 지웠다.  리눅스 혈통의 잔재다
SDL_byteorder.h                       SDL2 는 SDL_endian.h 다
_moncontrol 중복 정의                 상류가 빈 스텁을 내보내는데
                                      OPENSTEP System 프레임워크의 gmon.o
                                      에 진짜가 있다.  static 으로 바꿔
                                      충돌만 없앴다 -- 호출은 그대로 무동작
_SNDDMA_Submit 미정의                 §3.2
_mprotect 미정의                      Mach 에는 없다.  vm_protect 가
                                      대응이지만 **쓰지 않았다**: 부르는
                                      곳이 전부 #if id386 안이라 이 빌드에서
                                      도달 불가능하고, 돌려본 적 없는 코드를
                                      완성된 것처럼 두는 것보다 무엇이
                                      없는지 말하는 Sys_Error 가 낫다
```

### 11.3 조용한 실패 하나를 미리 닫았다

`common.c` 의 엔디안 판별은 `#ifdef SDL` 이 아니면
`byte swaptest[2]={1,0}; if (*(short *)swaptest == 1)` 로 간다.  그 자리에
**"egcs 1.1.1 이 -O2 에서 이 식을 잘못 컴파일한다"** 는 주석이 붙어 있고,
우리 컴파일러도 같은 시대이며 이 워크스페이스는 이미 gcc 2.7.2.1 의
오컴파일을 한 번 만났다.

그래서 같은 식을 같은 플래그(`-m486 -O`)로 돌려 봤다:

```
this machine is    : little-endian
Quake's probe says : little-endian
QUAKE_ENDIAN_PROBE=pass
```

여기서 틀렸다면 엔진의 모든 파일·네트워크 읽기가 반대로 바이트를 바꾸고,
아무것도 그렇다고 말해주지 않았을 것이다.

### 11.4 `vid_sdl.c` 에서 구조가 바뀐 한 곳

SDL2 에는 **팔레트 창 표면이 없다.**  SDL 1.2 는 8비트 디스플레이 표면을
하드웨어 팔레트째 내주었고 엔진이 거기에 직접 그렸다.  SDL2 에서는 8비트
표면을 우리가 들고, 프레임마다 창 표면으로 blit 한다.

```
SDL_SetVideoMode(w,h,8,HWPALETTE)  ->  SDL_CreateWindow + GetWindowSurface
                                        + CreateRGBSurfaceWithFormat(INDEX8)
SDL_SetColors                      ->  SDL_SetPaletteColors
SDL_UpdateRects                    ->  더티 사각형만 blit + UpdateWindowSurface
SDL_WM_SetCaption                  ->  SDL_SetWindowTitle
SDL_WarpMouse                      ->  SDL_WarpMouseInWindow
SDL_INIT_CDROM                     ->  없앰 (SDL2 에 CD API 가 없다)
SDLK_KP0..KP9                      ->  SDLK_KP_0..KP_9
SDLK_BREAK                         ->  없앰 (SDL2 에 없다)
기본 해상도 640x400                ->  640x480
```

`vid.rowbytes` 는 **표면의 pitch** 로 잡았다.  폭과 같다고 가정하면 SDL 의
행 정렬 때문에 그림이 줄마다 어긋난다.

### 11.5 데이터를 기계에 올렸다 (2026-08-30)

```
/usr/local/quake/id1/pak0.pak      43,948,916   sum 62070 42919
/usr/local/quake/id1/pak1.pak       5,856,792   sum 58072  5720
/usr/local/quake/id1/docs/          COPYING · CREDITS ·
                                    README-IMPORTANT-LICENCE-INFO
```

**크기가 아니라 내용으로 확인했다**: 양쪽에서 `sum` 을 떠서 맞췄다.  48 MB
NFS 복사를 크기만 보고 넘기면, 잘린 pak 은 한참 뒤에 엉뚱한 증상으로
나타난다.

`music/` 28 MB 는 **올리지 않았다.**  Ogg 트랙인데 이 엔진은 CD 오디오만
알고 이 빌드는 `cd_null.c` 를 링크한다 — 재생할 방법이 아예 없다.  공간을
아낀 것이 아니라 쓸 수 없는 것을 뺀 것이다.

문서 셋은 몇 KB 이지만 함께 두었다.  남의 자산을 기계에 놓을 때 그 라이선스와
크레딧이 같이 있어야 한다.

`docs/` 와 브랜딩 이미지 나머지(3.6 MB)는 사람이 읽는 것이라 뺐다.

### 11.6 다음

`Q1-2` 로 넘어갔다 -- §12.

---

## 12. Q1-2 결과 (2026-08-30)

**엔진이 돈다.**  창이 뜨고, 메뉴가 열리고, LibreQuake 가 자기 설정을
실행한다.

```
Added packfile /usr/local/quake/id1/pak0.pak (353 files)
Added packfile /usr/local/quake/id1/pak1.pak (60 files)
Playing registered version.
Console initialized.
UDP_Init: "<this host>" does not resolve; using 127.0.0.1.
UDP Initialized
16.0 megabyte heap
execing quake.rc / default.cfg
LibreQuake Version V0.09-beta
```

353 과 60 은 `Q1-0` 의 검사기가 센 것과 같은 수다.  `16.0 megabyte heap` 은
`sys_sdl.c` 의 기본값 변경이 실제로 먹은 것이다.  운영자가 화면에서 확인:
**창이 뜨고 ESC 로 메뉴가 열린다.**

### 12.1 하나 걸렸고, 상류의 결함이었다

첫 실행은 `Console initialized.` 직후 **버스 오류**로 죽었다.

`-noudp` 를 붙이면 사라졌다 -- 그것이 이분법이었다.  `-noudp` 는
`UDP_Init` 을 곧장 반환시키는 것 말고는 아무것도 바꾸지 않으므로, 범인은
그 함수다.  그리고 `net_udp.c:68` 이 이것이다:

```c
local = gethostbyname(buff);
myAddr = *(int *)local->h_addr_list[0];      /* local 을 안 본다 */
```

**추측하지 않고 확인했다.**  같은 두 호출만 하는 프로그램을 짜서 돌렸다:

```
gethostname()   : "<this host>"
gethostbyname() : NULL
```

이 기계는 자기 이름을 못 푼다 (hosts 에도 NetInfo 에도 없다).  그래서 그
줄은 NULL 역참조이고, i386 Mach 에서 그것은 창이 만들어지기도 전의 버스
오류다.

**우리가 만든 결함이 아니라 상류의 것이다.**  이름이 풀리는 리눅스에서는
드러나지 않는다.

고친 방식: `local` 과 `h_addr_list[0]` 을 둘 다 확인하고, 없으면 **말하고**
루프백으로 간다.  `myAddr` 은 두 곳에서만 쓰인다 -- 사용자가 친 부분 주소를
완성할 때, 그리고 `INADDR_ANY` 에 묶인 소켓의 주소를 채울 때 -- 이름이 없는
기계에서는 어느 쪽도 옳을 수 없으므로, 조용히 틀린 값을 쓰는 대신 무엇이
문제인지 말한다.

`net_udp.c` 는 이 참에 **플랫폼 파일로 옮겼다.**  코어 목록에 있을 것이
아니었다.

### 12.2 지금 가속은 아니다

돌고 있는 것은 소프트웨어 래스터라이저다.  8비트 픽셀을 시스템 메모리에
그리고, 창 표면으로 blit 하고, AppKit 이 화소당 126 ns 로 올린다.  이
프로젝트의 Matrox 가속은 **GL 경로**에 있고 Quake 의 이 렌더러는 GL 을 쓰지
않는다.  Route A 다.

(바이너리가 `libGL.a` 를 링크하는 것은 `libSDL2.a` 가 OSMesa 심볼을
요구하기 때문일 뿐, GL 문맥은 만들어지지 않는다.)

### 12.3 다음

```
Q1-3/4  한 프레임과 움직임은 이미 화면에서 확인됐다.  남은 것은 숫자다 --
        640x480 에서 프레임당 얼마인지, 그리고 그것이 24 fps 천장에
        얼마나 붙는지
Q1-5    소리.  -nomouse 로만 돌렸고 소리는 아직 듣지 않았다
Q1-6    23 맵을 하나씩 열고 r_reportsurfout / r_reportedgeout 를 읽는다
```

---

## 13. Q1-4 결과 (2026-08-30)

Quake 자체 벤치마크, 640x480, LibreQuake 의 demo1:

```
4527 frames  282.5 seconds  16.0 fps
VID: asked 640x480, got 640x480, on a 1600x1200 desktop
```

```
프레임         62.50 ms
  전달(실측)   41.18 ms   66%
  Quake 자체   21.32 ms   34%   <- 차감이다
전달만의 천장             25.8 fps
```

**Route A 는 여기까지다.**  Quake 를 무한히 빠르게 해도 25.8 fps 를 못 넘고,
지금 16.0 fps 로 이미 그 천장의 62% 에 붙어 있다.

### 13.1 차감을 독립 측정으로 검증하지 않았고, 그게 맞다

800x600 을 재서 예측 구간(10.7~12.2 fps) 안에 드는지 보려 했다.  중단했다.

**어떤 결정도 바꾸지 않기 때문이다.**  Quake 자체가 21 ms 든 25 ms 든 전달이
41 ms 로 과반이고, A 냐 C 냐는 이미 천장이 정한다.  정밀도를 위해 9 분짜리
벤치마크를 돌리는 것은 답이 정해진 질문에 시간을 쓰는 일이다.

그러므로 §13 의 34% 는 **차감이며 독립 확인을 받지 않았다**고 적어 둔다.
이 숫자에 기대는 결론을 나중에 세운다면, 그때 재야 한다.

### 13.2 창 크기와 화면 크기는 다른 질문이었다

운영자가 "창이 너무 작다, 640x480 도 안 되는 것 같다" 고 했다.  내가 따로
띄운 창으로 잰 것은 "640x480 을 요청하면 640x480 이 나온다" 까지만 증명해서
불충분했다 -- Quake 가 그 값을 넣었는지는 별개다.  그래서 **엔진이 직접
로그에 적게** 했다:

```
VID: asked 640x480, got 640x480, on a 1600x1200 desktop
```

창은 맞았다.  바탕화면이 1600x1200 이라 넓이로 16% 를 차지할 뿐이었다.

**전체화면은 시험하지 않는다** (운영자 결정).  이 경로에서 그럴 값이 없기도
하다: 전달이 화소당 126 ns 이므로 1600x1200 은 프레임당 242 ms, 천장 4.1 fps
다.  SDL2 의 `-fullscreen` 은 `FULLSCREEN_DESKTOP` 이라 바로 그 크기가 된다.

화면을 채우면서 640x480 의 값을 내려면 창이 아니라 **디스플레이 모드**를
바꿔야 하고, 이 프로젝트의 Matrox 드라이버가 그 모드를 지원한다.

---

## 14. Q1-5 결과 (2026-08-30) — 소리가 난다

운영자가 들었다: **환경음, 그리고 횃불이 타는 소리.**  뒤엣것이 특히
말해주는 바가 있다 — 횃불은 위치를 가진 점음원이므로, 감쇠와 방향이
계산되고 있다는 뜻이다.  단순히 "출력이 열렸다" 가 아니라 믹서가 돌고 있다.

```
SDL_AUDIODRIVER=openstep     명시해서 띄웠다.  dummy 로 조용히 빠지지 않았다
Sound Initialization
Sound sampling rate: 11025   Quake 가 요청한 값이다
```

11025 는 장치의 값이 아니다.  이 기계의 장치는 22050 모노 `AUDIO_S16MSB` 로
협상하고, `SNDDMA_Init` 이 그것을 받아들이지 못해 `obtained == NULL` 로 다시
열며, SDL2 가 변환 스트림을 붙여 11025 스테레오를 그대로 준다 (§3.2).
**설계대로 동작한 것이고, 로그의 11025 가 그 증거다.**

동작하는 사슬 전체:

```
Quake 믹서 -> snd_sdl.c -> SDL2 오디오 -> OPENSTEP SoundKit 백엔드 -> 하드웨어
```

### 14.1 내가 없는 문제를 잡으려 했다

처음 운영자의 보고는 **"전혀 안 들린다"** 였다.  나는 곧장 진단을 설계했다 --
`soundinfo` 를 두 번 찍어 `samplepos` 가 움직이는지 보는, 귀가 필요 없는
시험이었다.  좋은 시험이고, **필요가 없었다.**

돌고 있던 인스턴스를 죽여야 그 진단을 돌릴 수 있었는데, 그 명령이 거절되었고
잠시 뒤 소리가 들리기 시작했다.  이유는 Quake 쪽에 있다:

```
환경음      S_UpdateAmbientSounds 가 볼륨을 시간에 걸쳐 올린다.  즉시 나지 않는다
횃불        점음원이다.  가까이 가야 들린다
```

**시작 지점에 가만히 서 있는 한 순간은 침묵의 증거가 아니었다.**  화면에서
움직이고 있다는 운영자의 관찰이 내 로그 grep 을 이겼던 것과 같은 종류의
일이다 (§12 의 맵 로딩 판단도 그랬다).

교훈은 도구가 아니라 순서다: **증상이 재현되는지부터 확인하고 진단을 짠다.**
여기서는 "조금 더 기다리고 움직여 본다" 가 그 확인이었고, 비용이 0 이었다.

### 14.2 지금까지

```
Q1-0  데이터 사전검사        23 맵 · 81 모델 · 3 스프라이트, 전부 통과
Q1-1  빌드와 링크            코어 67 + 플랫폼 4
Q1-2  실행                   pak 353+60, 창, 메뉴, 이동
Q1-4  측정                   640x480 에서 16.0 fps (천장 25.8)
Q1-5  소리                   환경음과 위치 음원
Q1-6  전 맵 순회             도구는 만들었고 돌리지 않았다 -- 값이 낮다고
                             판단 (Route C 로 가면 그 계수기는 의미를 잃는다)
```

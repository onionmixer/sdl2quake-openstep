# Q3 — GLQuake 파이프라인 원점 재평가

## 0. 왜 다시 보는가

지금까지는 "왜 revoke 가 나는가" 를 드라이버 거절 카운터로 좁혀 왔다. 그 결과
좌표 정책 불일치 하나는 찾아 재기준으로 완화했지만, **GLQuake 가 한 프레임에
무엇을 어떤 상태로 그리는지를 코드로 읽은 적이 없었다.** 이 문서는 그것을
읽고, 각 그리기 경로를 드라이버의 게이트(`osmgaMesaTexStateOK`, 블렌드·깊이·
알파테스트 매핑)에 대조한 결과다. 실기 실행 없이 소스만으로 작성했다.

참조: `upstream/sdlquake/gl_rmain.c`, `gl_rsurf.c`, `gl_warp.c`, `gl_draw.c`,
`r_part.c`, `port/openstep/gl_vidsdl.c`; 드라이버 `mesa/OpenStepMGAMesaHook.c`.

## 1. 한 프레임의 그리기 경로와 판정

포트의 전역 상태(`gl_vidsdl.c` GL_Init): `glCullFace(GL_FRONT)`, `GL_ALPHA_TEST`
켬 + `GL_GREATER 0.666`, `GL_FLAT`, 블렌드 `SRC_ALPHA/ONE_MINUS_SRC_ALPHA`,
texenv `GL_REPLACE`. 포트는 `gl_filter_min` 을 `GL_LINEAR` 로 강제한다(드라이버가
밉맵 필터 4종을 거절하므로). `gl_mtexable = false` (Mesa 3.4.2 에 SGIS 멀티텍스처
없음) → 항상 **단일 텍스처 2패스**. `gl_texsort = 1` (기본).

| # | 경로 | 텍스처 | 필터/랩 | 좌표 범위 | env | 블렌드 | 깊이 | 게이트 판정 |
|---|---|---|---|---|---|---|---|---|
| 1 | 월드 텍스처 패스 (`DrawTextureChains`→`DrawGLPoly`) | RGB(3), POT 로 리스케일, ≤256 | LINEAR / REPEAT | `s/텍스처폭` = **반복 단위, 68% 가 >8, 최대 223** | REPLACE | 없음 | LEQUAL, 쓰기 | **HW 가능**. 좌표 정책만 걸림 → T2 재기준으로 완화(폭>8 인 0.07% 는 SW) |
| 2 | **라이트맵 패스** (`R_BlendLightmaps`) | 128×128 **GL_LUMINANCE** (기본 `-lm_1`) | LINEAR / REPEAT(기본) | [0,1] | REPLACE | `ZERO / ONE_MINUS_SRC_COLOR` | LEQUAL, **쓰기 끔** | **항상 SW** — 게이트가 RGB/RGBA 외 포맷을 거절 (`Hook.c:2947`) |
| 3 | 하늘 2패스 (`R_DrawSkyChain`) | 128×128 RGB + RGBA | LINEAR | `(speedscale+dir)/128` ≈ [−3, 4] | REPLACE | 2패스 `SRC_ALPHA/OM` | LEQUAL | HW 가능 |
| 4 | 물 (`EmitWaterPolys`, `r_wateralpha=1` 이면 1번 체인 안) | RGB | LINEAR / REPEAT | `(os+turbsin)/64` ≈ 월드와 같은 반복 단위 | REPLACE | 없음 | LEQUAL | HW 가능, 좌표 정책 → 재기준 대상 |
| 5 | 알리아스 모델 (`R_DrawAliasModel`) | 스킨 RGB | LINEAR | [0,1] | **MODULATE**, `GL_SMOOTH` 정점색 | 없음 | LEQUAL | HW 가능 (ShadeModel 은 분기) |
| 6 | 모델 그림자 (`GL_DrawAliasShadow`) | 없음 | — | — | — | `SRC_ALPHA/OM` | LEQUAL | HW 가능 (무텍스처) |
| 7 | 스프라이트 | RGBA | LINEAR | [0,1] | REPLACE | 없음, **알파테스트 GREATER** | LEQUAL | HW 가능 |
| 8 | 파티클 (`R_DrawParticles`) | 8×8 RGBA | LINEAR | [0,1] | MODULATE | `SRC_ALPHA/OM` | LEQUAL | HW 가능 |
| 9 | 폴리블렌드 (`R_PolyBlend`) | 없음 | — | — | — | `SRC_ALPHA/OM`, 깊이 끔 | — | HW 가능 |
| 10 | 2D HUD/콘솔 (`GL_Set2D`, `Draw_*`) | RGB/RGBA | LINEAR | [0,1] | REPLACE | 없음, 알파테스트 켬 | 끔 | HW 가능 |
| — | `gl_ztrick=1` | — | — | — | — | — | 홀짝 프레임 `GEQUAL` + `glDepthRange(1,0.5)` | 깊이함수 GEQUAL 은 매핑됨; DepthRange 는 Mesa 뷰포트 변환이 처리 (훅은 `Win[2]` 사용) — **확인 필요** |

## 2. 놓친 것

### 2.1 라이트맵 패스는 좌표와 무관하게 **매 프레임 월드 전체가 소프트웨어**다

이것이 가장 큰 누락이다. `gl_lightmap_format` 기본값은 `GL_LUMINANCE`
(`gl_rsurf.c:1614`). 드라이버는 단일 채널 포맷을 거절한다(주석: "ALPHA,
LUMINANCE, INTENSITY 는 다른 치환이며 제공하지 않는다"). 따라서 **월드의 모든
표면이 2번째 패스에서 소프트웨어로 그려진다** — 좌표 정책을 완벽히 고쳐도
그대로다.

그리고 그 소프트웨어 패스는 싸지 않다. 드라이버는 OSMesa 의 색 버퍼를 **VRAM
대체 표면**으로 바꿔 놓으므로(`Hook.c:1951` "It drew into the substituted
surface"), 소프트웨어 래스터라이저는 VRAM 에 직접 그린다. 라이트맵 패스는
**블렌드**(`ZERO/ONE_MINUS_SRC_COLOR`)라서 픽셀마다 **PCI 너머 VRAM 을 읽고
쓴다**. 화면 전체를 매 프레임.

게다가 `osmgaMesaSoftly` 는 매 삼각형마다 `osmgaMesaFlushPending()` 을
부른다 — 하드웨어 배치가 쌓여 있으면 강제 flush + 완료 대기다. 텍스처 패스(HW)와
라이트맵 패스(SW)가 표면 체인 단위로 번갈아 오므로 배치가 계속 끊긴다.

**코드 수정 없는 실험이 있다**: `-lm_4` 로 실행하면 라이트맵이 `GL_RGBA`
(`dest[3] = 255−t`, RGB = 0)가 되고 블렌드는 현재 값(`SRC_ALPHA/OM_SRC_ALPHA`)을
쓴다 → 결과 = `dst · t/255`, 동일한 어둡힘. 이 상태는 게이트의 모든 조건을
통과한다(RGBA, LINEAR, REPEAT+POT, REPLACE, 블렌드 인자 허용, 깊이쓰기 끔 허용).

### 2.2 GLQuake 에서 `mgastats` 를 한 번도 읽지 않았다

`hard / soft / declined / read back / flush` 카운터가 있는데 revoke 유무만
봤다. 경로별 HW/SW 비율을 모른 채 좌표 정책만 쫓았다.

### 2.3 동적 라이트맵 재업로드

`R_BlendLightmaps` 는 dlight 가 닿은 라이트맵 블록을 매 프레임
`glTexSubImage2D` 로 다시 올린다. 드라이버의 텍스처 상주 경로에서 이것이 재업로드
(746 ns/픽셀급)를 유발하는지, 블록 수(384)가 충분한지 미측정.

### 2.4 남은 거절의 발원지

재기준 후에도 `row-ends 158`, `anchor-uv 49` 가 남는다. 후보: 폭>8 인 면(0.07%),
물(turbsin 이 최대 ±8 텍셀을 더함), 브러시 모델(`R_DrawBrushModel`, 자체 변환).
어느 것인지 모른다.

### 2.5 revoke 백스톱 자체

연속 8회 거절 → 영구 반납. 정상적으로 SW 로 재생된 `E_TEXCOORD` 를 장치 고장으로
승격한다. codex 도 지적했고, 별건으로 미뤄 둔 상태.

## 3. 작업 계획 (초안 — 교차검토 대상)

### P0. 측정 (재부팅 없음, 유저랜드만)
- GLQuake 에 `mgastats` 자동 출력(예: 10초 간격, 또는 종료 시)을 넣고
  `hard / soft / declined / read back / flush` 를 기록한다.
- 같은 실행을 `-lm_4` 로 반복한다. 예측: `soft` 가 급감.
- WARP=0 고정.

### P1. 라이트맵 포맷
- `-lm_4` 가 예측대로면 포트의 기본값을 RGBA 로 바꾼다(`gl_vidsdl.c` 또는
  `COM_CheckParm` 대체). 화면 동일성은 `glquake_sw` 와 비교.
- 대안: 드라이버가 LUMINANCE 를 업로드 시 RGB 로 확장. 더 일반적이지만 드라이버
  변경 + 재부팅. P1 이 먹히면 불필요.

### P2. 남은 거절의 발원지
- `OSMGA_MESA_TESTHOOKS` 빌드로 거절 삼각형의 텍스처 크기·좌표·q 를 덤프해
  경로를 특정. 물이면 turbsin 여유를 재기준 K 선택에 반영(폭 계산에 ±8 텍셀
  포함).

### P3. 배치 단절
- P1 이후에도 `flush` 가 높으면, 소프트웨어 삼각형이 강제하는 flush 의 수를
  세고 경로를 특정한다.

### P4. 백스톱 정책 (별건)
- 정상 SW 재생된 per-triangle 거절은 revoke run 에 가산하지 않는다. P1~P3 결과
  측정 뒤에만.

### P5. 그 다음에야 fps
- `timedemo demo1` 로 프레임 정규화 비교. 그 전의 fps 는 증상이지 진단이 아니다.

# Q5 — sdlquake 의 GL 요구 전수 감사 (2026-08-31, 2차 — codex 정정 반영)

목적: "몇몇 화면 깨짐"의 원인 후보를 **주관 배제로** 좁힌다.  glquake 소스가
호출하는 GL 엔트리포인트 전수(기계 추출, 64개)와 상태 인자 전수를, 우리
스택(Mesa 3.4.2 코어 + OSMGA 훅 + WARP/사다리꼴 티어)의 처리와 대조했다.
모든 판정은 파일:줄 확인 기반.  codex 교차검토 §7.

## 1. 엔트리포인트 전수 (64) — 분류

**행렬/뷰포트/상태 조회 (Mesa 코어가 소화, 티어 무관)**: glMatrixMode,
LoadIdentity/LoadMatrixf/Push/PopMatrix, Frustum, Ortho, Rotate/Scale/
Translatef, Viewport, GetFloatv/GetIntegerv/GetString/GetTexParameteriv/
IsTexture, Hint(PERSPECTIVE_CORRECTION — 우리 코어엔 무의미), ClearColor.

**정점 스트림 (Mesa T&L → 훅 삼각형 콜백)**: Begin/End, Vertex2f/3f/3fv,
TexCoord2f/2fv, Color3f/4f/4fv/3ubv.  프리미티브 실사용: QUADS(11곳),
POLYGON(10), TRIANGLE_FAN(6), TRIANGLES(2), STRIP(2), LINES(1 — gl_test.c,
`#ifdef GLTEST` 밖 Test_Init/Spawn 만 링크됨, Test_Draw 는 미컴파일;
LINES 는 어차피 Mesa 소프트웨어 라인 경로).  전부 Mesa 가 삼각형으로 풀어
훅에 옴 — 실측 드로우 전량 WARP 와 일치.

**텍스처**: BindTexture, TexImage2D(GL_RGBA 계열만 — COLOR_INDEX8 경로는
SHARED_TEXTURE_PALETTE 확장 부재로 미실행), TexSubImage2D(라이트맵 갱신),
TexParameterf(MIN/MAG 필터, WRAP S/T REPEAT), TexEnvf.  gluBuild2DMipmaps/
gluScaleImage 는 **`#if 0` 안** (gl_draw.c:1029-1039) — GLU 링크 불필요 확인
(libGL_mga 84 멤버 중 glu 없음).

**픽셀/버퍼**: Clear, ReadPixels(스크린샷·envmap), ReadBuffer,
DrawBuffer(GL_FRONT — 로딩 디스크 gl_draw.c:811, envmap/timerefresh) —
OSMesa 단일 표면에서 FRONT/BACK 구분 없음: 로딩 디스크는 프레임에 묻힘
(고장 아님, 미표시 가능).

**glX\***: gl_vidlinuxglx.c 전용 — 미링크.
**\*PointerEXT/ColorTableEXT**: gl_vidnt.c 전용 — 미링크.

## 2. 상태 인자 전수 × 우리 게이트

| 상태 | quake 사용 | 우리 처리 | 판정 |
|---|---|---|---|
| glBlendFunc | ONE/ONE · SRC_A/OM_SRC_A · ZERO/OM_SRC_C | WARP 허용 목록에 셋 다 있음 (Hook.c:3467-3480), 사다리꼴 매핑도 (2716-2732) | ✅ |
| glTexEnv | REPLACE·MODULATE·BLEND | WARP 는 REPLACE/MODULATE 만 (3177). **GL_BLEND env 는 multitexture(qglMTexCoord2fSGIS) 경로 전용** — SGIS 확장 부재로 미실행 | ✅(미사용) / ⚠️게이트 지나면 사다리꼴에 명시 거절 없음(잠재) |
| glAlphaFunc | GREATER 0.666 | GT 매핑 있음 (2757-2763) | ✅ |
| glDepthFunc | LEQUAL·GEQUAL | 둘 다 매핑 (2681, 3522) — GEQUAL 은 **거울 렌더 시에만** | ✅(경로 희귀) |
| glDepthMask | 0/1 (라이트맵·물) | atype I/ZI 로 매핑 (1544, 2916) | ✅ |
| glDepthRange | 총 모델 0..0.3 hack | Mesa 뷰포트 행렬(matrix.c:1455-6,1516)이 win z 에 반영 → 티어는 win z 소비 | ✅(이론) — 실측 미확인 |
| glShadeModel | FLAT/SMOOTH | FLAT provoking 색 처리 (2851,2913) | ✅ |
| glCullFace | FRONT/BACK | Mesa T&L 이 컬링 후 콜백 | ✅ |
| glFog\* | **주석 처리됨** (gl_rmain.c:1131-1138) | 훅에 fog 게이트 **없음** | ✅(미사용) / ⚠️잠재 갭: fog 켠 앱은 오렌더 |
| SHARED_TEXTURE_PALETTE | glEnable 요청 | 확장 미제공 → 미사용 경로 | ✅ |
| VERTEX_ARRAY_EXT | gl_vidnt 전용 | 미링크 | ✅ |

## 3. 핵심 메커니즘 검증 — RGBA 라이트맵

isPermedia=true → gl_lightmap_format=GL_RGBA.  GL_BuildLightMap 은 **알파
채널에만 255-light** 를 쓴다(gl_rsurf.c:188-203, RGB 는 0).  R_BlendLightmaps
의 RGBA 는 분기 없음 → blend 는 **직전 전역 상태** = GL_Init 의
(SRC_ALPHA, OM_SRC_ALPHA)(gl_vidsdl.c:485), env 는 REPLACE(486).  결과
= 벽색×light/255 + 0×(1-…) — **의도된 변조**.  단 이 경로는 우리 WARP 가
**텍스처 알파를 blend 인자로 실제 사용**해야 성립한다 — ALPHASEL_TEX
(2810)·TEXF_TEXALPHA(2889, HW3D:1880) 플러밍 존재, 레벨 화면 정상 실측과
부합.  ✅ (단, "몇몇 화면"이 라이트맵 관련이면 재심)

## 4. 깨진 화면 후보 (우선순위, 사용자 증상 대조 대기)

| # | 후보 | 근거 |
|---|---|---|
| 1 | **물/하늘 워프면** (gl_warp) — REPEAT+속도스크롤 텍스코드, 큰 폴리곤 세분 | 텍스코드 rebase(T3a) 는 tq==1 전제; warp 면의 큰 s/t 이동 |
| 2 | **반투명 물 (r_wateralpha<1)** | MODULATE+색알파 경로 — WARP 의 iterated-alpha × texture 조합 |
| 3 | **파티클** (GL_TRIANGLES, 8×8 dot 텍스처, MODULATE) | 초소형 삼각형 다수 — 라스터 경계 판정 |
| 4 | **화면 플래시/워터 틴트** (V_PolyBlend 무텍스처 블렌드 쿼드) | 무텍스처+blend 조합 |
| 5 | **총 모델** (depthrange hack) | win-z 압축 구간의 WARP z 정밀도 |
| 6 | **거울** (GEQUAL+CULL FRONT) | 희귀 경로, 실측 0 회 |
| 7 | 로딩 디스크/FRONT 드로우 | 단일 표면에서 의미 상실 (미표시는 정상) |

## 5. 확인된 잠재 갭 (quake 미사용이지만 기록)

- fog 게이트 부재 — Fog.Enabled 검사 없음.
- 사다리꼴 티어 env 명시 게이트 부재 (WARP 거절 후 낙하 시 DECAL/BLEND 를
  MODULATE 처럼 그릴 수 있음).
- GL_LINES/GL_POINTS 는 훅 미가속 (Mesa 소프트웨어 스팬) — 성능만 문제.

## 6. 문서 최신화

- Q3_PIPELINE_REASSESSMENT: §12 로 현황 갱신 (T5/epoch/Q4 완료 반영).
- Q4_GRAB_SOUND_PLAN: §9 까지 완결 (커밋 1957f64).
- 본 문서(Q5)는 §7 의 정정을 반영한 2차가 정본이다.

## 7. codex 교차검토 판정 (2026-08-31) — 채택 정정

| codex 주장 | 내 검증 | 판정 |
|---|---|---|
| **GEQUAL 은 거울 전용이 아니라 기본 z-trick 의 격프레임 상시 경로** — 짝수 프레임 depth 1→0.5 역방향+GEQUAL, depth clear 생략 (gl_rmain.c:995-1015); 거울은 오히려 LEQUAL | R_Clear 직접 확인 — 맞다. **내 §2 표의 "경로 희귀" 는 오판** | ✅채택 — 의심 상위 승격 |
| 64개는 raw lexical 표면이고 링크·전처리 후 실행 표면은 **48개** (glX/EXT/glu/#if0/주석 제외); gl_test.c 는 전체가 `#ifdef GLTEST` 라 미링크, GL_LINES 도 그 안 `#if 0` | 근거 줄 확인 | ✅채택 — §1 재정의 |
| fog·GL_BLEND env 는 **오렌더가 아니라 공통 게이트의 소프트웨어 폴백** — osmgaMesaTexStateOK 는 두 하드웨어 티어 공통(3564), RasterMask 화이트리스트가 fog 거절(3383) | 확인 | ✅채택 — §5 두 줄 삭제·정정 |
| AlphaRef 는 Mesa 가 GLubyte(170)로 이미 양자화, 훅 전달 정확 | types.h:357 확인 | ✅확인 |
| TexSubImage 무효화→lazy 재업로드 체인 정본 검증 (Texture.c:310→244) | 내 검증과 일치 | ✅확인 |
| conback 은 항상 256² 아님(lump 크기 유지→POT 리샘플); 비-POT gl_max_size 는 NPOT+REPEAT 를 만들 수 있고 훅이 거절(3289) | 확인 | ✅채택 — 크기 표 추가 대상 |
| 포트는 mipmap 필터를 매 프레임 GL_LINEAR 로 강제 복구(gl_vidsdl.c:505-594) — "런타임 mipmap = 영구 소프트웨어" 서술 정정 | 확인 | ✅채택 |
| RGBA 라이트맵 RGB=0 은 "함수가 기록"이 아니라 **BSS 0 불변식**(gl_rsurf.c:53) | 확인 | ✅채택 — §3 표현 정정 |
| clear 상태 축 누락: clear color (1,0,0,0), gl_clear=0 기본, depth-only clear 는 훅이 Mesa 에 남김(4084/4290) | 확인 | ✅채택 |
| 의심 1순위는 물/하늘이 아니라 **HUD·메뉴·콘솔의 alpha/filter 조합 + z-trick** | 증상(시작화면·하단 UI)과 부합 | ✅채택 — §4 재순위 |

## 8. 판별 시험표 (실기, 콘솔 명령 — codex 설계 채택)

```
[T1] gl_ztrick 0        잔상·격프레임 깨짐이 사라지면 depth 경로(z-trick 의
     (+ gl_clear 1)     GEQUAL/역 depth range) 가 주범 -- 최우선 시험
[T2] toggleconsole      반개(=Draw_AlphaPic, blend) vs 전개(=Draw_Pic,
                        alpha test) 콘솔 배경 비교 -- 어느 경로가 검은지
[T3] gl_texturemode GL_NEAREST (재시작 후 autoexec) -- 필터 축 분리
[T4] gl_flashblend 1/0  무텍스처 ONE/ONE 팬 (동적 라이트 블롭)
[T5] gl_polyblend 0/1   전화면 틴트 (물속/데미지)
[T6] r_wateralpha 0.5   반투명 물
[T7] r_dynamic 1 + 어두운 벽 사격 -- TexSubImage 실기 확인
[T8] WARP=0 (사다리꼴) 및 glquake_sw (순수 Mesa) 대조 -- 티어 격리
```

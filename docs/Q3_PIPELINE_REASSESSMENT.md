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
| — | `gl_ztrick=1` | — | — | — | — | — | 홀짝 프레임 `GEQUAL` + `glDepthRange(1,0.5)` | 깊이함수 GEQUAL 은 매핑됨; DepthRange 는 Mesa 뷰포트 변환이 처리 — 확인함: 훅은 `VB->Win.data[][2]` 를 그대로 쓴다 (`Hook.c:2234`) |

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
RGBA 모드에서는 `R_BlendLightmaps` 가 블렌드 함수를 건드리지 않으므로 현재 값이
쓰이는데, 코드베이스 어디에서도 `ZERO/ONE_MINUS_SRC_COLOR` 는 LUMINANCE 갈래
밖에서 설정되지 않는다 → 항상 `SRC_ALPHA/OM_SRC_ALPHA` 다.

포트 쪽 스위치도 있다: 업스트림 `gl_rsurf.c:1616` 은 `isPermedia` 가 참이면
기본을 RGBA 로 둔다. 포트는 `isPermedia = false` (`gl_vidsdl.c:91`). 이 한 줄을
바꾸면 업스트림을 건드리지 않고 기본이 RGBA 가 된다(`-lm_1` 로 되돌릴 수 있다).

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

## 4. 독립 교차검토 판정표 (gpt-5.6-sol, 영문, 소스만으로, 문서·이력 열람 금지)

codex 는 내 Q3 표를 보지 않고 같은 추출을 했다. 두 표를 대조하고, 갈리는 지점과
codex 가 새로 찾은 것을 전부 소스에서 확인했다.

### 4.1 일치한 것 (독립 도출이 서로를 확인)

- 라이트맵 `GL_LUMINANCE` → 게이트가 항상 소프트웨어 (`Hook.c:2937`). `-lm_4` /
  `isPermedia` 로 RGBA 가 되면 게이트 전 조건 통과.
- 소프트웨어 삼각형은 VRAM 대체 표면에 직접 래스터되고, 그 전에 하드웨어 배치를
  동기 flush 한다 → HW/SW 교대가 배치를 파괴.
- 월드·하늘·물·모델·스프라이트·파티클·HUD 는 게이트 통과.
- 좌표 정책 [−1, +8] 반복, 빌더 재기준, 커널의 행끝점 검사.

### 4.2 codex 가 새로 찾았고 내가 놓친 것 (전부 검증함)

| 발견 | 검증 | 판정 |
|---|---|---|
| **revoke 후에도 소프트웨어가 VRAM 에 그린다.** revoke 는 명령창만 닫고 색 표면은 바인드된 채 둔다 (`Hook.c:1584`) | 주석 원문 확인: "does NOT touch the colour surface" | ✅ **중요.** revoke 된 GLQuake 가 스톡 소프트웨어(`glquake_sw`)보다 훨씬 느린 이유. "느려서 깨진 것처럼 보였다"는 관찰과 일치 |
| 동적 광원 플래시 (`R_RenderDlights`): 무텍스처 팬, `ONE/ONE` 블렌드, SMOOTH, 깊이쓰기 끔 — 내 표에 없던 경로 | `gl_rlight.c` R_RenderDlights, `gl_rmain.c:967` 호출 확인; 훅 인자표에 ONE/ONE 있음 | ✅ 채택. HW 가능, 표에 추가 |
| `Draw_TileClear` 2D 는 `x/64` 로 S 최대 10 → 8 반복 초과 | `gl_draw.c` 확인 | ✅ 채택. 아핀(tq=1)이라 재기준이 처리 |
| 깊이만 지우기는 소프트웨어 (`Hook.c:3684`) | 주석 확인 "depth alone — NOT taken" | ⚖️ 사실이나 기본 `gl_ztrick 1` 에서는 clear 가 없어 영향 없음 |
| 포트의 필터 "수리"는 전역 변수만 바꾸고 **기존 텍스처 객체는 안 고친다**; 업스트림 `gl_texturemode` 명령은 객체를 순회한다 (`gl_draw.c:351`) | 두 코드 모두 확인 | ⚖️ **사실이나 기본 경로에서는 비활성.** 자동 실행되는 cfg(config/default/autoexec)에는 `gl_texturemode` 가 없다(타깃 grep — `q23.cfg`, `q23min.cfg` 에만 있고 이들은 자동 실행되지 않음). 다만 사용자가 그 명령을 치면 월드 전체가 영구히 SW 가 되므로 고칠 가치는 있다 |
| 텍스처 상주에 **살아 있는 텍스처의 LRU 퇴거가 없다**; 아레나가 차면 그 텍스처는 영구 SW | `osmgaTexDrop` 은 이미지 재정의/삭제 시에만 호출됨을 확인 | ✅ 채택. 384 블록·바이트 한도 초과 시 점진적 저하 |
| `glTexSubImage2D` 가 이미지 전체를 무효화해 128×128 전부 재복사 | `Texture.c:311` 확인 | ✅ 채택. 동적 라이트맵 비용 |
| 사다리꼴 배치 키에 `tmr[0..5]` 전부 포함 → 원근에서 삼각형마다 flush 가능 | `Hook.c:2691` (이전 세션에서 확인) | ✅ 채택 (알고 있던 것) |
| 프레젠트 모드는 swap 때만 arm; 그 전·포커스 상실·창 이동 시 렌더 브래킷이 VRAM 을 RAM 으로 미러 | `SDL_openstepvideo.m:2445/2537` 확인 | ✅ 채택. 초기 프레임·창 이동이 느린 별도 원인 |
| `R_RenderDlights` 가 SMOOTH 를 켜고 안 되돌린다 | 확인 | ⏭️ 흰 정점색이라 화면 무영향; 행동 안 바뀜 |

### 4.3 codex 와 갈린 것

| codex 주장 | 내 검증 | 판정 |
|---|---|---|
| 근본 원인 1순위는 "커널 검증기 거절 → revoke", 라이트맵은 2순위 | 순서의 문제. revoke 는 **결과**이고 최종 영향이 가장 크다는 점은 맞다. 그러나 revoke 를 막아도 라이트맵이 SW 인 한 가속은 반쪽이고, 라이트맵을 고쳐도 revoke 가 나면 전부 SW 다. **둘 다 필수**이며 순서는 비용 순 | ⚖️ 부분채택 — 둘을 "필수 2건"으로 병렬 배치 |
| revoke 의 후보 판정은 13/site 6 또는 19(`E_TRIEMPTY`); 숫자 없이는 특정 불가 | 이미 측정함(codex 는 문서를 못 봤다): 판정 13, `row-ends` 지배, 재기준 후 잔여 158 | ✅ codex 의 방법론이 맞고 답은 이미 있다 |
| 유저랜드에서 **제출 전에 검증기를 미리 돌려** 나쁜 삼각형을 걸러라 (검증기가 `libGL_mga.a` 에 이미 링크됨) | `build-matrox-mesa.csh:143` 확인 | ✅ **채택. 이것이 핵심 훅이다.** 커널 거절 자체가 사라지므로 revoke 는 구조적으로 불가능해지고, 잔여는 조용한 SW 낙하가 된다. 커널 변경 없음 |
| 정상 재생된 `E_TEXCOORD`/`E_TRIEMPTY` 는 revoke 에 가산하지 말 것 | `osmgaMesaGeometryVerdict` 가 9·17 만 사면함을 확인 | ✅ 채택 (프리검증 뒤 잔여 안전망) |

## 5. 확정 계획 — GLQuake 가 요구하는 훅을 구현한다

원칙: **커널은 건드리지 않는다.** 아래는 전부 유저랜드(훅·빌더·포트)이고 재부팅이
없다. 각 단계는 측정으로 닫는다.

### H0. 계측 (측정 없이는 아무것도 확정하지 않는다) — 유저랜드
`mgastats` 를 확장: 판정 히스토그램, 마지막 거절 기록, 재생/narrow 수, flush 사유,
텍스처 업로드/거절/퇴거, `hookFlushKey`. 그리고 종료 시 자동 출력.
**측정**: 한 프레임의 일을 "게이트 거절 / 제출 전 낙하 / 제출 후 거절 / 그려짐"
으로 정량 분할할 수 있어야 한다.

### H1. 라이트맵 훅 — 포트 (코드 한 줄) + 측정
`isPermedia = true` 상당(백엔드 기본 RGBA). 우선 `-lm_4` 로 **재빌드 없이** 확인.
**측정**: `soft` 급감, `read back` 0. 대조군 `r_fullbright 1`. 화면은 `glquake_sw`
와 비교.
(일반해 — 훅이 LUMINANCE 를 32비트로 확장 — 는 다른 앱을 위해 뒤로 미룬다.)

### H2. 제출 전 검증 훅 — 훅
`osmgaHW3DValidateReachSite` 를 배치 제출 **전에** 유저랜드에서 돌려, 거절될
삼각형을 빼고(SW 재생) 나머지만 ioctl. 커널 거절이 0 이 되어 revoke 는 구조적으로
사라진다.
**측정**: `declined` 0, 카운터의 `refusals(all)` 증가 0, 화면 동일.

### H3. revoke 정책 — 훅 (H2 뒤의 안전망)
정상 재생된 per-triangle 판정(13, 19)은 연속 거절 run 에 가산하지 않는다.
배치 수준 실패는 계속 가산.
**측정**: 주입한 나쁜 삼각형은 replay 카운터만 올리고 run 은 안 올린다.

### H4. 필터 강제 — 포트
`gl_texturemode` 가 밉맵을 고르면 기존 객체까지 순회해 되돌린다(업스트림 명령과
같은 루프). 기본 경로에는 영향 없지만 사용자 명령 하나로 전부 SW 가 되는 함정을
막는다.

### H5. 혼합 비용 — 훅 (H1·H2 뒤에 측정으로 결정)
`flush` 가 여전히 높으면: `TexSubImage` 더티 사각형, 상주 LRU, 원근 배치 키.
**측정**: H0 의 flush 사유 분포가 결정한다.

### H6. 그 다음에야 fps
`timedemo demo1` 프레임 정규화. 그 전의 fps 는 증상이다.

### 보류 (커널·별건)
좌표 정책 상한 확장(하드웨어 검증 필요), 깊이만 지우기, 밉맵, 멀티텍스처,
WARP 기본화(화면 비교 뒤).

## 6. H0·H2 구현과 코드 교차검토 (gpt-5.6-sol)

### 구현
- **H0**: 게이트가 거절한 상태에서 그려지는 삼각형을 세는 래퍼(`osmgaMesaGatedTriangle`),
  마지막 거절의 지점, 게이트의 **어느 줄이** 거절했는지(`__LINE__`)와 횟수,
  `mgastats` 확장 + N 프레임마다 stderr 덤프 + SIGTERM → 정상 종료.
- **H2**: `osmgaMesaSubmitBatch` 가 ioctl 전에 커널과 같은 검증기를 돌려, 삼각형을
  지목하는 판정이면 커널을 부르지 않고 그 삼각형만 소프트웨어로 재생(기존 narrowing
  경로 재사용). 로컬 거절은 revoke run 에 가산하지 않는다.

### 측정 (60초, WARP=0)
- **revoke 0** — H2 의 구조적 목표 달성. 이전엔 첫 월드 프레임에서 항상 revoke.
- 그러나 첫 월드 프레임이 45초 안에 끝나지 않는다. gdb 표본: `Draw_Character →
  osmgaMesaMirror → OSMGAMesaBufferMirror` — 콘솔 글자(quad 하나)마다 640×480 전체를
  VRAM→RAM 으로 복사. `hookDrawn` 정지, `gated` 증가 → 맵 로드 뒤 게이트가 소프트웨어로
  돌아섰고, 그 상태에서 브래킷마다 전체 미러가 돈다.

### 판정표

| codex 주장 | 검증 | 판정 |
|---|---|---|
| prefix 가 커널에서 거절되면 `res` 가 덮여 같은 거절을 두 번 세고, 로컬 거절이 커널 거절로 둔갑한다 | `Hook.c` 1840/1859 확인 | ✅ **채택. 제 변경 전부터 있던 이중 계수.** 지목 판정을 `named` 에 따로 보관 |
| 열린 창 범위는 삼각형 단위 판정을 바꾸지 않는다; 커널은 stride/containment 를 검증기 전에 따로 본다 | `.m:6693-6707` | ✅ 확인 |
| 검증기는 `const` 배치에 부작용 없음, 출력은 매 호출 초기화 | `HW3D.c:635-647` | ✅ 확인 |
| 로컬 `E_TEXCOORD` 가 히스토그램·site 어디에도 안 남는다 | 코드 | ✅ 채택. 로컬 히스토그램·last 추가 |
| `OSMGA_MESA_VERDICTS 24` 는 판정 24(`E_ALPHACROSS`)를 못 담는다 | `HW3D.h:131` | ✅ 채택. 32 로 |
| 게이트 이유가 리터럴 줄번호라 편집하면 틀어짐; 텍스처 게이트는 안팎 두 번 기록 | 코드 | ✅ 채택. `__LINE__`, 바깥 호출 지점 제외 |
| 래퍼는 단일 컨텍스트에서 안전; 다중 컨텍스트에서는 `savedTriangle` 전역 하나라 조용히 버릴 수 있다 | 코드 | ⚖️ 사실. GLQuake 는 단일 컨텍스트. 일반 계약 결함으로 기록만 |
| 검증 비용이 무시할 수준이 아닐 수 있다(텍스처 프리미티브당 행 순회 2회) | 문서 인용 | ⏭️ 측정으로 답한다(H0 덤프의 submit 시간) |
| 브래킷마다 전체 미러: RenderStart 의 `osmgaMesaSoil` 이 무조건 dirty, `bufPresent` 가 꺼져 있으면 매 브래킷 복사 — 우발 버그가 아니라 보수적 전달 모델의 비용이나 GLQuake 에는 명백한 병목 | `Hook.c` Soil/Mirror 확인 | ✅ 확인. **다음 질문은 왜 맵 로드 뒤 `bufPresent` 가 꺼지는가** (SDL 이 stamp 를 해제하는 조건) |
| `sig_atomic_t`, 죽은 `mga_stats_next`, 환경값 단위 불명확, warp0.sh 가 STATS_EVERY 를 안 넣음 | 코드 | ✅ 채택, 전부 수정 |
| 테스트가 `-lm_4` 를 안 넣는다 | — | ⏭️ 의도. H1 은 H2 확인 뒤 별도로 |

### 다음 (실기 복귀 후, 60초 한 번)
게이트가 *어느 줄에서* 거절하는지(`gate refused` 줄)와 `bufPresent` 가 꺼진 이유를
같은 실행에서 읽는다.

## 7. 실기 없이 진행한 것 (실기 다운 중)

- **H1 적용**: 포트의 `isPermedia = true`. 이 빌드에서 그 변수를 읽는 곳은
  `gl_rsurf.c:1616` 하나(라이트맵 기본 RGBA)뿐임을 전수 grep 으로 확인. `-lm_1` 로
  되돌릴 수 있다. 실기 확인 대기.
- **present 모드 계측**: `OSMGAMesaBufferPresentMode` 의 on/off 전환 횟수와
  프레젠트 ioctl 의 거절 판정 히스토그램을 `mgastats` 에 추가. 맵 로드 뒤
  `bufPresent` 가 꺼지는 경로는 코드상 셋뿐이다 — (a) 포커스 상실/창 크기·컨텍스트
  변경, (b) stamp 가 `E_BUSY`/`E_DST` 로 거절 → 그 프레임만 release, 다음 swap 에서
  재arm, (c) `surface_origin()==0`. (b)면 거절 한 번마다 **다음 프레임 전체가 브래킷당
  전체 미러**로 그려진다. 60초 실행 한 번이 셋 중 어느 것인지 답한다.
- **SDL 버그 발견(미수정)**: `gl_stamp_barred` 가 서면 `StampArm` 이 release 없이
  FALSE 를 돌려주므로 present 모드가 켜진 채 남고, AppKit 경로는 갱신되지 않은
  배열을 그린다. SDL 재빌드가 필요하므로 별도 항목으로 미룬다.

## 8. SDL1 → SDL2 대응 감사 (실기 다운 중, 소스만으로)

업스트림 SDL 1.2 호출 27종을 포트의 대응과 대조. 올바른 것: 8bpp 화면 → INDEX8
서피스+블릿, `SetColors` → `SetPaletteColors`, `UpdateRects` → 더티 변환+전체
프레젠트, `WarpMouse` → `WarpMouseInWindow`, 오디오 API 동일, CD → null, GL 깊이
16비트.

부족한 것과 조치:
1. **팔레트 변경 뒤 더티 사각형만 재변환** → 상태바 등이 이전 색으로 남음.
   `VID_SetPalette` 가 플래그를 세우고 다음 `VID_Update` 가 한 번 전체 블릿. (적용)
2. `SDL_GL_DEPTH_SIZE 16` 을 명시. (적용)
3. `D_EndDirectRect` 가 비어 로딩 디스크 아이콘이 프레임 사이에 안 보임. (기록)
4. SDL2 키 반복이 기본 켜짐(업스트림은 꺼짐). 게임 중은 `Key_Event` 가 거름. (기록)
5. 포커스 상실 시 키 상태 미해제(업스트림 동일). (기록)
6. 마우스: 프레임당 마지막 delta 만 반영하던 결함 → 누적으로 수정, codex 재검토
   반영(중앙 가드 제거, 실제 창 크기 기준 warp, 펌프당 1회, `mouse_avail` 가드).
   `PSsetmouse` 의 물리 이동과 커서 숨김 상태의 `mouseMoved` 수신은 실기 확인 필요.

## 9. 육안 검증 결과와 미해결 이슈 (2026-08-31)

T4(WARP 재기준) 실기 육안 검증: **화면 정상** — 텍스처 정위치, seam 없음, 깊이
정상, 프레임 연속 갱신. 모든 삼각형 WARP, 소프트웨어 0, revoke 0.

나중에 별도 작업으로 기록:
1. **커서 grab/relative 모드 미구현** — 마우스 이동이 왼쪽 버튼을 누른 동안만
   시점 회전으로 들어오고(드래그 이벤트), 버튼을 떼면 OS 커서만 움직인다.
   즉 OPENSTEP 백엔드가 `mouseMoved` 를 시점 입력으로 못 쓰고 있고, grab
   (커서 숨김+가둠+warp 또는 relative 모드)을 SDL 백엔드에 구현해야 한다.
2. **사운드 ~1초 지연** — 발사음이 약 1초 뒤에 난다. SDL 오디오 콜백 큐/
   mixahead/samplepos 경로의 지연 분석 필요.

## 10. T5 — 클립 flush 병합 (2026-08-31)

클립 임시 정점 소스도 배치에 지연하되 재생 불가로 표시; 재생이 요구되면
드랍·계수하고 클립 지연을 런 전체에서 래치 해제(즉시-flush 복귀).
계획·검증: openstep-matrox-remade/docs/T5_CLIP_DEFER_PLAN.md (codex 판정표 포함).

레벨 프레임 190 → **153 ms (6.5 fps)**, 제출 542 → 155회/프레임, 클립 flush 0,
손실·거절 0. fail-safe 주입 시험 통과(드랍 2 → 래치 → 재생 569 정상).

남은 프레임 비용: 제출 66.5 ms(bracket 155회 — 다음 후보는 bracket 간 병합),
나머지 ~87 ms 는 Mesa immediate-mode T&L CPU 비용.

## 11. T&L 측정 — 프레임 시간의 해부 (2026-08-31)

bracket 시계(RenderStart 진입~RenderFinish 반환, `OSMGA_STATS_TIME=1` 로 arm,
mgastats `bracket` 줄)와 Quake `host_speeds 1` 을 겹쳐 레벨 프레임 153 ms 를
분해했다:

| 구간 | ms | 내용 |
|---|---|---|
| 제출 | 66.4 | ioctl 진입+인코딩+완료 대기 (155회/프레임) |
| bracket 내부 비제출 | 40.1 | Mesa T&L(변환·클립·셰이드·투영)+래스터셋업+훅 |
| bracket 외부 | 46.8 | 그중 게임 로직(server+cl)은 **~2 ms** — 나머지는 R_RenderView 의 BSP/표면 순회 + glVertex/glTexCoord 디스패치 + Mesa VB 축적 |

host_speeds: tot 120 = server 2.2 + gfx 118.3 (렌더가 프레임의 98%).
참고로 WARP 인자 버그로 우연히 잰 trapezoid 티어는 ~300 ms/프레임 — WARP 이
프레임을 절반으로 줄이고 있음을 재확인.

다음 후보의 상한(제출 모형 85.4µs+132.4ns/dw):
- bracket 간 병합: ~10-13 ms (state 변경 43회/프레임이 하한)
- Mesa T&L 내부: 40 ms 전부가 이론 상한, 범용 Mesa 코드라 난이도 높음
- 디스패치/순회 47 ms: GLQuake 쪽 호출 수 감소(immediate-mode 축소)가 지렛대

주의: run-glquake-self.sh 의 5번째 이후 인자는 전부 EXTRA 로 전달된다(따옴표
불필요). WARP 인자는 shift 전에 캡처한다 — 한 번 틀려서 trapezoid 로 쟀다.

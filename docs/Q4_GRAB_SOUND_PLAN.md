# Q4 — 마우스 grab 과 사운드 지연 (계획, 2026-08-31, 코딩 전)

두 사용성 결함(Q3 §9 에 기록)의 수정 계획.  구현 전 codex 교차검토 대상.

## 1. 마우스 — 확인된 사실과 원인 사슬

### 1-1. 증상
버튼을 누른 채로만 시점이 돌고, 버튼을 떼면 OS 커서가 데스크톱을 돌아다닌다.

### 1-2. 사실 (전부 파일:줄 확인)
- 백엔드는 mouseMoved 를 구현하고(`SDL_openstepvideo.m:1006-1013`) 창 생성 시
  `setAcceptsMouseMovedEvents:YES`(`:1527`, `:1810`).  **moved 이벤트 자체는
  들어온다** — 단 AppKit 규칙상 **포인터가 뷰 안에 있고 창이 key 일 때만**.
- 포인터가 뷰를 나가면 `mouseExited` 가 SDL 마우스 포커스를 지운다(`:1024-1029`).
  이후 이동 이벤트는 오지 않는다.
- `mouseDragged`(버튼 든 채 이동)는 AppKit 이 **위치와 무관하게** 전달하고,
  백엔드는 moved 로 전달한다(`:1031-1037`).  → 버튼을 누르면 도는 이유.
- in_sdl.c 의 되돌림 warp 는 **창 4분의 1 문턱을 넘은 이벤트가 왔을 때만**
  발동한다(`in_sdl.c:186-205`).  1600×1200 데스크톱 위 640×480 창에서는 빠른
  스윕이 문턱 이벤트 없이 창을 벗어날 수 있고, 벗어난 뒤에는 이벤트도 warp 도
  없다.  → 버튼을 떼면 OS 커서가 나가버리는 이유.
- `OPENSTEP_WarpMouse` 는 `lockFocus` 아래 `PSsetmouse` 를 부른다(`:321-339`).
  **물리 포인터를 실제로 옮기는지는 실기 미검증**(Q3 이후 미결 항목) —
  위 증상은 "warp 가 물리적으로 안 됐다" 로도 설명이 가능하다.  계획의 첫
  실기 단계가 이것을 가른다(§3-V0).
- 보이지 않는 커서는 이미 있다(`OPENSTEP_CreateInvisibleCursor`, `:132`),
  `SDL_ShowCursor(SDL_DISABLE)` 경로도 구현돼 있다(`:285-318`).
- `SetWindowTitle` 도 구현돼 있다(`:1304`).

### 1-3. 설계 — 게임측 grab (SDL 백엔드 무변경이 기본)
in_sdl.c 에 grab 상태 하나(`in_grabbed`, 기본 **ON**, `-nomouse` 면 무관):

```
grab ON  일 때:  SDL_ShowCursor(DISABLE);
                 펌프마다 1회, 입력 포커스가 있을 때만, 무조건 중앙 warp
                 (문턱 없음 -- 창을 벗어날 틈을 주지 않는다)
grab OFF 일 때:  SDL_ShowCursor(ENABLE); warp 없음;
                 마우스 이동·버튼을 게임에 넣지 않는다 (키보드는 유지)
토글:            Shift+Ctrl+G (KEYDOWN, KMOD_SHIFT && KMOD_CTRL && sym=='g')
                 -- 해당 g 의 down/up 을 게임 키로 보내지 않는다(스턱 방지)
포커스 상실:     SDL_WINDOW_INPUT_FOCUS 없으면 warp 하지 않는다
                 (다른 앱을 쓰는 사용자의 포인터와 싸우지 않는다)
```

**타이틀바 안내** (사용자 요구): 토글·초기화 때 `SDL_SetWindowTitle`:
```
grab ON :  "GLQuake  [mouse grabbed -- Shift+Ctrl+G releases]"
grab OFF:  "GLQuake  [mouse free -- Shift+Ctrl+G grabs]"
```

두 엔진(glquake·squake)이 in_sdl.c 를 공유하므로 한 곳 수정으로 둘 다 얻는다.

### 1-4. 대안(백엔드 상대 모드)을 안 하는 이유
SDL_SetRelativeMouseMode 를 백엔드에 넣는 것이 정석이지만, 뷰 밖 이동은
AppKit 이 주지 않으므로 결국 warp 기반이 되고, 그 warp 루프는 지금 게임측에
이미 있다.  백엔드 API 를 늘리는 것보다 사용처에서 완성하는 편이 작다.
(단 §3-V0 에서 PSsetmouse 가 물리 warp 가 아니라고 판명되면 백엔드의
WarpMouse 자체를 고쳐야 하며, 그때는 백엔드 수정이 범위에 들어온다.)

## 2. 사운드 지연 — 확인된 사실과 산술

### 2-1. 지연의 소재 (전부 확인)
- Quake 는 `desired_speed = 11025`(snd_dma.c:66), samples 512 를 요청한다.
- 백엔드가 11025 를 거부하고 44100 으로 올린 뒤 **`samples = freq/4 = 11025`
  (= 250 ms)** 로 강제한다(`SDL_openstepaudio.m:36-44`).
- `QUEUE_AHEAD = 4`(`SDL_openstepaudio.h:11`): WaitDevice 는 큐에 4개가 쌓일
  때까지 막지 않는다 → 정상 상태 큐 깊이 ≈ 4 × 250 ms = **1.0 초**.
  여기에 Quake mixahead 0.1 s.  **관측된 ~1초 지연과 정확히 일치한다.**
- 프레임 전체가 `SDL_LockAudio` 로 감싸여 있다(gl_vidsdl.c:593-615) — zone/
  cache 스레드 비안전 때문의 **의도된 설계**(주석에 코어덤프 근거).  콜백은
  프레임 사이에만 돈다.  이 락은 유지한다.

### 2-2. 설계 — 버퍼·큐 축소
지연 ≈ AHEAD × (samples/freq).  락 설계상 큐가 프레임 시간(~153 ms, 레벨
로드 중엔 수 초)을 버텨야 한다.  로드 중 끊김은 무해(어차피 정적)하므로
경계는 플레이 프레임이다:

```
안 A (제안):  samples = freq/8 (= 125 ms), AHEAD = 2   ->  ~250 ms + mixahead
              프레임 153 ms < 큐 250 ms, 언더런 없이 재생 지속
안 B (보수):  samples = freq/4 유지, AHEAD = 2          ->  ~500 ms
안 C (공격):  samples = freq/16 (= 62.5 ms), AHEAD = 3  ->  ~190 ms, 여유 34 ms
```

안 A 로 시작한다.  느린 프레임(복잡 장면 ~200 ms)에서 끊기면 안 B 로 후퇴.
SNDStartPlaying 호출은 초당 8회(안 A) — SoundKit 오버헤드 문제 없음.
변경 파일: `SDL_openstepaudio.m`(samples), `SDL_openstepaudio.h`(AHEAD).
SDL 라이브러리 재빌드 + glquake 재링크 필요.

### 2-3. 하지 않는 것
- `desired_speed` 22050 승격(변환 절감): 별개 최적화, 이번 범위 밖으로 기록.
- 프레임 락 제거: cachedir 수정으로 원인 하나가 없어졌어도 zone/cache 비안전
  논증은 그대로다.  건드리지 않는다.

## 3. 검증 (실기, 각 ≤60초)

```
V0  PSsetmouse 물리 warp 프로브: warp 직후 mouseLocationOutsideOfEventStream
    되읽기 + 사용자 육안(포인터가 실제로 움직였나).  아니면 §1-4 의 대안 발동.
V1  glquake: 버튼 없이 시점 회전, 커서 안 보임, 창 밖 이탈 없음
V2  Shift+Ctrl+G: OS 커서 복귀+타이틀 변경, 게임이 마우스 무시, 재토글 복귀,
    g 키 스턱 없음
V3  포커스 상실 중 warp 없음 (다른 창 클릭 후 데스크톱 조작)
V4  사운드: 발사음 지연 체감(사용자), 연속 재생 끊김 없음, 레벨 진행 중 정상
V5  회귀: 153 ms/프레임 불변, drawn 전량 WARP, 거절·손실 0; squake 도 빌드
```

## 4. codex 에 묻는 것

1. §1-2 원인 사슬의 검증 — mouseExited 후 이벤트 두절이 증상의 전부인가,
   놓친 갈래(예: key 창 조건, 첫 응답자, 트래킹 사각형 부재)가 있는가?
2. 펌프마다 PSsetmouse 1회(락포커스 왕복 포함)의 비용 — 프레임 153 ms 에서
   유의미한가?  lockFocus 를 매 펌프 하는 것의 부작용은?
3. warp 직후 주입하는 합성 SDL_SendMouseMotion(절대좌표) 이 상대 델타 누적과
   상호작용해 시점을 튀게 할 여지 — SDL2 코어의 warp 델타 처리 확인.
4. Shift+Ctrl+G 소비 방식 — down 만 삼키고 up 은 흘려도 되나, 둘 다 삼켜야
   하나?  Quake 의 Key_Event 대칭성.
5. ungrab 중 버튼까지 삼키는 UX 가 맞나(메뉴 클릭도 막힌다), 아니면 이동만
   삼켜야 하나?
6. 오디오 안 A 의 안전성 — WaitDevice/PlayDevice 흐름에서 AHEAD=2, 125 ms 로
   줄일 때 SDL 코어 콜백 스케줄과 SNDWait 의 상호작용, 언더런 시 SoundKit 의
   회복 동작.  SND_ERR 경로에서 큐가 새는가?
7. 타이틀 갱신을 이벤트 펌프 문맥에서 불러도 되는가(OPENSTEP_SetWindowTitle
   의 스레드/컨텍스트 전제).
8. 빠진 것.

## 5. codex 교차검토 판정 (2026-08-31, gpt-5.6-sol) — GO with changes

| codex 주장 | 내 검증 | 판정 |
|---|---|---|
| **안 A(freq/8+AHEAD=2)는 불안전** — 최악 위상 보장은 `(AHEAD-1)×버퍼`=125 ms < 153 ms. 콜백→Play→Wait 순서가 근거 | SDL_audio.c 콜백 루프에서 Play 직후 Wait 확인(770-780 부근).  내 산술이 틀렸다 — 첫 버퍼는 이미 재생 중 | ✅**채택 — freq/8 + AHEAD=3** (보장 250 ms, 명목 지연 ~375 ms + mixahead).  끊기면 freq/16+AHEAD=5(보장 250, 명목 312) 시도 |
| 워프 합성 델타는 정확히 0 — `last_x=목표, has_position=false` 후 드라이버 호출 | SDL_mouse.c:1077-1081, 592-597 직접 추적(사전 검증 §와 일치) | ✅확인 — 설계 유지 |
| 핫키 판정은 `SDL_GetModState` 가 아니라 **`event.key.keysym.mod`** — 펌프가 네이티브 큐를 통째로 소진하므로 전역 상태는 미래를 본다 | 펌프 구조(:2682-2697)와 이벤트별 modstate 스냅샷 확인 | ✅채택 |
| G-down 을 삼켰으면 대응 **G-up 도 latch 로** 삼켜야(스턱/고아 -command 방지), `repeat==0` 가드 | keys.c 의 up 처리(-command 생성) 논거 타당 | ✅채택 |
| 토글 시 **눌린 버튼 release 합성 + oldstate·누적 델타 클리어** | in_sdl.c 버튼이 폴링(:224)임을 확인 | ✅채택 |
| 커서 숨김이 두 vid 백엔드에 **무조건** 있음 → `-nomouse` 도 숨겨짐; 소유권을 IN_SetWindow 로 | vid_sdl.c:183, gl_vidsdl.c:778 확인 | ✅채택 |
| IN_Init 이 VID_Init **보다 먼저**(host.c:885) — 초기 커서/타이틀은 IN_SetWindow 에서 | 확인 | ✅채택 |
| 타이틀은 엔진명 보존 + 짧은 접두("sdlquake"/"glquake" 각각) | vid_sdl.c:117, gl_vidsdl.c:686 확인 | ✅채택 — `"[Shift+Ctrl+G ...] <원제>"` 형식, 원제는 SDL_GetWindowTitle 로 IN_SetWindow 에서 보관 |
| grab 은 엄밀한 잠금이 아니라 주기적 재중앙 — 프레임 사이 창밖 클릭으로 포커스를 잃을 수 있음 | 논거 타당 (153 ms 간격) | ⚖️수용 — 명시적 한계로 문서화, V1 에 창밖 클릭 시험 추가 |
| §1-2 인과 서술 정정: mouseExited 는 결과이지 원인이 아님(창 밖 = moved 전달 조건 상실); 트래킹 사각형은 이미 있음(:926, :1210) | 확인 | ✅채택 — 서술 수정 |
| 포커스 회복 시 재중앙 전 첫 모션의 대형 델타 | has_position 지속 → 가능 | ✅채택 — 회복 첫 펌프는 warp 후 누적 버림 |
| SNDStartPlaying 실패 시 count 미증가로 콜백 고속 재시도 여지; 언더런 계측 필요 | 코드 확인 | ⚖️부분채택 — 백엔드에 underrun/실패 카운터 추가(CloseDevice 에서 SDL_Log), 재시도 폭주는 이번 범위 밖 기록 |
| squake 는 오디오 재생 회귀도(프레임락 없음) | 타당 | ✅채택 — V5 확장 |
| 메뉴/콘솔·pause 중 grab 정책 명시 | 타당 | ✅채택 — 수동 토글 단일 정책(자동 해제 없음)으로 문서화 |

### 확정 스펙 (구현 대상)
- in_sdl.c: `in_grabbed`(기본 ON), 펌프당 1회 무조건 중앙 warp(입력 포커스
  시), 토글 = KEYDOWN sym=='g' && (keysym.mod SHIFT+CTRL) && !repeat,
  G-up latch, 토글 시 버튼 release 합성+상태/델타 클리어, ungrab 중 이동·버튼
  차단, 포커스 회복 첫 펌프 누적 버림.
- IN_SetWindow: 원제 보관, 초기 커서·타이틀 적용.  vid_sdl.c/gl_vidsdl.c 의
  무조건 SDL_ShowCursor(0) 제거.
- 타이틀: `"[Shift+Ctrl+G frees mouse] <원제>"` / `"[Shift+Ctrl+G grabs mouse] <원제>"`.
- SDL_openstepaudio: samples = freq/8, QUEUE_AHEAD = 3, underrun·실패 카운터.
- 검증 V0~V5 (§3) + 창밖 클릭 시험, squake 오디오 회귀.

## 6. V1 실패 — 시점이 전혀 돌지 않는다 (2026-08-31, 원인 확정)

### 증상
grab·커서 숨김·봉쇄·클릭·타이틀 전부 정상.  **마우스 이동이 시점을 전혀
돌리지 못한다** — 이전(버튼 들고는 돌았음)보다 오히려 후퇴.

### 원인 — warp 의 리셋이 다음 '진짜' 이벤트의 델타까지 지운다
`SDL_PerformWarpMouseInWindow` 는 `last_x/y = 목표점, has_position = FALSE`
로 리셋한다(SDL_mouse.c:1077-1081).  검토 때는 "warp 의 합성 모션이 델타 0"
까지만 걸었고 그건 맞다.  놓친 것은 그 다음이다:

**warp 후 처음 오는 진짜 모션도** `has_position == FALSE` 라서 첫-위치
규칙을 탄다 — `else if (mouse->has_position) xrel = x - last_x` 를 건너뛰고
(SDL_mouse.c:586), `if (!has_position) { 위치만 설정 }` (:592-597) — **xrel=0
으로 큐에 들어간다.**  그 이벤트에 실린 이동량 전부가 증발한다.

매 펌프 warp 구조에서 이것이 치명적인 이유: AppKit 은 mouse-moved 를
**병합**하므로 펌프당 모션 이벤트는 사실상 한 개다.  펌프마다 warp →
`has_position=FALSE` → 다음 펌프의 유일한 모션이 첫-위치로 소화 → 델타 0 —
**모든 펌프에서 모든 이동이 삼켜진다.**  관측된 완전 무반응과 정확히
일치한다.  (기존 문턱 warp 에서는 warp 가 드물어 대부분의 이벤트가 정상
델타를 가졌다 — 그래서 "가끔 씹히는" 정도로 안 보였던 것.)

교차검토도 나도 "warp 자신의 이벤트" 까지만 추적하고 "warp 다음의 첫 진짜
이벤트" 를 걷지 않았다.

### 수정안 — 델타를 SDL 의 장부가 아니라 우리가 계산한다
`xrel` 을 버리고 **절대좌표에서 직접 차분**한다.  in_sdl.c 에 자체 기준점:

```
static int in_prevX, in_prevY, in_prevValid;

MOUSEMOTION:
    if (in_prevValid) {
        mouse_x += (event.motion.x - in_prevX) * 10;
        mouse_y += (event.motion.y - in_prevY) * 10;
    }
    in_prevX = event.motion.x;  in_prevY = event.motion.y;  in_prevValid = 1;

warp 직후:
    in_prevX = ww/2;  in_prevY = wh/2;  in_prevValid = 1;
```

- warp 뒤 첫 진짜 이벤트: `(x - 중앙)` = warp 이후의 실제 이동 전부.  SDL 이
  0 을 주든 말든 무관.
- warp 자신의 합성 이벤트: 위치 = 중앙 = prev → 차분 0 (게다가 SDL 이
  '상태 불변' 으로 큐에서 떨어뜨리는 경우도 무해).
- 펌프 안 다중 이벤트: 연쇄 차분이라 이중계상 없음.
- 포커스 회복: 기존의 누적 버림 유지 + 회복 첫 warp 가 prev 를 중앙으로
  재설정하므로 진입 이벤트의 점프도 흡수.
- ungrab 중에는 prevValid 를 지운다(재grab 첫 이벤트 점프 방지 — warp 가
  다시 세팅할 때까지).

### 검토받을 점
1. 차분 기준 전환이 놓치는 경우 — 창 크기 변경, 이벤트 좌표계(창 픽셀)와
   warp 좌표의 일치, coalescing 이 안 될 때의 다중 이벤트.
2. `*10` 스케일 유지가 맞는가(기존 xrel 경로와 체감 동일성).
3. mouseEntered/becomeKey 의 절대 모션 주입이 이 차분에 미치는 영향.

## 7. 런타임 트레이스가 §6 의 진단도 뒤집었다 (2026-08-31)

codex 는 §6 의 수정에 NO-GO 를 줬다 — 백엔드 WarpMouse 가 warp 안에서 즉시
합성 모션을 보내 첫-위치 규칙을 소진하므로, 소스대로면 다음 진짜 이벤트의
xrel 은 정상이어야 한다는 반증이었다.  트레이스가 그 반증을 확인했고, 더
깊은 사실을 보여줬다:

```
MM 이벤트 1,400개 중 '중앙(320,240)/델타0' 아닌 것: 3개
1,397개의 빈도 == warp 주기(~6.5/s)      -> 전부 warp 합성
의미 있는 3개: (13,397,r0) (385,364,r372) (604,381,r0)  -> 재진입 주입 모양
```

**grab 상태에서 손을 움직여도 진짜 mouseMoved 는 한 개도 오지 않는다.**
`setAcceptsMouseMovedEvents:YES`, 전면 트래킹 사각형, first responder,
key window 전부 갖춰져 있는데도(§1-2) 그렇다.  dragged 는 온다(원래 증상).
왜 안 오는지는 AppKit/WindowServer 레벨의 미규명 문제로 남긴다 —
`test/openstep/openstep-appkit-input-record-probe.m` 이 순수 AppKit 재현
프로브로 존재하며, 규명은 후속 과제다.

### 수정 v2 — 이벤트를 기다리지 않고 위치를 폴링한다

moved 배달 여부와 무관하게 성립하는 경로가 이미 있다:
`SDL_GetGlobalMouseState` -> 백엔드 `OPENSTEP_GetGlobalMouseState` ->
`mouseLocationOutsideOfEventStream` — *"이벤트가 없어도 현재 위치"* 를
AppKit 문서가 보증하고, 백엔드는 그것을 SDL 창 로컬 좌표로 변환해 준다
(SDL_openstepvideo.m:341-380).

```
펌프 끝(grab·포커스 시):
    SDL_GetGlobalMouseState(&px, &py)          현재 위치 (창 로컬)
    mouse_x += (px - cx) * 10                   중앙과의 차 = 이 프레임의 이동
    mouse_y += (py - cy) * 10
    SDL_WarpMouseInWindow(중앙)                 기준점 복원
MOUSEMOTION 이벤트는 시점 회전에 쓰지 않는다 (버튼은 기존 폴링 그대로)
```

- 프레임당 1 샘플 = 어차피 이벤트가 왔어도 병합돼 얻었을 해상도.
- warp 가 물리적으로 안 먹으면 다음 폴이 중앙이 아니게 되어 **폴 값 자체가
  진단이 된다** (연속 대형 델타 = warp 실패).
- 한계: 포인터가 창 밖까지 나가면 GetGlobalMouseState 의 좌표가 창 밖
  값이라 델타가 과대해질 수 있다 -> 폴 좌표를 창 크기로 클램프.
- SDL 창 좌표 원점 일치: 이벤트와 같은 뷰-로컬 변환을 쓰므로 동일.
- **자체 사전검증에서 찾은 가드**: 백엔드 GetGlobalMouseState 는
  `mouse->focus` 가 NULL 이면 (0,0) 을 반환한다(mouseExited 직후 등).
  그대로 차분하면 중앙과의 차 -320 이 유령 델타가 된다 →
  `SDL_GetMouseFocus() == in_window` 일 때만 폴·누적한다.

### 미해결 부속 관찰
트레이스의 의미 있는 3개가 전부 grab=0 에서 왔다 — 사용자가 이 인스턴스에서
핫키를 눌렀는지 확인 필요.  아니라면 토글 오발화 조사.

## 8. 가속 선형화 (2026-08-31, 프로브 후 계획)

### 증상과 원인
폴링 grab 으로 회전은 되지만 "버벅"인다.  폴링은 **가속이 적용된 위치**를
읽는다 — OPENSTEP 이벤트 시스템이 커널에서 델타에 가속 표를 곱한 결과다
(spacesaver2 internals §4: `scalePointerInX:andY:over:atRes:` 가속 테이블,
`Evs_SetMouseScaling` 파라미터).  실기 프로브(`test/mouse-scaling-probe.c`,
읽기 전용, cc 만으로 링크됨):

```
numScaleLevels=5
threshold 1 -> factor 1     느린 손:  1:1
threshold 6 -> factor 2
threshold 7 -> factor 3
threshold 8 -> factor 5
threshold 9 -> factor 7     빠른 손:  7배
```

빠른 스윕이 7배로 튀니 게임 시점이 널뛴다.  이것은 드라이버(SpaceSaver2 든
표준 PS2Mouse 든)가 아니라 **이벤트 시스템 층**의 일이므로, 같은 층의 정식
API 로 제어하는 것이 기계-일반적이다: `NXGetMouseScaling` /
`NXSetMouseScaling` (drivers/event_status_driver.h:75-76, 추가 라이브러리
불필요 — 프로브가 증명).

### 설계
```
IN_SetWindow:  h = NXOpenEventStatus(); NXGetMouseScaling(h, &saved)
grab ON  시:   linear = {1, {1}, {1}}; NXSetMouseScaling(h, &linear)
grab OFF 시:   NXSetMouseScaling(h, &saved)
IN_Shutdown:   복원 + NXCloseEventStatus   (Sys_Quit 경유 SIGTERM 포함)
```
- 스케일링은 **시스템 전역**이다: grab 동안 데스크톱 커서도 1:1 이 되지만
  grab 중엔 커서가 숨겨져 있고, ungrab·종료가 복원한다.  크래시로 복원을
  못 하면 가속이 1:1 로 남는 잔여 위험 — Preferences 마우스 슬라이더로
  복구 가능함을 문서화.
- linear 를 numScaleLevels=0 이 아니라 **1단계 {1,1}** 로 명시한다 — 0 의
  의미(표 없음)가 커널 경로마다 다를 수 있어 명시 1:1 이 안전.
- 게임 체감 스케일: 가속 제거로 빠른 스윕의 델타가 최대 7배 줄어든다 —
  in_sdl 의 *10 은 유지하고 부족하면 게임 cvar `sensitivity` 로 조정(기존
  이용자 경로).
- 남는 "버벅" 요소: 표본율이 프레임율(6.5 Hz)에 묶인 것은 이 층에서 못
  고친다 — 한계로 기록.

### 8.1 잔여 "로봇 스텝" 과 증폭 제거 (2026-08-31)

선형화 후에도 시점이 1,3,5,7,9도 식으로 계단졌다.  원인은 배율 양자화:
샘플당 픽셀 수 x **10배 증폭** x sensitivity(3) x m_yaw(0.022) 라서 최소
단위(1픽셀)가 0.66도씩 점프했다.  10배는 가속 시절 이벤트 경로의 유산 —
제거했다.  이제 1픽셀 = 0.066도가 최소 단위이고, 속도는 게임 cvar
`sensitivity` 로 조정한다(권장 시작값 8~12).  남는 계단감은 표본율이
프레임율(6.5 Hz)에 묶인 시간적 한계로, 이 층에서는 더 줄일 수 없다.

## 9. 진짜 원인 — 버퍼된 warp 가 질의 직전에 착지해 손 이동을 지운다 (2026-08-31)

증상 사슬을 프로브 다섯 개로 좁혔다:

1. per-pump 계측: 등속 스윕 20초에 400 펌프 중 374개 델타 0 — 폴이 중앙만 봄.
2. mouse-live-probe: warp 없는 문맥에선 두 질의 채널 모두 손을 완벽 추적.
3. mouse-channels-probe: 서버 이벤트 마스크 moved 비트 ON, accepts=YES 에서
   moved 이벤트 도착(드레인 시 초당 1개) — "구독 안 됨" 가설 기각.
4. **mouse-accepts-probe**: warp 가 있는 두 칸(C·D)만 얼음 — 읽기가 warp 1초
   뒤인데도 warp 지점 고정.  사용자 관찰("커서가 자유롭게 움직이다 한 번씩
   튐")과 결합하면: **PSsetmouse 는 DPS 버퍼에 남아 있다가 다음 DPS 왕복 —
   바로 우리의 위치 질의 — 때 실행된다.**  게임의 매 펌프에서 이전 펌프의
   warp 가 질의 마이크로초 전에 착지해 손의 한 프레임 이동을 전부 지웠다.
5. **mouse-flush-probe (판정)**: 같은 주기에서 warp 직후 PSWait() 만 추가 —
   기존 18/18 샘플 중앙거리 0(전소) vs 플러시 18/18 손 위치 보존(중앙값
   859px).

수정: `SDL_WarpMouseInWindow` 직후 `PSWait()` 한 줄 (in_sdl.c, C 선언 수동).
비용: 펌프당 DPS 왕복 1회 추가 — 6.5 Hz 에서 무시 가능.

교훈: §6(첫-위치 규칙)·§7(질의가 이벤트-fed)·가속(§8)은 각각 실재했지만
주범이 아니었다.  주범은 격리 프로브 + 사용자의 육안 관찰("튀는 커서")이
합쳐져서야 보였다.

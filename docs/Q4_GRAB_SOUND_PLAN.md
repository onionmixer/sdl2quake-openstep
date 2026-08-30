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

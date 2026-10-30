# 06. 웹 UI 설계 (팀원 3)

## 1. 화면 구성

```
┌──────────────────────────────────────────────────────────────────┐
│ 🎬 Face Pause        장치: [xiao-01 ▾]    ● 브로커 연결됨  ● 장치 온라인 │  ← ① 상단 바
├──────────────────────────────────────────────────────────────────┤
│ [ https://www.youtube.com/watch?v=...                ] [불러오기] │  ← ② 링크 입력
│  최근: 강의 1편 · 뮤직비디오 · ...                                 │  ← ③ 최근 영상
├──────────────────────────────────────────────────────────────────┤
│                    ┌────────────────────────┐                    │
│                    │   YouTube 플레이어       │                    │  ← ④ 플레이어
│                    └────────────────────────┘                    │
│      ( 👀 시청 중 )       얼굴 제어  [ ON ●]                       │  ← ⑤ 상태 배지 + ⑥ 토글
├──────────────────────────────────────────────────────────────────┤
│ faces 1 · score 0.91 · fps 7.8 · 추론 95ms · Wi-Fi -58dBm         │  ← ⑦ HUD
└──────────────────────────────────────────────────────────────────┘

처음 접속 시 전체를 덮는 오버레이:  [ ▶ 시작하기 ]                     ← ⑧
```

## 2. 요소별 동작

| # | 요소 | 동작 |
|---|---|---|
| ① | 상단 바 | 장치 선택(`xiao-01` / `sim-01`, 선택값 저장), 브로커 연결 상태, 장치 status 표시 |
| ② | 링크 입력 | 붙여 넣은 링크에서 영상 ID 추출 → `player.cueVideoById(id)` (얼굴이 있으면 바로 재생). Enter로도 동작 |
| ③ | 최근 영상 | 최근 5개를 `localStorage`에 저장, 클릭하면 다시 불러옴 |
| ④ | 플레이어 | YouTube IFrame API. 초기 영상은 `.env`의 `DEFAULT_VIDEO_ID` 또는 마지막으로 본 영상 |
| ⑤ | 상태 배지 | `👀 시청 중`(초록) / `⏸ 자리 비움`(회색) / `⚠ 장치 오프라인`(빨강) / `⚠ 카메라 오류`(빨강) |
| ⑥ | 얼굴 제어 토글 | OFF면 presence를 무시하고 일반 플레이어처럼 동작 (데모 비상용) |
| ⑦ | HUD | telemetry 값 표시. 3초 이상 안 오면 흐리게 |
| ⑧ | 시작 오버레이 | 브라우저 자동 재생 정책 때문에 사용자가 한 번 클릭해야 소리 있는 재생이 가능. 클릭 전에는 얼굴 제어를 하지 않는다 |

## 3. 링크 → 영상 ID 규칙

| 입력 예 | 추출 ID |
|---|---|
| `https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=30s` | `dQw4w9WgXcQ` |
| `https://youtu.be/dQw4w9WgXcQ?si=xxxx` | `dQw4w9WgXcQ` |
| `https://www.youtube.com/shorts/dQw4w9WgXcQ` | `dQw4w9WgXcQ` |
| `https://www.youtube.com/embed/dQw4w9WgXcQ` | `dQw4w9WgXcQ` |
| `https://m.youtube.com/watch?v=dQw4w9WgXcQ` | `dQw4w9WgXcQ` |
| `dQw4w9WgXcQ` (ID만) | `dQw4w9WgXcQ` |
| 그 외 | 오류: "올바른 유튜브 링크가 아닙니다" |

영상 ID는 `[A-Za-z0-9_-]{11}`.

## 4. 재생 제어 규칙

```
presence 수신
  ├─ 시작 전(오버레이) 또는 토글 OFF → 상태 배지만 갱신, 플레이어는 건드리지 않음
  ├─ present → playVideo()
  └─ absent  → pauseVideo()

status 수신
  ├─ offline / error → pauseVideo(), 빨간 배지
  └─ online          → 배지만 갱신 (재생은 다음 presence를 기다림. retained라 바로 옴)

브로커 연결 끊김 → pauseVideo(), "브로커 연결 끊김" (mqtt.js가 자동 재접속)
장치 변경 → 이전 장치 구독 해제, 새 장치 facepause/<id>/# 구독
```

- **상태가 바뀔 때만** 재생/정지를 호출한다. 그래서 얼굴이 있는 동안 사용자가 직접 정지를 눌러도 웹이 다시 재생시키지 않는다.
- 얼굴이 없는 상태에서 새 영상을 불러오면 정지 상태로 대기하다가, 얼굴이 나타나면 재생한다.
- 임베드가 금지된 영상(플레이어 `onError` 101/150) → "이 영상은 외부 재생이 허용되지 않습니다".

## 5. 파일

| 파일 | 내용 |
|---|---|
| `services/web/public/index.html` | 레이아웃, 외부 스크립트(mqtt.js, YouTube IFrame API) 로드 |
| `services/web/public/style.css` | 배지 색, 레이아웃 |
| `services/web/public/player.js` | 위 규칙 전부 |
| `services/web/40-fp-config.sh` | 컨테이너 시작 시 `.env` 값으로 `config.js` 생성 |

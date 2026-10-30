# 01. 시스템 아키텍처

## 1. 요구사항

| ID | 요구사항 | 담당 |
|---|---|---|
| F1 | 카메라 앞에 얼굴이 있으면 영상이 재생된다 | 전원 |
| F2 | 얼굴이 사라지면(자리 이탈, 고개 돌림) 영상이 일시정지된다. 재생 위치는 유지된다 | 전원 |
| F3 | 한두 프레임 검출 실패로 영상이 깜빡이듯 멈추지 않는다 (디바운스) | 팀원 2 |
| F4 | ESP32 전원이 꺼지거나 Wi-Fi가 끊기면 웹은 일시정지하고 "장치 오프라인"을 표시한다 | 팀원 2 (LWT), 팀원 3 (표시) |
| F5 | 웹에서 유튜브 링크를 붙여 넣어 원하는 영상을 불러온다 | 팀원 3 |
| F6 | 웹에서 현재 상태(얼굴 수, 점수, FPS, 추론 시간)를 볼 수 있다 | 팀원 2 (발행), 팀원 3 (표시) |
| N1 | 얼굴이 사라진 뒤 정지까지 **≤ 2초**, 얼굴이 나타난 뒤 재생까지 **≤ 1초** | 팀원 1·2 |
| N2 | 카메라 이미지는 ESP32 밖으로 보내지 않는다 | 팀원 1 |

## 2. 구성 요소

| 구성 요소 | 실행 위치 | 기술 | 역할 | 담당 |
|---|---|---|---|---|
| `face_sensor` | ESP32 (FreeRTOS 태스크) | Arduino, esp32-camera, **Edge Impulse FOMO** | 카메라 프레임 캡처 → 얼굴 검출 → `face_result_t`를 큐에 넣음 | 👤 팀원 1 |
| `presence_link` | ESP32 (FreeRTOS 태스크) | Arduino, WiFi, PubSubClient | 큐에서 결과를 읽어 디바운스 → 상태 변경 시 MQTT 발행, LWT, 텔레메트리, LED | 👤 팀원 2 |
| `broker` | PC, Docker | eclipse-mosquitto 2 | MQTT 브로커. ESP32는 TCP 1883, 브라우저는 WebSocket 9001 | 👤 팀원 3 |
| `web` | PC, Docker | nginx + HTML/JS, YouTube IFrame API, mqtt.js | 플레이어 페이지 ([06-web-ui.md](06-web-ui.md)) | 👤 팀원 3 |
| `sim` | PC, Docker | Python + paho-mqtt | 가짜 ESP32. 계약과 똑같은 메시지를 시나리오대로 발행 | 👤 팀원 3 |
| `fw` | PC, Docker (도구) | arduino-cli + ESP32 Arduino 코어 2.0.17 | 펌웨어 컴파일 확인, 디바운스 단위 테스트 | 👤 팀원 1·2 |

> ESP32에서는 Docker가 돌지 않는다. ESP32 안의 두 사람(팀원 1·2)은 **컨테이너 대신 스케치 안의 파일(`face_sensor.*` / `presence_link.*`)로 분리**하고, 두 부분은 FreeRTOS 큐 하나로만 연결한다. PC 쪽(팀원 3)은 기능별로 컨테이너를 분리한다. 자세한 내용은 [02-containers.md](02-containers.md).

## 3. 데이터 흐름

```
[OV2640] --카메라 프레임--> [face_sensor]
                              │ 96×96 RGB로 축소 → FOMO 추론
                              │ 프레임마다 face_result_t (≈5~10 FPS 목표)
                              ▼
                  FreeRTOS 큐 (길이 1, xQueueOverwrite = 항상 최신값)
                              │
                              ▼
                       [presence_link]
                              │ 디바운스 상태 머신 (§4)
                              │  ├─ 상태가 바뀔 때만  → facepause/<id>/presence (retained, QoS1)
                              │  ├─ 1초마다          → facepause/<id>/telemetry (QoS0)
                              │  └─ 접속 시 / LWT    → facepause/<id>/status   (retained)
                              ▼   Wi-Fi (집 공유기 2.4GHz)
                  [broker] mosquitto :1883 (TCP) / :9001 (WS)
                              │
                              ▼
                  [브라우저] mqtt.js 구독 (web 컨테이너가 준 페이지)
                              │
             present → player.playVideo()
             absent  → player.pauseVideo()
             offline → player.pauseVideo() + "장치 오프라인" 배너
```

**왜 상태가 바뀔 때만 보내고, retained로 보내나?**
- 웹은 "재생/정지 명령"이 아니라 "현재 상태"를 받는다. 같은 상태를 계속 보내면 사용자가 직접 정지했을 때 웹이 다시 재생시키는 문제가 생긴다.
- retained이므로 웹 페이지를 새로 고침해도 접속 즉시 마지막 상태를 받는다.

## 4. 디바운스 상태 머신 (presence_link, 팀원 2)

```
                     연속 FP_PRESENT_FRAMES 프레임 동안 얼굴 검출
          ┌──────────────────────────────────────────────────────┐
          │                                                      ▼
   ┌─────────────┐                                        ┌─────────────┐
   │   ABSENT    │                                        │   PRESENT   │
   │ (영상 정지) │                                        │ (영상 재생) │
   └─────────────┘                                        └─────────────┘
          ▲                                                      │
          └──────────────────────────────────────────────────────┘
                마지막 검출 후 FP_ABSENT_MS 동안 얼굴 없음
```

| 파라미터 (`config.h`) | 기본값 | 의미 |
|---|---|---|
| `FP_SCORE_THRESHOLD` | 0.5 | 이 점수 미만의 검출 결과는 얼굴로 치지 않는다 (팀원 1이 적용) |
| `FP_PRESENT_FRAMES` | 2 | 연속 N프레임 검출되면 PRESENT로 전환 (오검출 1프레임에 재생되는 것 방지) |
| `FP_ABSENT_MS` | 1500 | 마지막 검출 후 이 시간 동안 얼굴이 없으면 ABSENT로 전환 (깜빡임 방지) |
| `FP_SENSOR_TIMEOUT_MS` | 3000 | 이 시간 동안 `face_sensor` 결과가 안 오면 ABSENT + `status: error` |
| `FP_TELEMETRY_MS` | 1000 | 텔레메트리 발행 주기 |

- 비대칭인 이유: "재생"은 빨리(사용자가 기다림), "정지"는 조금 늦게(잠깐 고개 숙여 메모해도 안 멈춤) 하는 게 체감이 좋다.
- 부팅 직후 초기 상태는 `ABSENT`.

## 5. 웹 플레이어 동작

화면 설계는 [06-web-ui.md](06-web-ui.md)에 따로 정리했다. 요약:

| 수신 | 플레이어 동작 | 화면 표시 |
|---|---|---|
| `presence = present` | `playVideo()` | 초록 배지 "👀 시청 중" |
| `presence = absent` | `pauseVideo()` | 회색 배지 "⏸ 자리 비움" |
| `status = offline / error` 또는 브로커 연결 끊김 | `pauseVideo()` | 빨간 배너 "⚠ 장치 오프라인" |
| `telemetry` | 변화 없음 | HUD 숫자 갱신 |

## 6. 폴더 구조

```
esp32-face-pause/
├── README.md                      # 시작 가이드
├── docker-compose.yml             # broker, web, sim, fw            (팀원 3, fw 서비스는 팀원 2)
├── .env.example                   # 포트, 브로커 주소, 기본 영상
├── docs/                          # 설계 문서 (이 폴더)
├── shared/schema/                 # 공동 소유: MQTT 페이로드 JSON Schema
├── firmware/                      # Arduino 펌웨어 (팀원 1·2)
│   ├── Dockerfile                 # arduino-cli + ESP32 코어 2.0.17 + PubSubClient
│   ├── build.sh                   # 컨테이너 안에서 컴파일
│   ├── test.sh                    # 디바운스 단위 테스트 (PC에서 g++)
│   ├── libraries/                 # Edge Impulse 내보낸 라이브러리를 여기에 (팀원 1)
│   ├── test/debounce_test.cpp     # 팀원 2
│   └── face_pause/                # 스케치 폴더 (폴더 이름 = .ino 이름)
│       ├── face_pause.ino         # setup(): 큐 만들고 두 부분 시작 (팀원 2)
│       ├── config.example.h       # → config.h 로 복사 (Wi-Fi 비밀번호, git에 안 올라감)
│       ├── face_result.h          # ★ 계약: 팀원 1 ↔ 2 쪽지 양식 (공동 소유)
│       ├── face_sensor.h/.cpp     # 👤 팀원 1
│       ├── camera_pins.h          # 👤 팀원 1
│       ├── face_sensor_stub.cpp   # 👤 팀원 2: BOOT 버튼 = 얼굴 (모델 없이 테스트)
│       ├── presence_link.h/.cpp   # 👤 팀원 2
│       └── debounce.h/.cpp        # 👤 팀원 2 (Arduino 의존 없음 → PC에서 테스트)
└── services/                      # 👤 팀원 3
    ├── broker/mosquitto.conf
    ├── web/   (Dockerfile, 40-fp-config.sh, public/index.html·style.css·player.js)
    └── sim/   (Dockerfile, requirements.txt, fake_esp32.py)
```

## 7. 하드웨어 메모 (XIAO ESP32S3 Sense)

| 항목 | 값 / 주의 |
|---|---|
| 칩 | ESP32-S3, 8MB Octal PSRAM, 8MB Flash → Arduino 보드 설정에서 **PSRAM: "OPI PSRAM"** 필수 |
| 카메라 | OV2640 (최근 출하분은 OV3660일 수 있음 → esp32-camera가 자동 인식) |
| 카메라 핀 | PWDN -1, RESET -1, XCLK 10, SIOD 40, SIOC 39, Y9 48, Y8 11, Y7 12, Y6 14, Y5 16, Y4 18, Y3 17, Y2 15, VSYNC 38, HREF 47, PCLK 13 |
| 사용자 LED | GPIO 21 (**active-low**: LOW = 켜짐) → PRESENT일 때 켬 |
| BOOT 버튼 | GPIO 0 (누르면 LOW) → 팀원 2의 stub에서 "얼굴 있음" 대용 |
| Wi-Fi | **2.4GHz만** 지원. 노트북과 같은 공유기에 연결 |
| 발열 | 카메라 + 연속 추론 시 꽤 뜨거워진다 |

카메라 핀은 Seeed Studio 공식 위키(XIAO ESP32S3 Sense camera usage)와 한 번 더 대조한다.

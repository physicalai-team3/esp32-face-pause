# esp32-face-pause

**얼굴이 보이면 유튜브가 재생되고, 얼굴이 사라지면 멈춘다.**

XIAO ESP32S3 Sense가 카메라로 얼굴을 검출(Edge Impulse FOMO)하고, 그 결과를 MQTT로 보낸다. 웹 페이지의 유튜브 플레이어는 결과에 따라 재생/일시정지한다. 카메라 영상은 보드 밖으로 나가지 않는다.

```
[XIAO ESP32S3 Sense] ──Wi-Fi, MQTT──▶ [broker] ──WebSocket──▶ [브라우저: 유튜브 플레이어]
  얼굴 검출 + 디바운스                  (Docker)                present ▶ / absent ⏸
```

설계 문서: [docs/](docs/README.md) — 처음이라면 [01 아키텍처](docs/01-architecture.md) → [03 메시지 계약](docs/03-messages.md) → [04 분업 계획](docs/04-team-plan.md) 순서로 읽는다.

## Docker 컨테이너 구성

```
╔═══════════════════════════════ 노트북 (docker compose) ════════════════════════════════╗
║                                                                                         ║
║   [기본]  docker compose up -d                                                          ║
║   ┌──────────────────────────────┐                ┌──────────────────────────────┐      ║
║   │ broker  (fp-broker)          │                │ web  (fp-web)                │      ║
║   │ eclipse-mosquitto:2          │                │ nginx + 플레이어 페이지        │      ║
║   │ :1883  MQTT (ESP32, sim)     │                │ :8080                        │      ║
║   │ :9001  WebSocket (브라우저)   │                │ 시작 시 .env → config.js     │      ║
║   └──────▲────────────▲──────────┘                └──────────────┬───────────────┘      ║
║          │            │ 👤 팀원 3                        👤 팀원 3 │                      ║
║          │            │                                         │                      ║
║   [--profile sim]     │                                         │                      ║
║   ┌───────────────────┴──────────┐                              │                      ║
║   │ sim  (fp-sim)                │                              │                      ║
║   │ 가짜 ESP32 (python+paho)      │                              │                      ║
║   │ DEVICE_ID=sim-01             │                              │                      ║
║   └──────────────────────────────┘                              │                      ║
║          │ 👤 팀원 3                                             │                      ║
║          │                                                      │                      ║
║   [--profile firmware]  docker compose run --rm fw             │                      ║
║   ┌──────────────────────────────┐                              │                      ║
║   │ fw  (1회용 도구)              │  ./firmware 폴더를 마운트       │                      ║
║   │ arduino-cli + esp32 2.0.17   │  → 컴파일 확인 / 단위 테스트    │                      ║
║   └──────────────────────────────┘                              │                      ║
║          👤 팀원 1·2                                             │                      ║
╚══════════╪══════════════════════════════════════════════════════╪══════════════════════╝
           │ Wi-Fi, MQTT :1883                                     │ http://localhost:8080
           │                                                       ▼
┌──────────┴─── XIAO ESP32S3 Sense (컨테이너 아님) ───┐    ┌────────── 브라우저 ──────────┐
│ face_sensor ──큐──▶ presence_link                  │    │ 유튜브 플레이어 + mqtt.js       │
│  👤 팀원 1           👤 팀원 2                       │    │ ws://…:9001 로 broker 직접 구독 │
│ (Arduino IDE 로 USB 업로드)                          │    └──────────────────────────────┘
└────────────────────────────────────────────────────┘
```

| 컨테이너 | 언제 켜나 | 하는 일 | 주인 |
|---|---|---|---|
| `broker` | 항상 | MQTT 우체국. ESP32·sim은 1883, 브라우저는 9001(WebSocket) | 팀원 3 |
| `web` | 항상 | 플레이어 페이지 파일을 브라우저에 전달 (재생·구독은 브라우저가 직접) | 팀원 3 |
| `sim` | 보드 없이 테스트할 때 | 진짜 보드와 같은 형식으로 present/absent/offline 발행 | 팀원 3 |
| `fw` | 컴파일·테스트할 때만 잠깐 | 팀 전원이 같은 버전으로 컴파일되는지 확인 | 팀원 1·2 |

- ESP32 보드는 컨테이너가 아니다. 보드 안의 두 사람은 **파일**(`face_sensor.*` / `presence_link.*`)로 나누고 큐 하나로 연결한다.
- 컨테이너끼리, 그리고 보드·브라우저와는 **MQTT 토픽**([03-messages.md](docs/03-messages.md))으로만 대화한다.

## 역할

| | 담당 | 작업 폴더 | 시작 문서 |
|---|---|---|---|
| 👤 팀원 1 — 눈 | 카메라 + 얼굴 검출 모델 | `firmware/face_pause/face_sensor.*`, `firmware/libraries/` | [05-face-model.md](docs/05-face-model.md) |
| 👤 팀원 2 — 신경 | Wi-Fi + MQTT + 디바운스 | `firmware/face_pause/presence_link.*`, `debounce.*` | [03-messages.md](docs/03-messages.md) |
| 👤 팀원 3 — 화면 | 브로커 + 웹 플레이어 + 가짜 ESP32 | `services/`, `docker-compose.yml` | [06-web-ui.md](docs/06-web-ui.md) |

각자 할 일은 코드 안의 `TODO(팀원N)` 주석으로 표시되어 있다.

## 빠른 시작

### 0. 공통 준비
- Docker Desktop 실행
- `cp .env.example .env`

### 팀원 3 — 보드 없이 웹 띄우기
```bash
docker compose up -d --build                     # broker + web
docker compose --profile sim up -d --build sim   # 가짜 ESP32 (sim-01)
```
브라우저에서 http://localhost:8080 → 장치 `sim-01` 선택 → **시작하기**.

### 팀원 1·2 — 펌웨어
1. `firmware/face_pause/config.example.h`를 `config.h`로 복사하고 Wi-Fi(2.4GHz)와 노트북 IP를 입력한다.
2. 컴파일 확인 (컨테이너):
   ```bash
   docker compose --profile firmware build fw   # 최초 1회, 수 분 걸림
   docker compose run --rm fw                   # 컴파일
   docker compose run --rm fw sh test.sh        # 디바운스 단위 테스트
   ```
3. 업로드 (노트북의 Arduino IDE 2): 스케치북 위치 `firmware/`, 보드 **XIAO_ESP32S3**, esp32 코어 **2.0.17**, **PSRAM: OPI PSRAM**, 라이브러리 **PubSubClient 2.8**. 자세한 순서는 [02-containers.md §3](docs/02-containers.md#3-실행-방법).
4. `FP_SENSOR_STUB 1`(기본값)이면 모델 없이 **BOOT 버튼을 누르고 있는 동안 "얼굴 있음"**으로 동작한다.

### 메시지 확인
```bash
docker exec -it fp-broker mosquitto_sub -v -t 'facepause/#'
```

## ⚠ 첫 30분에 확인할 것 (아직 검증 안 된 부분)

뼈대를 만들 때 Docker·컴파일러가 없는 환경이라 **실제로 돌려 보지 못한 부분**이 있다. 각자 작업을 시작하기 전에 확인하고, 문제가 있으면 바로 팀에 공유한다.

### 👤 팀원 1
- [ ] **Edge Impulse 헤더 이름**: `face_sensor.cpp`의 `#include <Face_detection_-_FOMO_inferencing.h>`는 추정값이다. 내보낸 라이브러리의 `src/*_inferencing.h` 이름으로 바꾼다.
- [ ] **카메라 핀**: `camera_pins.h`를 Seeed 공식 위키(XIAO ESP32S3 Sense camera)와 대조한다.
- [ ] **코어 2.0.17 + FOMO 라이브러리 호환**: 예제 `esp32_camera`가 2.0.17에서 컴파일·실행되는지 확인한다. 크래시가 나면 [05-face-model.md §3](docs/05-face-model.md)의 대처(ESP-NN 끄기)를 시도한다.
- [ ] **`FP_SENSOR_STUB 0` 컴파일**: 라이브러리를 넣은 뒤 `docker compose run --rm fw`가 통과하는지 확인한다. 이 경로는 한 번도 컴파일해 보지 않았다.

### 👤 팀원 2
- [ ] **`fw` 컨테이너 빌드**: `docker compose --profile firmware build fw`가 성공하는지 확인한다. arduino-cli 설치, esp32 코어 2.0.17 설치, PubSubClient 설치를 한 번도 돌려 보지 않았다.
- [ ] **보드 옵션 이름**: `build.sh`의 FQBN `esp32:esp32:XIAO_ESP32S3:PSRAM=opi`가 맞는지 확인한다. 틀리면 `arduino-cli board details -b esp32:esp32:XIAO_ESP32S3`로 정확한 옵션 이름을 찾는다.
- [ ] **stub 모드 컴파일**: `docker compose run --rm fw`가 기본 설정(`FP_SENSOR_STUB 1`)으로 통과하는지 확인한다.
- [ ] **단위 테스트 실행**: `docker compose run --rm fw sh test.sh`. 처음에는 `debounce.cpp`가 TODO라서 **실패하는 것이 정상**이다. 구현 후 11개 전부 PASS가 목표다. 테스트의 기대값은 참고 구현으로 검증해 두었다.

### 👤 팀원 3
- [ ] **broker·web 기동**: `docker compose up -d --build` 후 `docker compose ps`에서 broker가 `healthy`인지 확인한다.
- [ ] **config.js 생성**: `docker compose logs web`에 `[fp-config] config.js: mqttUrl=...` 줄이 찍히는지 확인한다. 브라우저에서 http://localhost:8080/config.js 를 열어 봐도 된다.
- [ ] **페이지 로드**: http://localhost:8080 에서 브라우저 개발자 도구(F12) 콘솔에 오류가 없는지 확인한다. `player.js`는 문법 검사를 하지 못했다.
- [ ] **sim 기동**: `docker compose --profile sim up -d --build sim` 후 `docker compose logs sim`에 `[sim] connected` 줄이 찍히는지 확인한다.

### 👥 통합 때 (2:00)
- [ ] ESP32에서 노트북 브로커로 접속되는지 확인한다. 노트북 IP, 2.4GHz Wi-Fi, 노트북 네트워크가 "개인 네트워크"인지가 관건이다 ([02-containers.md §4](docs/02-containers.md#4-네트워크-체크리스트-집-공유기-기준)).
- [ ] PubSubClient의 LWT(offline)가 실제로 나가는지 확인한다. 보드 USB를 뽑고 약 20초 안에 `status=offline`이 와야 한다.

## 폴더 구조

```
docs/              설계 문서
shared/schema/     MQTT 메시지 형식 (JSON Schema) — 공동 소유
firmware/          Arduino 펌웨어 (팀원 1·2)
  face_pause/        스케치: face_result.h(계약), face_sensor.*, presence_link.*, debounce.*
  libraries/         Edge Impulse 라이브러리
  test/              디바운스 단위 테스트
services/          Docker 서비스 (팀원 3)
  broker/            mosquitto 설정
  web/               유튜브 플레이어 페이지
  sim/               가짜 ESP32
```

## 협업 규칙 (요약)

- 브랜치: `feat/eye`(팀원 1), `feat/link`(팀원 2), `feat/screen`(팀원 3). 통합 시점에만 `main`으로 merge.
- **자기 파일만 수정한다.** 계약 파일(`docs/03-messages.md`, `shared/schema/`, `firmware/face_pause/*.h`) 수정은 `[contract]` 커밋 + 팀 공유.
- `config.h`와 `.env`에는 비밀번호가 들어가므로 커밋하지 않는다 (`.gitignore`에 있음).

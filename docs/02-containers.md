# 02. 컨테이너 설계

## 1. 분리 원칙

> **"한 기능 = 한 컨테이너 = 한 주인"**, 컨테이너끼리는 **MQTT 토픽**으로만 대화한다.

ESP32 위의 코드는 컨테이너로 나눌 수 없으므로, 펌웨어 안에서는 **파일(`face_sensor.*` / `presence_link.*`)**이 컨테이너 역할을 한다.

| 경계 | 분리 단위 | 연결 방식 | 계약 위치 |
|---|---|---|---|
| ESP32 ↔ PC | 장치 ↔ Docker 네트워크 | MQTT (TCP 1883) | [03-messages.md §2~4](03-messages.md) |
| 브라우저 ↔ PC | 브라우저 ↔ `broker` | MQTT over WebSocket (9001) | [03-messages.md §2~4](03-messages.md) |
| 팀원 1 ↔ 팀원 2 | `face_sensor.*` ↔ `presence_link.*` | FreeRTOS 큐 (`face_result_t`) | [03-messages.md §5](03-messages.md) |

```
╔══════════════════════════ 노트북 (Docker) ═══════════════════════════╗
║   ┌─────────────┐        ┌──────────────────┐        ┌─────────────┐  ║
║   │    sim      │──MQTT─▶│      broker      │       │     web     │  ║
║   │ 가짜 ESP32   │  1883  │    mosquitto     │       │   nginx     │  ║
║   └─────────────┘        │  1883  │  9001   │       │ 페이지 파일  │  ║
║      👤 팀원 3           └───▲────┴────▲────┘       └──────┬──────┘  ║
║                              │         │  👤 팀원 3         │         ║
║   ┌─────────────┐            │         │                    │         ║
║   │     fw      │ 컴파일 확인 │         │                    │         ║
║   │ arduino-cli │ (도구,1회용)│         │                    │         ║
║   └─────────────┘            │         │                    │         ║
║    👤 팀원 1·2               │         │                    │         ║
╚══════════════════════════════╪═════════╪════════════════════╪═════════╝
       Arduino IDE로 USB 업로드  │ Wi-Fi   │ WebSocket          │ http://localhost:8080
                    ▼          │ 1883    │ 9001               ▼
      ┌──────── ESP32 보드 (Docker 아님) ─┐        ┌──────── 브라우저 ────────┐
      │ face_sensor ──큐──▶ presence_link ─┘        │  유튜브 플레이어 + mqtt.js │
      │  👤 팀원 1           👤 팀원 2       │        └──────────────────────────┘
      └──────────────────────────────────┘
```

이렇게 하면 **누구의 코드든 같은 계약을 지키는 가짜(mock)로 바꿔 끼울 수 있다.**

| 진짜 | 대신 쓸 수 있는 가짜 | 누가 쓰나 |
|---|---|---|
| ESP32 전체 | `sim` 컨테이너 | 팀원 3 |
| `face_sensor` | `face_sensor_stub.cpp` (`FP_SENSOR_STUB 1`, BOOT 버튼 = 얼굴) | 팀원 2 |
| `presence_link` | 시리얼 모니터 로그 + LED | 팀원 1 |
| `web` | `mosquitto_sub -v -t 'facepause/#'` | 팀원 1·2 |

## 2. 컨테이너 목록

| 서비스 | 이미지 | 포트 (호스트) | compose profile | 주인 | 하는 일 |
|---|---|---|---|---|---|
| `broker` | `eclipse-mosquitto:2` | 1883 (MQTT), 9001 (WebSocket) | 기본 | 팀원 3 | 메시지 중계 |
| `web` | `./services/web` (nginx:alpine) | 8080 | 기본 | 팀원 3 | 플레이어 페이지 서빙 |
| `sim` | `./services/sim` (python:3.12-slim) | 없음 | `sim` | 팀원 3 | 가짜 ESP32 |
| `fw` | `./firmware` (arduino-cli + esp32 코어 2.0.17) | 없음 | `firmware` | 팀원 1·2 | 컴파일 확인, 단위 테스트 |

- `fw`는 **도구 컨테이너**다. `docker compose run --rm fw`로 한 번 실행하고 끝난다.
- `fw`는 "모두 같은 버전으로 컴파일되는지" 확인하는 용도다. **보드에 올리는 것(업로드)은 각자 노트북의 Arduino IDE**에서 한다. Windows의 Docker는 USB(COM 포트)를 컨테이너에 넘기기 어렵기 때문이다.
- `sim`의 기본 `DEVICE_ID`는 `sim-01`이라 실제 보드(`xiao-01`)와 겹치지 않는다. 웹에서 장치를 골라 본다.

## 3. 실행 방법

### PC 쪽 (팀원 3, 데모)
```bash
cp .env.example .env
docker compose up -d --build                  # broker + web
# 브라우저: http://localhost:8080
docker compose --profile sim up -d --build sim   # 보드 없이 테스트할 때만
docker compose logs -f sim
```

### 펌웨어 컴파일 확인 (팀원 1·2)
```bash
docker compose --profile firmware build fw    # 최초 1회 (ESP32 코어 다운로드로 수 분 걸림)
docker compose run --rm fw                    # = sh build.sh  → firmware/build/ 에 결과물
docker compose run --rm fw sh test.sh         # 디바운스 단위 테스트
```

### 보드에 업로드 (팀원 1·2, 노트북의 Arduino IDE 2)
1. **파일 → 기본 설정**
   - 추가 보드 관리자 URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - **스케치북 위치: `<저장소 경로>\firmware`** ← 이렇게 하면 `firmware/libraries/`(Edge Impulse 라이브러리)를 IDE가 자동으로 찾는다
2. 보드 관리자에서 **esp32 by Espressif Systems `2.0.17`** 설치 (3.x 아님)
3. 라이브러리 관리자에서 **PubSubClient `2.8`** 설치
4. `firmware/face_pause/config.example.h`를 `config.h`로 복사하고 Wi-Fi·브로커 주소 입력
5. `firmware/face_pause/face_pause.ino` 열기 → 도구 메뉴:
   - 보드: **XIAO_ESP32S3**
   - PSRAM: **OPI PSRAM**
   - 포트: 장치 관리자에 보이는 COM 번호
6. 업로드 → 시리얼 모니터 115200
- 업로드가 안 되면 **BOOT 버튼을 누른 채 RESET** 후 다시 업로드.

### 메시지 엿보기 (모두)
```bash
docker exec -it fp-broker mosquitto_sub -v -t 'facepause/#'
docker exec -it fp-broker mosquitto_pub -t 'facepause/sim-01/presence' -r -m '{"v":1,"device":"sim-01","state":"present","faces":1,"score":0.9,"seq":1,"uptime_ms":0}'
```

## 4. 네트워크 체크리스트 (집 공유기 기준)

| 문제 | 확인 |
|---|---|
| ESP32가 Wi-Fi에 못 붙음 | `config.h`의 SSID가 **2.4GHz** 쪽인가? (`_5G`가 붙은 이름이면 안 됨) |
| ESP32가 브로커에 못 붙음 | `FP_MQTT_HOST`가 `localhost`가 아니라 **노트북 IP**(`ipconfig`의 IPv4)인가? 노트북 재부팅 후 IP가 바뀌지 않았나? |
| | 노트북 Wi-Fi가 **개인 네트워크**인가? (공용이면 방화벽이 1883을 막음) |
| | Docker Desktop 첫 실행 때 방화벽 "허용"을 눌렀나? |
| 브라우저가 브로커에 못 붙음 | `.env`의 `MQTT_WS_URL`이 `ws://<노트북 IP>:9001`인가? (같은 노트북에서 열 때만 `localhost` 가능) |
| 유튜브가 안 나옴 | `http://localhost:8080`으로 열었나? (`file://` 불가) 영상이 외부 재생 허용인가? |
| 발표 장소가 학교 Wi-Fi | 기기 간 통신이 막혀 있을 수 있다 → 휴대폰 핫스팟 사용, `FP_MQTT_HOST`만 새 IP로 |

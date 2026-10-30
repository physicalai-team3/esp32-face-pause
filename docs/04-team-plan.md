# 04. 3인 분업 계획

## 1. 역할 분담

| | 👤 팀원 1 — **눈** (ESP32) | 👤 팀원 2 — **신경** (ESP32) | 👤 팀원 3 — **화면** (ESP32 불필요) |
|---|---|---|---|
| 분리 단위 | 스케치 파일 `face_sensor.*` | 스케치 파일 `presence_link.*`, `debounce.*`, `face_pause.ino` | 컨테이너 `broker`, `web`, `sim` |
| 파일 | `firmware/face_pause/face_sensor.h·.cpp`, `camera_pins.h`, `firmware/libraries/**` | `firmware/face_pause/presence_link.h·.cpp`, `debounce.h·.cpp`, `face_sensor_stub.cpp`, `face_pause.ino`, `config.example.h`, `firmware/test/**`, `firmware/Dockerfile·build.sh·test.sh` | `services/**`, `docker-compose.yml`, `.env.example` |
| 핵심 과제 | Edge Impulse 모델 Clone·배포, 카메라 초기화(XIAO 핀, PSRAM), FOMO 추론 → `face_result_t`, FPS·거리·조명 측정 | Wi-Fi 접속·재접속, MQTT 발행·LWT, 디바운스 상태 머신, 텔레메트리, LED | mosquitto(TCP+WS), 유튜브 링크 입력·플레이어·mqtt.js, 상태 배지·HUD·오프라인 표시, 가짜 ESP32 시뮬레이터 |
| 계약 | [03 §5](03-messages.md) (`face_result_t` **생산**) | [03 §5](03-messages.md) (소비), [03 §2~4](03-messages.md) (MQTT **생산**) | [03 §2~4](03-messages.md) (MQTT **소비** + `sim`으로 생산 흉내) |
| 상대 없이 개발 | Edge Impulse `esp32_camera` 예제 → 우리 `face_sensor.cpp`로 이식. 시리얼 로그 `faces=1 score=0.92 infer=95ms` | `FP_SENSOR_STUB 1`: **BOOT 버튼 누름 = 얼굴** → `mosquitto_sub`로 발행 확인. `debounce`는 `test.sh`로 PC에서 테스트 | `sim` 컨테이너가 present/absent/offline을 시나리오대로 발행 |
| ESP32 보드 | 1대 (주로 사용) | 1대 (1대뿐이면 아래 참고) | 불필요 |

**왜 이렇게 나눴나?**
- 팀원 1은 **"카메라와 모델"**만, 팀원 2는 **"네트워크와 판단"**만 안다. 경계가 `face_result_t` 하나라서 서로의 코드를 읽을 필요가 없다.
- 디바운스를 팀원 2가 맡는 이유: "언제 상태를 바꿀지"는 MQTT로 무엇을 보낼지와 붙어 있는 결정이다. 팀원 1은 프레임마다 정직한 원시 결과만 내보낸다.
- 팀원 3은 하드웨어 없이 `sim`만으로 페이지를 끝까지 완성할 수 있다. 통합 때 장치 선택만 `xiao-01`로 바꾸면 된다.

**보드가 1대뿐이라면**: 오전(0:30–2:30)은 팀원 1이 보드를 쓰고, 팀원 2는 `debounce.cpp` + 단위 테스트, `presence_link.cpp` 작성과 `fw` 컨테이너 컴파일 확인까지 한다. 2:30 이후 팀원 2가 보드를 받아 stub로 MQTT를 붙인다.

## 2. 타임라인 (하루)

| 시간 | 팀원 1 (face_sensor) | 팀원 2 (presence_link) | 팀원 3 (broker/web/sim) | 합류 |
|---|---|---|---|---|
| **0:00–0:30** | 함께: 문서 읽기, [03-messages.md](03-messages.md)·헤더 확인, 브랜치 생성, 노트북 IP 공유 | ← | ← | ✅ **계약 동결** |
| 0:30–1:30 | Edge Impulse 계정, 프로젝트 Clone, Arduino library 배포 → `firmware/libraries/`. Arduino IDE 설정 (코어 2.0.17, OPI PSRAM) | Arduino IDE 설정, `fw` 이미지 빌드, `debounce.cpp` 구현 → `test.sh` 통과 | `broker` + `web` 기동, 링크 입력 → 플레이어 표시, mqtt.js 접속 | |
| 1:30–3:00 | `esp32_camera` 예제를 XIAO 핀으로 돌려 얼굴 검출 확인 → `face_sensor.cpp`로 이식 | Wi-Fi 접속, PubSubClient 접속 + LWT, stub(BOOT 버튼) → presence 발행 | `sim` 시나리오(cycle/flicker/offline), presence → play/pause, 시작 오버레이 | |
| **3:00–3:30** | **1차 통합**: 실제 ESP32(stub 또는 실제 모델) → broker → web 재생/정지 | ← | ← | ✅ **E2E 1차** |
| 3:30–5:00 | `FP_SENSOR_STUB 0`으로 실제 모델 통합, 거리·조명·측면 측정, 임계값 튜닝 | 텔레메트리, 재접속(공유기 끄고 켜기), `FP_ABSENT_MS` 체감 튜닝, LED | HUD, 오프라인 배지, 얼굴 제어 토글, 최근 영상, 장치 선택 | |
| 5:00–6:00 | 팀원 2와 짝으로 실사용 튜닝 (메모하기, 휴대폰 보기 등) | ← | 데모 시나리오 정리, README 실행법 보완 | |
| **6:00–7:00** | **2차 통합 + 데모 리허설** | ← | ← | ✅ **데모** |

## 3. Git 협업 규칙

```
main ──────────●──────────────●──────────●   (0:30 계약, 3:00, 6:00에만 merge)
             ╱ │ ╲          ╱ │ ╲
feat/eye    ●──●  │        ●──●  │          ← 팀원 1 (face_sensor)
feat/link    ●──●─┘         ●──●─┘          ← 팀원 2 (presence_link)
feat/screen   ●──●──●────────●──●──●        ← 팀원 3 (services, compose)
```

- **각자 자기 파일만 수정** (§1 표의 "파일" 행) → merge 충돌이 거의 없다.
- `config.h`(Wi-Fi 비밀번호)와 `.env`는 `.gitignore`에 있다. **절대 커밋하지 않는다.** 설정 항목을 추가할 때는 `config.example.h` / `.env.example`에 추가한다.
- `docs/03-messages.md`, `shared/schema/`, `face_result.h`, 헤더(`*.h`) 수정은 `[contract] ...` 커밋 + 즉시 공유.

## 4. 각자 "완료" 기준

### 팀원 1 — face_sensor
- [ ] Edge Impulse 라이브러리가 `firmware/libraries/`에 커밋됨
- [ ] 부팅 로그에 PSRAM 인식(`psramFound() = 1`), 카메라 초기화 성공
- [ ] 얼굴을 비추면 시리얼에 `faces=1 score>=0.5`, 가리면 `faces=0`
- [ ] 추론 FPS ≥ 5, `infer_ms` 측정값 기록
- [ ] 정면 0.5m / 1m / 1.5m, 측면(고개 90°)에서의 검출 여부 표 작성 ([05 §5](05-face-model.md))
- [ ] 카메라 초기화 실패 시 `FS_CAMERA_INIT`을 반환 (재부팅 루프 금지)

### 팀원 2 — presence_link
- [ ] `docker compose run --rm fw sh test.sh` 전부 통과
- [ ] `mosquitto_sub`로 status(online) → presence(absent) 순서 확인
- [ ] BOOT 버튼(stub)으로 present/absent 전환, 짧게 한 번 누른 건 무시됨
- [ ] 전원을 뽑으면 keepalive 이내에 `status=offline` (LWT)
- [ ] 공유기를 껐다 켜면 자동 재접속 후 presence 재발행

### 팀원 3 — broker / web / sim
- [ ] `docker compose up -d --build` 한 번으로 broker+web 기동, http://localhost:8080 에서 영상 표시
- [ ] 06 문서 §3의 링크 형식 전부 인식
- [ ] `sim` cycle 시나리오로 재생/정지 자동 전환, flicker 시나리오에서도 오류 없음
- [ ] offline 시나리오에서 정지 + 배지, 다시 online이면 복구
- [ ] 새로 고침 직후 retained 메시지로 현재 상태가 바로 반영됨

## 5. 통합 체크리스트 (3:00, 6:00)

1. 노트북 IP 확인(`ipconfig`) → 팀원 2의 `config.h`의 `FP_MQTT_HOST`에 반영
2. `docker compose up -d` (팀원 3), 노트북 Wi-Fi가 "개인 네트워크"인지 확인
3. 펌웨어 업로드 → `mosquitto_sub -v -t 'facepause/#'`로 online·presence 확인
4. 웹에서 장치 `xiao-01` 선택 → 시작 클릭
5. 시나리오 테스트:

| # | 동작 | 기대 결과 | 측정 |
|---|---|---|---|
| T1 | 카메라 정면에 앉는다 | 1초 이내 재생 | 체감 지연 |
| T2 | 자리를 떠난다 | 2초 이내 정지, 재생 위치 유지 | 체감 지연 |
| T3 | 고개를 옆으로 돌린다 | 정지 (측면 얼굴은 검출 안 됨) | 검출 여부 |
| T4 | 0.5초 정도 손으로 얼굴을 가린다 | **계속 재생** (디바운스) | |
| T5 | 두 사람이 함께 본다 | 재생, HUD faces=2 | |
| T6 | ESP32 USB를 뽑는다 | 정지 + "장치 오프라인" | 배지까지 시간 |
| T7 | 웹 새로 고침 | 현재 상태 즉시 반영 | |

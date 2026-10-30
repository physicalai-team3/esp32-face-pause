# 03. 메시지 계약 (공동 소유)

> 이 문서, `shared/schema/*.json`, `firmware/face_pause/face_result.h`는 **3명 공동 소유**다. 수정할 때는 커밋 메시지에 `[contract]`를 붙이고 나머지 두 명에게 바로 알린다.
> 오전 첫 30분 안에 확정(동결)하고, 그 뒤에는 필드 **추가**만 허용한다 (이름 변경·삭제 금지).

## 1. 공통 규칙

- 토픽 접두사: `facepause/<device_id>/…`
- `device_id`: 실제 장치는 `xiao-01`, `xiao-02`…, 가짜 장치는 `sim-01`. 소문자·숫자·하이픈만.
- 페이로드: UTF-8 JSON, 한 줄, 256바이트 미만 (PubSubClient 버퍼 크기 고려).
- 모든 페이로드에 `"v": 1` (스키마 버전). 웹은 모르는 필드는 무시한다.
- ESP32에는 실제 시각이 없으므로 타임스탬프 대신 `uptime_ms`(부팅 후 경과 ms)와 `seq`(증가 번호)를 쓴다.

| 토픽 | 방향 | QoS | retained | 발행 시점 |
|---|---|---|---|---|
| `facepause/<id>/presence` | ESP32 → 웹 | 1 | ✅ | **상태가 바뀔 때만** + MQTT 접속(재접속) 직후 1회 |
| `facepause/<id>/status` | ESP32 → 웹 | 1 | ✅ | 접속 시 `online`, 비정상 종료 시 브로커가 LWT로 `offline` |
| `facepause/<id>/telemetry` | ESP32 → 웹 | 0 | ❌ | `FP_TELEMETRY_MS`(1초)마다 |

> PubSubClient는 QoS 0 발행만 지원한다. 그래서 ESP32는 presence/status를 **QoS 0 + retained**로 보낸다. 같은 공유기 안에서는 유실이 거의 없고, retained라서 웹은 재접속 시 항상 최신 상태를 받는다. (LWT는 QoS 1로 등록 가능)

## 2. presence — 재생/정지의 유일한 근거

```json
{"v":1,"device":"xiao-01","state":"present","faces":1,"score":0.93,"seq":42,"uptime_ms":183402}
```

| 필드 | 타입 | 설명 |
|---|---|---|
| `v` | int | 항상 1 |
| `device` | string | 토픽의 `<id>`와 동일 |
| `state` | `"present"` \| `"absent"` | 디바운스가 끝난 최종 상태. **웹은 이 값만 보고 재생/정지한다** |
| `faces` | int | 전환 시점 프레임의 얼굴 수 (absent면 0) |
| `score` | float 0~1 | 전환 시점 최고 점수 (absent면 0) |
| `seq` | int | presence 메시지마다 1씩 증가 (재부팅하면 0부터) |
| `uptime_ms` | int | 부팅 후 경과 시간 |

## 3. status — 장치 생존 여부

```json
{"v":1,"device":"xiao-01","state":"online","fw":"0.1.0","ip":"192.168.0.23"}
{"v":1,"device":"xiao-01","state":"offline"}
{"v":1,"device":"xiao-01","state":"error","reason":"camera_init"}
```

| `state` | 누가 보내나 | 웹 동작 |
|---|---|---|
| `online` | ESP32, MQTT 접속 직후 | 배너 제거 |
| `offline` | **브로커**(LWT: ESP32가 접속할 때 미리 등록, keepalive 15초) | 일시정지 + "장치 오프라인" |
| `error` | ESP32, 카메라·모델 오류 시 (`reason`: `camera_init`, `model_init`, `sensor_timeout`) | 일시정지 + 오류 표시 |

## 4. telemetry — 디버그/HUD용 (재생 제어에 쓰지 않음)

```json
{"v":1,"device":"xiao-01","faces":1,"score":0.91,"fps":7.8,"infer_ms":95,"rssi":-58,"heap_kb":152,"psram_kb":5830,"uptime_ms":184000}
```

| 필드 | 설명 |
|---|---|
| `faces`, `score` | **디바운스 전** 최근 프레임의 원시 결과 (튜닝용) |
| `fps` | 최근 1초 평균 추론 FPS |
| `infer_ms` | 최근 프레임 추론 시간 |
| `rssi` | Wi-Fi 신호 세기 (dBm) |
| `heap_kb`, `psram_kb` | 남은 내부 RAM / PSRAM |

## 5. 펌웨어 내부 계약 (팀원 1 ↔ 팀원 2)

### 헤더 파일이란?
C/C++에서 **"이 코드는 이렇게 쓰면 된다"는 약속(메뉴판)만 적은 파일**(`.h`)이다. 실제 동작 코드는 `.cpp`(주방)에 있다. 아래 세 헤더만 지키면 팀원 1·2는 서로의 `.cpp`를 몰라도 된다.

### `face_result.h` — 쪽지 양식 (공동 소유)
```cpp
typedef struct {
    bool     detected;     // score >= FP_SCORE_THRESHOLD 인 얼굴이 1개 이상
    uint8_t  count;        // 검출된 얼굴 수 (FOMO 중심점 개수)
    float    best_score;   // 가장 높은 점수 (없으면 0)
    uint16_t infer_ms;     // 이번 프레임 추론 시간
    uint32_t frame_id;     // 1씩 증가
    uint32_t ts_ms;        // millis()
} face_result_t;
```

### `face_sensor.h` — 팀원 1이 제공
```cpp
enum FaceSensorStatus { FS_OK = 0, FS_CAMERA_INIT, FS_MODEL_INIT };

// 카메라·모델을 초기화하고 검출 태스크를 시작한다.
// out_queue: 길이 1, 항목 크기 sizeof(face_result_t). 태스크는 xQueueOverwrite()로 항상 최신 결과만 남긴다.
FaceSensorStatus face_sensor_start(QueueHandle_t out_queue);
const char *face_sensor_status_str(FaceSensorStatus s);   // "ok", "camera_init", "model_init"
```

### `presence_link.h` — 팀원 2가 제공
```cpp
// Wi-Fi·MQTT 접속, 디바운스, 발행을 담당하는 태스크를 시작한다. in_queue에서 face_result_t를 읽는다.
bool presence_link_start(QueueHandle_t in_queue);
// face_sensor 시작 실패 시 setup()이 호출 → status: error 발행
void presence_link_report_sensor_error(const char *reason);
```

### `face_pause.ino` — 두 부분을 연결하는 유일한 지점
```cpp
void setup() {
    Serial.begin(115200);
    g_face_queue = xQueueCreate(1, sizeof(face_result_t));
    presence_link_start(g_face_queue);                 // 먼저: Wi-Fi 접속 시작, LED
    FaceSensorStatus st = face_sensor_start(g_face_queue);
    if (st != FS_OK) presence_link_report_sensor_error(face_sensor_status_str(st));
}
```

- `face_sensor`는 Wi-Fi·MQTT를 **전혀 모른다.** `presence_link`는 카메라·모델을 **전혀 모른다.**
- `config.h`의 `FP_SENSOR_STUB 1`이면 `face_sensor.cpp` 대신 `face_sensor_stub.cpp`(BOOT 버튼 = 얼굴)가 컴파일된다 → 팀원 2는 모델 없이 개발 가능.

## 6. 예시 시퀀스

```
시간    ESP32 내부(raw)       presence_link 상태     발행
0.0s    부팅                   ABSENT                status=online, presence=absent(seq 0)
2.0s    detected (1/2)         ABSENT                -
2.15s   detected (2/2)         → PRESENT             presence=present(seq 1)   → 웹: play
5.0s    not detected           PRESENT (타이머 시작)   -
5.3s    detected               PRESENT (타이머 리셋)   -                         ← 깜빡임 흡수
8.0s    not detected …         PRESENT               -
9.5s    (1.5s 경과)            → ABSENT              presence=absent(seq 2)    → 웹: pause
12.0s   전원 뽑힘              -                     (약 15~22s 후) 브로커가 status=offline → 웹: 배너
```

// 이 파일을 같은 폴더에 config.h 로 복사해서 값을 채운다. (config.h 는 git에 올라가지 않음)
// 새 설정 항목을 추가할 때는 이 example 파일에도 추가해서 커밋한다.
#pragma once

// ── 네트워크 (팀원 2) ─────────────────────────────────────
#define FP_WIFI_SSID         "your-2.4GHz-ssid"   // _5G 가 붙은 이름 X
#define FP_WIFI_PASS         "your-password"
#define FP_MQTT_HOST         "192.168.0.10"       // 노트북 IP (ipconfig 의 IPv4). localhost X
#define FP_MQTT_PORT         1883
#define FP_MQTT_KEEPALIVE_S  15                   // 끊긴 뒤 약 1.5배 후 브로커가 offline(LWT) 발행
#define FP_DEVICE_ID         "xiao-01"            // 토픽 facepause/<id>/...  소문자·숫자·하이픈
#define FP_FW_VERSION        "0.1.0"

// ── 얼굴 센서 (팀원 1) ────────────────────────────────────
// 1: face_sensor_stub.cpp 사용 (BOOT 버튼을 누르고 있으면 얼굴 있음, 모델/카메라 불필요)
// 0: face_sensor.cpp 사용 (Edge Impulse 라이브러리가 firmware/libraries/ 에 있어야 함)
#define FP_SENSOR_STUB       1
#define FP_SCORE_THRESHOLD   0.5f                 // 이 점수 미만은 얼굴로 치지 않음
#define FP_SENSOR_LOG        1                    // 1: 프레임마다 시리얼에 결과 출력

// ── 디바운스 / 발행 (팀원 2) ──────────────────────────────
#define FP_PRESENT_FRAMES    2                    // 연속 N프레임 검출 → present
#define FP_ABSENT_MS         1500                 // 마지막 검출 후 N ms → absent
#define FP_SENSOR_TIMEOUT_MS 3000                 // 센서 결과가 N ms 안 오면 absent + status error
#define FP_TELEMETRY_MS      1000                 // 텔레메트리 주기

// ── 보드 핀 (XIAO ESP32S3 Sense) ─────────────────────────
#define FP_LED_PIN           21                   // 사용자 LED, active-low (LOW = 켜짐)
#define FP_BOOT_PIN          0                    // BOOT 버튼 (누르면 LOW)

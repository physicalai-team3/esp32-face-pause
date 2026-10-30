# 05. 얼굴 검출 모델 (팀원 1)

## 1. 선정 모델: Edge Impulse "Face detection - FOMO"

| 항목 | 내용 |
|---|---|
| 프로젝트 | Demo Team, **Project ID 87291** — https://studio.edgeimpulse.com/public/87291/latest |
| 방식 | **FOMO** (Faster Objects, More Objects), MobileNetV2 0.35 기반 전이 학습 |
| 입력 | **96×96 RGB** |
| 라벨 | `face` 하나 |
| 출력 | 상자가 아니라 **얼굴 중심점** 목록 (각 중심점에 `value` = 점수) |
| 정확도 | 검증 90.2%, 테스트 80.8% (float32 기준. int8로 양자화하면 다소 낮아질 수 있음) |
| 데이터 | FFHQ 일부 413장 (Edge Impulse 팀이 직접 라벨링) |
| 라이선스 | BSD 3-Clause Clear |

**우리 용도에 맞는 이유**
- 필요한 건 "얼굴이 있다/없다"뿐이다. FOMO는 중심점만 주지만 **중심점 개수 = 얼굴 수**, **최대 점수 = 확신도**로 바로 쓸 수 있다.
- 96×96 입력이라 가볍다. ESP32-S3에서 실시간에 가깝게 돌 것으로 기대한다 (실측 필요).
- 정면 얼굴 위주로 학습되어 고개를 옆으로 돌리면 놓칠 가능성이 높다 → "시선을 떼면 정지" 기획과 맞음 (T3 시나리오로 확인).

## 2. 라이브러리 받기 (팀원 1, 오전 첫 작업)

공개 프로젝트는 대상 장치가 Alif 보드로 잡혀 있어서 바로 ESP32용으로 받을 수 없다. **내 계정으로 복제(Clone)** 해서 배포한다.

1. https://studio.edgeimpulse.com 에서 무료 계정 생성/로그인
2. 프로젝트 페이지 우측 상단 **Clone this project** → 내 프로젝트로 복제 (학습까지 수 분)
3. 복제한 프로젝트 → **Deployment**
   - 배포 형태: **Arduino library**
   - 최적화: **Quantized (int8)**, **EON Compiler** 켜기
   - Build → `.zip` 다운로드
4. zip을 **`firmware/libraries/`에 압축 해제** (예: `firmware/libraries/Face_detection_-_FOMO_inferencing/`)
5. 헤더 이름 확인: 라이브러리 폴더 `src/` 안의 `*_inferencing.h` → `face_sensor.cpp`의 `#include`를 그 이름으로 맞춘다
6. 라이브러리 폴더를 **커밋** (팀원 2가 진짜 모델로 통합 빌드할 때 필요)

> 다른 팀원이 직접 Clone할 필요는 없다. 팀원 1이 커밋한 라이브러리를 모두 같이 쓴다.

## 3. 처음 동작 확인: 예제부터

받은 라이브러리에는 **`esp32/esp32_camera` 예제**가 들어 있다. 우리 코드에 옮기기 전에 이 예제로 먼저 모델이 도는지 본다.

1. Arduino IDE → 파일 → 예제 → (라이브러리 이름) → `esp32` → `esp32_camera`
2. 예제는 ESP-EYE/AI-Thinker 보드용 핀으로 되어 있다 → **XIAO 핀으로 교체** (`firmware/face_pause/camera_pins.h` 내용 참고, PWDN·RESET = -1)
3. 보드 XIAO_ESP32S3, **PSRAM: OPI PSRAM**, 업로드 → 시리얼에 `face (0.9x) [x: .., y: ..]`가 나오면 성공

**알려진 문제와 대처**
| 증상 | 대처 |
|---|---|
| `Guru Meditation Error` / 메모리 부족으로 재부팅 | PSRAM이 OPI로 켜져 있는지 확인. 카메라 `fb_location = CAMERA_FB_IN_PSRAM` |
| 컴파일 실패 / 이상한 크래시 | 라이브러리 `src/edge-impulse-sdk/classifier/ei_classifier_config.h`에서 `EI_CLASSIFIER_TFLITE_ENABLE_ESP_NN`을 `0`으로 (가속을 끄면 느려지지만 안정적) |
| ESP32 코어 3.x에서 안 됨 | 보드 관리자에서 **2.0.17**로 내린다 (팀 전원 같은 버전) |

## 4. 우리 코드로 옮기기 (`face_sensor.cpp`)

예제의 핵심 흐름을 FreeRTOS 태스크로 옮기고, 결과를 `face_result_t`로 바꿔 큐에 넣는다.

```cpp
// 1) 카메라 초기화 (setup에서 1회)
//    pixel_format = PIXFORMAT_JPEG, frame_size = FRAMESIZE_QVGA (예제 방식: JPEG → RGB888 변환 → 96x96 리사이즈)
//    fb_location = CAMERA_FB_IN_PSRAM, grab_mode = CAMERA_GRAB_LATEST

// 2) 태스크 루프
while (true) {
    // 예제의 ei_camera_capture(EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, snapshot_buf) 사용
    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &ei_camera_get_data;

    ei_impulse_result_t result = {0};
    uint32_t t0 = millis();
    if (run_classifier(&signal, &result, false) != EI_IMPULSE_OK) continue;

    face_result_t r = {};
    for (size_t i = 0; i < result.bounding_boxes_count; i++) {       // FOMO: 중심점 목록
        const auto &bb = result.bounding_boxes[i];
        if (bb.value < FP_SCORE_THRESHOLD) continue;
        r.count++;
        if (bb.value > r.best_score) r.best_score = bb.value;
    }
    r.detected = r.count > 0;
    r.infer_ms = millis() - t0;          // 또는 result.timing.classification
    r.frame_id = ++frame_id;
    r.ts_ms    = millis();
    xQueueOverwrite(out_queue, &r);
}
```
> 위 코드는 설계용이다. 실제로는 **받은 라이브러리 버전의 `esp32_camera` 예제 코드를 기준으로** 맞춘다.

## 5. 측정 계획

| 측정 | 방법 | 목표 |
|---|---|---|
| 추론 시간 | `infer_ms` 평균/최대 (시리얼, 텔레메트리) | FPS ≥ 5 |
| 검출 거리 | 정면 0.5 / 1.0 / 1.5m에서 10초씩, 검출 프레임 비율 | 1m에서 ≥ 90% |
| 측면 | 고개 30° / 60° / 90° | 90°에서 검출 안 됨 (= 정지) |
| 조명 | 형광등 / 역광(창문 등지기) / 어두운 방 | 데모 장소 조건에서 동작 |
| 오검출 | 사람 없이 1분 (포스터·모니터 속 얼굴 주의) | present 전환 0회 |

96×96 입력이라 **멀리 있는 얼굴에 약할 수 있다.** 1.5m에서 놓치면 카메라 앞 1m 이내에서 쓰는 것으로 데모 조건을 정한다.

측정 결과는 이 문서 아래에 표로 추가한다.

## 6. 막혔을 때

| 상황 | 대안 |
|---|---|
| 점심 전까지 모델이 보드에서 안 돎 | 팀원 2·3은 영향 없음 (stub/sim으로 계속 진행). 팀원 1은 ESP-NN 끄기 → 그래도 안 되면 arduino-esp32 2.0.17의 `CameraWebServer` 예제에 내장된 얼굴 검출(`human_face_detect_msr01/mnp01`)로 교체. `face_result_t`만 같으면 나머지는 그대로다 |
| 정확도 부족 | `FP_SCORE_THRESHOLD` 조정, `FP_PRESENT_FRAMES`·`FP_ABSENT_MS`로 보정 (팀원 2) |

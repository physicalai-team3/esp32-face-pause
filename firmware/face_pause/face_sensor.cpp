// 팀원 1: 카메라 + Edge Impulse FOMO 얼굴 검출
// config.h 에서 FP_SENSOR_STUB 0 일 때만 컴파일된다. (1이면 face_sensor_stub.cpp 가 대신 쓰임)
// 참고: docs/05-face-model.md, Edge Impulse 라이브러리의 examples/esp32/esp32_camera
#include "config.h"

#if !FP_SENSOR_STUB

// 내보낸 라이브러리의 헤더 이름과 같아야 한다:
//   firmware/libraries/<라이브러리 폴더>/src/*_inferencing.h
// 이름이 다르면 컴파일 에러 "No such file or directory" → 이 줄만 고친다.
#include <Face_detection_-_FOMO_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"

#include "face_sensor.h"
#include "camera_pins.h"
#include "esp_camera.h"
#include "img_converters.h"

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "카메라 모델 라이브러리가 아니다. Edge Impulse에서 Face detection - FOMO 프로젝트로 다시 내보낼 것"
#endif
#if !EI_CLASSIFIER_OBJECT_DETECTION
#error "물체 검출(FOMO) 모델이 아니다. bounding_boxes 를 쓸 수 없음"
#endif

// 카메라 원본 크기 (FRAMESIZE_QQVGA 와 같아야 함). 모델은 어차피 96x96 이라 QVGA(320x240) 대신 써서 JPEG 디코딩 시간을 줄인다
// JPEG → RGB888 변환을 이 크기 그대로 버퍼에 한 뒤, 같은 버퍼 안에서 96x96 으로 줄인다.
#define EI_CAMERA_RAW_FRAME_BUFFER_COLS  160
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS  120
#define EI_CAMERA_FRAME_BYTE_SIZE        3

static QueueHandle_t s_out_queue;
static uint8_t *s_snapshot_buf;   // 160x120x3 = 56KB, PSRAM. 카메라 이미지는 이 버퍼 밖으로 나가지 않는다 (N2)

static bool camera_init()
{
    camera_config_t config = {};
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.ledc_timer = LEDC_TIMER_0;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.pixel_format = PIXFORMAT_JPEG;       // Edge Impulse 예제 방식: JPEG → RGB888 변환 → 96x96 리사이즈
    config.frame_size = FRAMESIZE_QQVGA;        // 160x120 (QVGA 320x240 은 infer 약 238ms)
    config.jpeg_quality = 12;
    config.fb_count = 2;                        // 추론하는 동안 다음 프레임을 미리 찍어 둔다 (대기 시간 단축)
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;      // 추론이 늦어도 항상 최신 프레임

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[face_sensor] camera init failed: 0x%x\n", err);
        return false;
    }
    sensor_t *s = esp_camera_sensor_get();
    Serial.printf("[face_sensor] camera PID 0x%x\n", s ? s->id.PID : 0);
    // OV3660 은 위아래가 뒤집혀 나온다 → 거꾸로 된 얼굴은 검출 안 됨 (Edge Impulse 예제·CameraWebServer 와 같은 보정)
    if (s && s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1);
        s->set_brightness(s, 1);
        s->set_saturation(s, 0);
    }
    return true;
}

// 예제의 ei_camera_capture() 와 같다. 한 프레임 → s_snapshot_buf 앞부분에 96x96 RGB888
static bool camera_capture(uint32_t img_width, uint32_t img_height)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[face_sensor] capture failed");
        return false;
    }
    bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, s_snapshot_buf);
    esp_camera_fb_return(fb);
    if (!converted) {
        Serial.println("[face_sensor] jpeg->rgb888 failed");
        return false;
    }

    if (img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS || img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS) {
        ei::image::processing::crop_and_interpolate_rgb888(
            s_snapshot_buf, EI_CAMERA_RAW_FRAME_BUFFER_COLS, EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
            s_snapshot_buf, img_width, img_height);
    }
    return true;
}

// run_classifier 가 픽셀을 꺼내 갈 때 부르는 함수 (예제의 ei_camera_get_data 와 같다)
static int camera_get_data(size_t offset, size_t length, float *out_ptr)
{
    size_t pixel_ix = offset * 3;
    for (size_t i = 0; i < length; i++) {
        // fmt2rgb888 은 실제로 BGR 순서로 쓴다 → RGB 로 뒤집어 0xRRGGBB 로 묶는다
        // https://github.com/espressif/esp32-camera/issues/379
        out_ptr[i] = (s_snapshot_buf[pixel_ix + 2] << 16) + (s_snapshot_buf[pixel_ix + 1] << 8) + s_snapshot_buf[pixel_ix];
        pixel_ix += 3;
    }
    return 0;
}

static void face_task(void *arg)
{
    (void)arg;
    uint32_t frame_id = 0;
    for (;;) {
        uint32_t t0 = millis();

        // 실패하면 결과를 보내지 않는다. 계속 실패하면 presence_link 가 FP_SENSOR_TIMEOUT_MS 로 알아챈다.
        if (!camera_capture(EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        ei::signal_t signal;
        signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
        signal.get_data = &camera_get_data;

        ei_impulse_result_t result = {};
        EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
        if (err != EI_IMPULSE_OK) {
            Serial.printf("[face_sensor] run_classifier failed: %d\n", err);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // FOMO: bounding_boxes = 얼굴 중심점 목록. 점수가 임계값 이상인 것만 얼굴로 센다.
        face_result_t r = {};
        for (size_t i = 0; i < result.bounding_boxes_count; i++) {
            const ei_impulse_result_bounding_box_t &bb = result.bounding_boxes[i];
            if (bb.value < FP_SCORE_THRESHOLD) {
                continue;
            }
            if (r.count < UINT8_MAX) {
                r.count++;
            }
            if (bb.value > r.best_score) {
                r.best_score = bb.value;
            }
        }

        r.detected = r.count > 0;
        r.infer_ms = (uint16_t)(millis() - t0);   // 캡처 + 변환 + 추론 전체
        r.frame_id = ++frame_id;
        r.ts_ms = millis();
        xQueueOverwrite(s_out_queue, &r);

        // presence_link 는 첫 결과가 FP_SENSOR_TIMEOUT_MS 안에 와야 sensor_timeout 을 띄우지 않는다
        if (r.frame_id == 1) {
            Serial.printf("[face_sensor] first result at %lums after boot\n", (unsigned long)r.ts_ms);
        }

#if FP_SENSOR_LOG
        Serial.printf("[face_sensor] #%lu faces=%u score=%.2f infer=%ums (dsp %dms, nn %dms)\n",
                      (unsigned long)r.frame_id, r.count, r.best_score, r.infer_ms,
                      result.timing.dsp, result.timing.classification);
#endif
        vTaskDelay(1);   // 다른 태스크(와치독)에 양보
    }
}

FaceSensorStatus face_sensor_start(QueueHandle_t out_queue)
{
    s_out_queue = out_queue;
    if (!camera_init()) {
        return FS_CAMERA_INIT;
    }

    // fmt2rgb888 이 원본 크기(160x120)로 쓰기 때문에 96x96 이 아니라 원본 크기만큼 필요하다.
    s_snapshot_buf = (uint8_t *)ps_malloc(EI_CAMERA_RAW_FRAME_BUFFER_COLS * EI_CAMERA_RAW_FRAME_BUFFER_ROWS * EI_CAMERA_FRAME_BYTE_SIZE);
    if (!s_snapshot_buf) {
        Serial.println("[face_sensor] snapshot buffer alloc failed (PSRAM: OPI PSRAM 켜져 있나?)");
        return FS_MODEL_INIT;
    }
    Serial.printf("[face_sensor] model %dx%d, threshold %.2f\n",
                  EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, FP_SCORE_THRESHOLD);

    // 추론은 스택을 많이 쓴다. 와치독/스택 오버플로가 나면 크기를 늘린다.
    xTaskCreatePinnedToCore(face_task, "face_sensor", 16384, NULL, 5, NULL, 1);
    return FS_OK;
}

#endif  // !FP_SENSOR_STUB

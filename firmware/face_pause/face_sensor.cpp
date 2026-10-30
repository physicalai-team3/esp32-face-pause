// 팀원 1: 카메라 + Edge Impulse FOMO 얼굴 검출
// config.h 에서 FP_SENSOR_STUB 0 일 때만 컴파일된다. (1이면 face_sensor_stub.cpp 가 대신 쓰임)
// 참고: docs/05-face-model.md, Edge Impulse 라이브러리의 examples/esp32/esp32_camera
#include "config.h"

#if !FP_SENSOR_STUB

#include "face_sensor.h"
#include "camera_pins.h"
#include "esp_camera.h"

// TODO(팀원1): 내보낸 라이브러리의 헤더 이름으로 교체
//              firmware/libraries/<라이브러리 폴더>/src/*_inferencing.h
#include <Face_detection_-_FOMO_inferencing.h>

static QueueHandle_t s_out_queue;

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
    config.frame_size = FRAMESIZE_QVGA;         // 320x240
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;      // 추론이 늦어도 항상 최신 프레임

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[face_sensor] camera init failed: 0x%x\n", err);
        return false;
    }
    sensor_t *s = esp_camera_sensor_get();
    Serial.printf("[face_sensor] camera PID 0x%x\n", s ? s->id.PID : 0);
    // TODO(팀원1): 화면이 뒤집혀 있으면 s->set_vflip(s, 1), s->set_hmirror(s, 1)
    return true;
}

static void face_task(void *arg)
{
    (void)arg;
    uint32_t frame_id = 0;
    for (;;) {
        face_result_t r = {};
        uint32_t t0 = millis();

        // TODO(팀원1): docs/05-face-model.md §4
        //  1) 캡처 → 96x96 RGB888 (예제의 ei_camera_capture(EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, buf))
        //  2) ei::signal_t 준비 → run_classifier(&signal, &result, false)
        //  3) result.bounding_boxes[i].value >= FP_SCORE_THRESHOLD 인 것만 세기
        //       r.count++, r.best_score = 최대값

        r.detected = r.count > 0;
        r.infer_ms = (uint16_t)(millis() - t0);
        r.frame_id = ++frame_id;
        r.ts_ms = millis();
        xQueueOverwrite(s_out_queue, &r);

#if FP_SENSOR_LOG
        Serial.printf("[face_sensor] #%lu faces=%u score=%.2f infer=%ums\n",
                      (unsigned long)r.frame_id, r.count, r.best_score, r.infer_ms);
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
    // TODO(팀원1): 96x96x3 스냅샷 버퍼를 ps_malloc() 으로 할당, 실패하면 return FS_MODEL_INIT;

    // 추론은 스택을 많이 쓴다. 와치독/스택 오버플로가 나면 크기를 늘린다.
    xTaskCreatePinnedToCore(face_task, "face_sensor", 16384, NULL, 5, NULL, 1);
    return FS_OK;
}

#endif  // !FP_SENSOR_STUB

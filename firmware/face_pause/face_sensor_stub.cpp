// 팀원 2: 가짜 face_sensor — config.h 에서 FP_SENSOR_STUB 1 일 때만 컴파일된다.
// BOOT 버튼을 누르고 있는 동안 "얼굴 있음". 카메라·모델 없이 presence_link 를 개발/테스트할 수 있다.
#include "config.h"

#if FP_SENSOR_STUB

#include "face_sensor.h"

static QueueHandle_t s_out_queue;

static void stub_task(void *arg)
{
    (void)arg;
    uint32_t frame_id = 0;
    for (;;) {
        bool pressed = digitalRead(FP_BOOT_PIN) == LOW;

        face_result_t r = {};
        r.detected = pressed;
        r.count = pressed ? 1 : 0;
        r.best_score = pressed ? 0.9f : 0.0f;
        r.infer_ms = 100;
        r.frame_id = ++frame_id;
        r.ts_ms = millis();
        xQueueOverwrite(s_out_queue, &r);

        vTaskDelay(pdMS_TO_TICKS(100));   // 실제 모델과 비슷하게 약 10 FPS
    }
}

FaceSensorStatus face_sensor_start(QueueHandle_t out_queue)
{
    s_out_queue = out_queue;
    pinMode(FP_BOOT_PIN, INPUT_PULLUP);
    xTaskCreatePinnedToCore(stub_task, "face_stub", 4096, NULL, 5, NULL, 1);
    Serial.println("[face_sensor] STUB mode: hold BOOT button = face");
    return FS_OK;
}

#endif  // FP_SENSOR_STUB

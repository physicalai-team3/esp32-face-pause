// 팀원 1 제공: 카메라 + 얼굴 검출 (docs/03-messages.md §5)
// Wi-Fi·MQTT는 전혀 모른다. 프레임마다 face_result_t 를 큐에 넣기만 한다.
#pragma once

#include <Arduino.h>
#include "face_result.h"

enum FaceSensorStatus {
    FS_OK = 0,
    FS_CAMERA_INIT,   // 카메라 초기화 실패
    FS_MODEL_INIT,    // 모델/버퍼 준비 실패
};

// 카메라·모델을 초기화하고 검출 태스크를 시작한다.
// out_queue: 길이 1, 항목 크기 sizeof(face_result_t).
//            태스크는 xQueueOverwrite() 로 항상 최신 결과만 남긴다.
// config.h 의 FP_SENSOR_STUB 이 1이면 face_sensor_stub.cpp 의 구현이 대신 쓰인다.
FaceSensorStatus face_sensor_start(QueueHandle_t out_queue);

// status 토픽의 reason 값과 같은 문자열 (shared/schema/status.schema.json)
static inline const char *face_sensor_status_str(FaceSensorStatus s)
{
    switch (s) {
        case FS_OK:          return "ok";
        case FS_CAMERA_INIT: return "camera_init";
        case FS_MODEL_INIT:  return "model_init";
    }
    return "unknown";
}

// 팀원 2 제공: Wi-Fi + MQTT + 디바운스 (docs/03-messages.md §2~5)
// 카메라·모델은 전혀 모른다. 큐에서 face_result_t 를 읽기만 한다.
#pragma once

#include <Arduino.h>
#include "face_result.h"

// Wi-Fi·MQTT 접속, 디바운스, 발행을 담당하는 태스크를 시작한다.
// in_queue 에서 face_result_t 를 xQueueReceive() 로 읽는다.
bool presence_link_start(QueueHandle_t in_queue);

// face_sensor_start() 가 실패했을 때 setup() 이 호출한다.
// MQTT 접속 후 status: {"state":"error","reason":<reason>} 를 발행한다.
void presence_link_report_sensor_error(const char *reason);

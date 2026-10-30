// esp32-face-pause 펌웨어 진입점 (팀원 2)
// face_sensor(팀원 1) 와 presence_link(팀원 2) 를 큐 하나로 연결하는 유일한 지점.
// 실제 일은 두 FreeRTOS 태스크가 하고, loop() 는 비워 둔다.
//
// 처음이라면: config.example.h 를 config.h 로 복사해서 Wi-Fi·노트북 IP 를 채운 뒤 업로드.
#include "config.h"
#include "face_result.h"
#include "face_sensor.h"
#include "presence_link.h"

static QueueHandle_t g_face_queue;

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.printf("\n[face_pause] fw %s, device %s, psram %s\n",
                  FP_FW_VERSION, FP_DEVICE_ID, psramFound() ? "OK" : "NOT FOUND (Tools > PSRAM > OPI PSRAM)");

    // 길이 1: 항상 최신 결과 하나만 (face_sensor 가 xQueueOverwrite 로 덮어씀)
    g_face_queue = xQueueCreate(1, sizeof(face_result_t));

    presence_link_start(g_face_queue);   // 먼저: Wi-Fi 접속 시작, LED

    FaceSensorStatus st = face_sensor_start(g_face_queue);
    if (st != FS_OK) {
        Serial.printf("[face_pause] face_sensor_start failed: %s\n", face_sensor_status_str(st));
        presence_link_report_sensor_error(face_sensor_status_str(st));
    }
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}

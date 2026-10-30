// 팀원 2: Wi-Fi + MQTT(PubSubClient) + 디바운스 → presence/status/telemetry 발행
// 메시지 형식: docs/03-messages.md §2~4, shared/schema/*.json
#include "config.h"
#include "presence_link.h"
#include "debounce.h"

#include <WiFi.h>
#include <PubSubClient.h>

static QueueHandle_t s_in_queue;
static WiFiClient s_net;
static PubSubClient s_mqtt(s_net);
static Debouncer s_deb;
static uint32_t s_presence_seq = 0;
static const char *volatile s_sensor_error = nullptr;   // setup() 에서 설정, 태스크에서 읽음

static char s_topic_presence[64];
static char s_topic_status[64];
static char s_topic_telemetry[64];

static void build_topics()
{
    snprintf(s_topic_presence, sizeof(s_topic_presence), "facepause/%s/presence", FP_DEVICE_ID);
    snprintf(s_topic_status, sizeof(s_topic_status), "facepause/%s/status", FP_DEVICE_ID);
    snprintf(s_topic_telemetry, sizeof(s_topic_telemetry), "facepause/%s/telemetry", FP_DEVICE_ID);
}

static void set_led(bool present)
{
    digitalWrite(FP_LED_PIN, present ? LOW : HIGH);   // active-low
}

// Wi-Fi 가 끊겨 있으면 재접속을 시도한다. 접속돼 있으면 true.
static bool wifi_ensure()
{
    // TODO(팀원2):
    //  - WiFi.status() == WL_CONNECTED 이면 return true
    //  - 아니면 WiFi.begin(FP_WIFI_SSID, FP_WIFI_PASS) (매 루프마다 begin 하지 말고 몇 초 간격으로)
    //  - 접속되면 Serial 에 WiFi.localIP() 출력
    return false;
}

// MQTT 가 끊겨 있으면 재접속한다. "이번 호출에서 새로 접속됐으면" true.
static bool mqtt_ensure()
{
    // TODO(팀원2):
    //  - s_mqtt.connected() 이면 return false
    //  - s_mqtt.setServer(FP_MQTT_HOST, FP_MQTT_PORT), setKeepAlive(FP_MQTT_KEEPALIVE_S), setBufferSize(512)
    //  - LWT 등록하며 접속 (재시도는 몇 초 간격으로):
    //      s_mqtt.connect(FP_DEVICE_ID, s_topic_status, 1, true, "{\"v\":1,\"device\":\"...\",\"state\":\"offline\"}")
    //  - 성공하면 return true
    return false;
}

static void publish_status()
{
    // TODO(팀원2): retained 로 발행
    //  s_sensor_error 가 있으면 {"v":1,"device":..,"state":"error","reason":<s_sensor_error>}
    //  없으면              {"v":1,"device":..,"state":"online","fw":FP_FW_VERSION,"ip":<WiFi.localIP()>}
}

static void publish_presence(const face_result_t *last)
{
    // TODO(팀원2): retained 로 발행, s_presence_seq 증가
    //  {"v":1,"device":..,"state":presence_state_str(s_deb.state),"faces":..,"score":..,"seq":..,"uptime_ms":millis()}
    //  absent 이면 faces=0, score=0
    (void)last;
}

static void publish_telemetry(const face_result_t *last, float fps)
{
    // TODO(팀원2): retained 아님
    //  {"v":1,"device":..,"faces":..,"score":..,"fps":..,"infer_ms":..,"rssi":WiFi.RSSI(),
    //   "heap_kb":ESP.getFreeHeap()/1024,"psram_kb":ESP.getFreePsram()/1024,"uptime_ms":millis()}
    (void)last;
    (void)fps;
}

static void link_task(void *arg)
{
    (void)arg;
    face_result_t last = {};
    uint32_t last_frame_ms = millis();
    uint32_t last_telemetry_ms = millis();
    uint32_t frames_since_telemetry = 0;

    for (;;) {
        if (wifi_ensure()) {
            if (mqtt_ensure()) {
                // 새로 접속(재접속)할 때마다: status → 현재 presence 1회
                publish_status();
                publish_presence(&last);
            }
            s_mqtt.loop();
        }

        bool changed;
        face_result_t r;
        if (xQueueReceive(s_in_queue, &r, pdMS_TO_TICKS(50)) == pdTRUE) {
            last = r;
            last_frame_ms = millis();
            frames_since_telemetry++;
            changed = debounce_update(&s_deb, r.detected, millis());
        } else {
            changed = debounce_tick(&s_deb, millis());
        }

        // TODO(팀원2): (millis() - last_frame_ms) >= FP_SENSOR_TIMEOUT_MS 이면
        //              s_sensor_error = "sensor_timeout" 으로 두고 publish_status() (한 번만)
        (void)last_frame_ms;

        if (changed) {
            Serial.printf("[presence_link] -> %s\n", presence_state_str(s_deb.state));
            set_led(s_deb.state == PRESENCE_PRESENT);
            if (s_mqtt.connected()) {
                publish_presence(&last);
            }
        }

        uint32_t now = millis();
        if (now - last_telemetry_ms >= FP_TELEMETRY_MS) {
            float fps = frames_since_telemetry * 1000.0f / (now - last_telemetry_ms);
            if (s_mqtt.connected()) {
                publish_telemetry(&last, fps);
            }
            last_telemetry_ms = now;
            frames_since_telemetry = 0;
        }
    }
}

bool presence_link_start(QueueHandle_t in_queue)
{
    s_in_queue = in_queue;
    build_topics();
    pinMode(FP_LED_PIN, OUTPUT);
    set_led(false);
    debounce_init(&s_deb, DebounceConfig{FP_PRESENT_FRAMES, FP_ABSENT_MS});

    WiFi.mode(WIFI_STA);
    // Wi-Fi 스택은 core 0 에서 돈다. 이 태스크도 core 0, 얼굴 검출은 core 1.
    return xTaskCreatePinnedToCore(link_task, "presence_link", 8192, NULL, 4, NULL, 0) == pdPASS;
}

void presence_link_report_sensor_error(const char *reason)
{
    s_sensor_error = reason;
    // 이미 접속돼 있으면 다음 루프에서 반영되도록 TODO(팀원2): 접속 중이면 publish_status() 다시 호출
}

// 팀원 2: Wi-Fi + MQTT(PubSubClient) + 디바운스 → presence/status/telemetry 발행
// 메시지 형식: docs/03-messages.md §2~4, shared/schema/*.json
#include "config.h"
#include "presence_link.h"
#include "debounce.h"

#include <WiFi.h>
#include <PubSubClient.h>

#define WIFI_RETRY_MS        10000   // Wi-Fi 재접속 시도 간격 (접속에 보통 2~5초 걸림)
#define MQTT_RETRY_MS        3000    // MQTT 재접속 시도 간격
#define MQTT_SOCKET_TIMEOUT_S 2      // 브로커 응답 대기 (기본 15초면 그동안 큐·디바운스가 멈춤)

static QueueHandle_t s_in_queue;
static WiFiClient s_net;
static PubSubClient s_mqtt(s_net);
static Debouncer s_deb;
static uint32_t s_presence_seq = 0;
static const char *volatile s_sensor_error = nullptr;   // setup() 에서 설정, 태스크에서 읽음
static volatile bool s_status_dirty = false;            // status 를 다시 발행해야 함 (태스크에서만 발행)

static char s_topic_presence[64];
static char s_topic_status[64];
static char s_topic_telemetry[64];
static char s_lwt_payload[64];

static void build_topics()
{
    snprintf(s_topic_presence, sizeof(s_topic_presence), "facepause/%s/presence", FP_DEVICE_ID);
    snprintf(s_topic_status, sizeof(s_topic_status), "facepause/%s/status", FP_DEVICE_ID);
    snprintf(s_topic_telemetry, sizeof(s_topic_telemetry), "facepause/%s/telemetry", FP_DEVICE_ID);
    snprintf(s_lwt_payload, sizeof(s_lwt_payload), "{\"v\":1,\"device\":\"%s\",\"state\":\"offline\"}", FP_DEVICE_ID);
}

static void set_led(bool present)
{
    digitalWrite(FP_LED_PIN, present ? LOW : HIGH);   // active-low
}

// Wi-Fi 가 끊겨 있으면 재접속을 시도한다. 접속돼 있으면 true.
static bool wifi_ensure()
{
    static bool s_was_connected = false;
    static bool s_attempted = false;
    static uint32_t s_last_attempt_ms = 0;

    if (WiFi.status() == WL_CONNECTED) {
        if (!s_was_connected) {
            s_was_connected = true;
            Serial.printf("[presence_link] wifi connected, ip %s, rssi %d\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());
        }
        return true;
    }

    if (s_was_connected) {
        s_was_connected = false;
        Serial.println("[presence_link] wifi lost");
    }

    uint32_t now = millis();
    if (!s_attempted || now - s_last_attempt_ms >= WIFI_RETRY_MS) {
        s_attempted = true;
        s_last_attempt_ms = now;
        Serial.printf("[presence_link] wifi connecting to %s ...\n", FP_WIFI_SSID);
        WiFi.disconnect();
        WiFi.begin(FP_WIFI_SSID, FP_WIFI_PASS);
    }
    return false;
}

// MQTT 가 끊겨 있으면 재접속한다. "이번 호출에서 새로 접속됐으면" true.
static bool mqtt_ensure()
{
    static bool s_attempted = false;
    static uint32_t s_last_attempt_ms = 0;

    if (s_mqtt.connected()) return false;

    uint32_t now = millis();
    if (s_attempted && now - s_last_attempt_ms < MQTT_RETRY_MS) return false;
    s_attempted = true;
    s_last_attempt_ms = now;

    Serial.printf("[presence_link] mqtt connecting to %s:%d ...\n", FP_MQTT_HOST, FP_MQTT_PORT);
    // LWT: 비정상 종료 시 브로커가 status 토픽에 offline 을 retained 로 대신 발행
    if (s_mqtt.connect(FP_DEVICE_ID, s_topic_status, 1, true, s_lwt_payload)) {
        Serial.println("[presence_link] mqtt connected");
        return true;
    }
    // state(): -2 접속 실패(IP·방화벽 확인), -4 응답 없음, 5 권한 없음 (PubSubClient.h)
    Serial.printf("[presence_link] mqtt connect failed, state %d\n", s_mqtt.state());
    return false;
}

static void publish(const char *topic, const char *payload, bool retained)
{
    if (!s_mqtt.publish(topic, payload, retained)) {
        Serial.printf("[presence_link] publish failed: %s\n", topic);
    }
}

static void publish_status()
{
    char buf[160];
    const char *err = s_sensor_error;
    if (err) {
        snprintf(buf, sizeof(buf), "{\"v\":1,\"device\":\"%s\",\"state\":\"error\",\"reason\":\"%s\"}",
                 FP_DEVICE_ID, err);
    } else {
        snprintf(buf, sizeof(buf), "{\"v\":1,\"device\":\"%s\",\"state\":\"online\",\"fw\":\"%s\",\"ip\":\"%s\"}",
                 FP_DEVICE_ID, FP_FW_VERSION, WiFi.localIP().toString().c_str());
    }
    publish(s_topic_status, buf, true);
    Serial.printf("[presence_link] status %s\n", buf);
}

static void publish_presence(const face_result_t *last)
{
    bool present = s_deb.state == PRESENCE_PRESENT;
    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"v\":1,\"device\":\"%s\",\"state\":\"%s\",\"faces\":%u,\"score\":%.2f,\"seq\":%lu,\"uptime_ms\":%lu}",
             FP_DEVICE_ID, presence_state_str(s_deb.state),
             present ? (unsigned)last->count : 0u,
             present ? last->best_score : 0.0f,
             (unsigned long)s_presence_seq++, (unsigned long)millis());
    publish(s_topic_presence, buf, true);
}

static void publish_telemetry(const face_result_t *last, float fps)
{
    char buf[224];
    snprintf(buf, sizeof(buf),
             "{\"v\":1,\"device\":\"%s\",\"faces\":%u,\"score\":%.2f,\"fps\":%.1f,\"infer_ms\":%u,\"rssi\":%d,"
             "\"heap_kb\":%lu,\"psram_kb\":%lu,\"uptime_ms\":%lu}",
             FP_DEVICE_ID, (unsigned)last->count, last->best_score, fps, (unsigned)last->infer_ms,
             (int)WiFi.RSSI(), (unsigned long)(ESP.getFreeHeap() / 1024),
             (unsigned long)(ESP.getFreePsram() / 1024), (unsigned long)millis());
    publish(s_topic_telemetry, buf, false);
}

static void link_task(void *arg)
{
    (void)arg;
    face_result_t last = {};
    uint32_t last_frame_ms = millis();
    uint32_t last_telemetry_ms = millis();
    uint32_t telemetry_frame_id = 0;   // 직전 텔레메트리 시점의 frame_id

    for (;;) {
        if (wifi_ensure()) {
            if (mqtt_ensure()) {
                // 새로 접속(재접속)할 때마다: status → 현재 presence 1회
                s_status_dirty = false;
                publish_status();
                publish_presence(&last);
            }
            if (s_status_dirty && s_mqtt.connected()) {
                s_status_dirty = false;
                publish_status();
            }
            s_mqtt.loop();
        }

        bool changed;
        face_result_t r;
        if (xQueueReceive(s_in_queue, &r, pdMS_TO_TICKS(50)) == pdTRUE) {
            last = r;
            last_frame_ms = millis();
            changed = debounce_update(&s_deb, r.detected, millis());

            // 센서가 다시 결과를 보내기 시작하면 sensor_timeout 오류를 해제한다
            if (s_sensor_error && strcmp(s_sensor_error, "sensor_timeout") == 0) {
                Serial.println("[presence_link] sensor recovered");
                s_sensor_error = nullptr;
                s_status_dirty = true;
            }
        } else {
            changed = debounce_tick(&s_deb, millis());
        }

        // 센서 결과가 오래 안 오면 error 를 한 번만 발행 (camera_init 등 기존 오류는 덮어쓰지 않음)
        if (!s_sensor_error && millis() - last_frame_ms >= FP_SENSOR_TIMEOUT_MS) {
            Serial.println("[presence_link] sensor timeout");
            s_sensor_error = "sensor_timeout";
            s_status_dirty = true;
        }

        if (changed) {
            Serial.printf("[presence_link] -> %s\n", presence_state_str(s_deb.state));
            set_led(s_deb.state == PRESENCE_PRESENT);
            if (s_mqtt.connected()) {
                publish_presence(&last);
            }
        }

        uint32_t now = millis();
        if (now - last_telemetry_ms >= FP_TELEMETRY_MS) {
            // 큐가 길이 1(덮어쓰기)이라 받은 횟수를 세면 놓친 프레임이 빠진다 → frame_id 증가량으로 계산
            float fps = (last.frame_id - telemetry_frame_id) * 1000.0f / (now - last_telemetry_ms);
            if (s_mqtt.connected()) {
                publish_telemetry(&last, fps);
            }
            last_telemetry_ms = now;
            telemetry_frame_id = last.frame_id;
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

    s_mqtt.setServer(FP_MQTT_HOST, FP_MQTT_PORT);
    s_mqtt.setKeepAlive(FP_MQTT_KEEPALIVE_S);
    s_mqtt.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
    s_mqtt.setBufferSize(512);

    WiFi.mode(WIFI_STA);
    // Wi-Fi 스택은 core 0 에서 돈다. 이 태스크도 core 0, 얼굴 검출은 core 1.
    return xTaskCreatePinnedToCore(link_task, "presence_link", 8192, NULL, 4, NULL, 0) == pdPASS;
}

void presence_link_report_sensor_error(const char *reason)
{
    // setup()(다른 태스크)에서 불린다. PubSubClient 는 태스크 간에 안전하지 않으므로 여기서 직접 발행하지 않고
    // 표시만 해 두면 link_task 가 접속 중일 때 바로, 아니면 접속 직후에 발행한다.
    s_sensor_error = reason;
    s_status_dirty = true;
}

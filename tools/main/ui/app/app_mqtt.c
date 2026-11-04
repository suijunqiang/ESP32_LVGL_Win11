/*
 * MQTT client task.
 *
 * Connects to the broker (URI + subscription topic are configurable at
 * runtime), publishes a status heartbeat and accepts UI-driven pub/sub
 * requests. Log lines are kept in a ring buffer the UI panel can render.
 */

#include "app_mqtt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "screenshot.h"

#define MQTT_PUBLISH_MS     10000
#define MQTT_DEFAULT_URI    "mqtt://suijunqiang.top:1883"
#define MQTT_DEFAULT_TOPIC  "esp32s3/win11/cmd"
#define MQTT_STATUS_TOPIC   "esp32s3/win11/status"

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client;
static volatile bool s_connected;
static char s_client_id[32];

/* Config: copied at boot from defaults, overridable by app_mqtt_set_config(). */
static char s_uri[MQTT_URI_MAX] = MQTT_DEFAULT_URI;
static char s_topic[MQTT_TOPIC_MAX] = MQTT_DEFAULT_TOPIC;

/* Log ring buffer, one line per slot. */
static char s_log_lines[MQTT_LOG_LINES][MQTT_LOG_LINE_MAX];
static unsigned s_log_head;   /* next write index */
static unsigned s_log_count;  /* how many slots are filled (0..MQTT_LOG_LINES) */
static SemaphoreHandle_t s_log_lock;

static void mqtt_log_v(const char *fmt, va_list ap)
{
    char buf[MQTT_LOG_LINE_MAX];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n < 0) {
        n = 0;
    }
    if ((unsigned)n >= sizeof(buf)) {
        buf[sizeof(buf) - 1] = '\0';
    }

    if (s_log_lock != NULL) {
        if (xSemaphoreTake(s_log_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
            return;
        }
    }

    unsigned i = s_log_head;
    s_log_head = (s_log_head + 1) % MQTT_LOG_LINES;
    if (s_log_count < MQTT_LOG_LINES) {
        s_log_count++;
    }
    /* Truncate buf so the timestamp prefix + NUL always fit in 128 bytes
     * without triggering the compiler's -Wformat-truncation check. */
    char line[MQTT_LOG_LINE_MAX];
    int prefix = snprintf(line, sizeof(line), "[%llu] ",
                          (unsigned long long)(esp_timer_get_time()
                                               / 1000000ULL));
    if (prefix < 0) prefix = 0;
    if ((unsigned)prefix >= sizeof(line)) {
        prefix = (int)sizeof(line) - 1;
        line[prefix] = '\0';
    }
    unsigned avail = sizeof(line) - (unsigned)prefix - 1;
    unsigned blen = (unsigned)n < avail ? (unsigned)n : avail;
    memcpy(line + prefix, buf, blen);
    line[prefix + blen] = '\0';
    memcpy(s_log_lines[i], line, sizeof(line));

    if (s_log_lock != NULL) {
        xSemaphoreGive(s_log_lock);
    }
}

void app_mqtt_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    mqtt_log_v(fmt, ap);
    va_end(ap);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            app_mqtt_log("connected to %s", s_uri);
            ESP_LOGI(TAG, "connected to %s", s_uri);
            esp_mqtt_client_subscribe(s_client, s_topic, 0);
            esp_mqtt_client_publish(s_client, MQTT_STATUS_TOPIC,
                                    "{\"state\":\"online\"}", 0, 0, 1);
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            app_mqtt_log("disconnected");
            ESP_LOGW(TAG, "disconnected");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            app_mqtt_log("subscribed #%ld", event->msg_id);
            break;

        case MQTT_EVENT_DATA: {
            char topic_buf[MQTT_TOPIC_MAX];
            char data_buf[MQTT_PAYLOAD_MAX];
            int tl = event->topic_len < (int)sizeof(topic_buf) - 1
                       ? event->topic_len
                       : (int)sizeof(topic_buf) - 1;
            int dl = event->data_len < (int)sizeof(data_buf) - 1
                       ? event->data_len
                       : (int)sizeof(data_buf) - 1;
            memcpy(topic_buf, event->topic, (size_t)tl);
            topic_buf[tl] = '\0';
            memcpy(data_buf, event->data, (size_t)dl);
            data_buf[dl] = '\0';
            app_mqtt_log("rx %s = %s", topic_buf, data_buf);

            if (dl >= 10 && strncmp(data_buf, "screenshot", 10) == 0) {
                screenshot_request();
                app_mqtt_log("screenshot requested");
            }
            break;
        }

        case MQTT_EVENT_PUBLISHED:
            app_mqtt_log("tx #%ld", event->msg_id);
            break;

        case MQTT_EVENT_ERROR:
            app_mqtt_log("transport error");
            ESP_LOGW(TAG, "transport error");
            break;

        default:
            break;
    }
}

static void mqtt_worker_task(void *arg)
{
    (void)arg;

    for (;;) {
        if (s_connected) {
            char clock_text[24] = "--:--";
            time_t now = time(NULL);
            if (now > 1700000000) {
                struct tm local;
                localtime_r(&now, &local);
                strftime(clock_text, sizeof(clock_text), "%Y-%m-%d %H:%M:%S",
                         &local);
            }

            char payload[192];
            int len = snprintf(payload, sizeof(payload),
                               "{\"state\":\"online\",\"uptime\":%lld,"
                               "\"heap\":%u,\"wifi\":true,\"time\":\"%s\"}",
                               (long long)(esp_timer_get_time() / 1000000),
                               (unsigned)heap_caps_get_free_size(
                                   MALLOC_CAP_INTERNAL),
                               clock_text);
            if (len > 0 && s_client != NULL) {
                esp_mqtt_client_publish(s_client, MQTT_STATUS_TOPIC,
                                        payload, len, 0, 0);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MQTT_PUBLISH_MS));
    }
}

static void mqtt_client_init_and_start(void)
{
    if (s_client != NULL) {
        esp_mqtt_client_disconnect(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }

    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = s_uri,
        .credentials.client_id = s_client_id,
        .session.keepalive = 30,
        .session.last_will = {
            .topic = MQTT_STATUS_TOPIC,
            .msg = "{\"state\":\"offline\"}",
            .msg_len = 0,
            .qos = 0,
            .retain = 1,
        },
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "cannot create the MQTT client");
        return;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));

    app_mqtt_log("starting client %s -> %s", s_client_id, s_uri);
    esp_mqtt_client_start(s_client);
}

void app_mqtt_start(void)
{
    if (s_log_lock == NULL) {
        s_log_lock = xSemaphoreCreateMutex();
    }
    s_log_head = 0;
    s_log_count = 0;
    memset(s_log_lines, 0, sizeof(s_log_lines));

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_client_id, sizeof(s_client_id), "esp32s3-win11-%02x%02x%02x",
             mac[3], mac[4], mac[5]);

    mqtt_client_init_and_start();

    BaseType_t ok = xTaskCreate(mqtt_worker_task, "mqtt_task", 4096, NULL, 4,
                                NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "cannot create the MQTT task");
        return;
    }
}

bool app_mqtt_is_connected(void)
{
    return s_connected;
}

void app_mqtt_get_status(mqtt_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->uri, sizeof(out->uri), "%s", s_uri);
    snprintf(out->topic, sizeof(out->topic), "%s", s_topic);
    snprintf(out->cmd_topic, sizeof(out->cmd_topic), "%s", s_topic);
    out->connected = s_connected;
    out->uptime_s = (int)(esp_timer_get_time() / 1000000LL);
    out->log_lines = s_log_count;
}

esp_err_t app_mqtt_set_config(const char *uri, const char *topic)
{
    if (uri != NULL && uri[0] != '\0') {
        snprintf(s_uri, sizeof(s_uri), "%s", uri);
    }
    if (topic != NULL) {
        snprintf(s_topic, sizeof(s_topic), "%s", topic);
    }
    app_mqtt_log("config saved: uri=%s topic=%s", s_uri, s_topic);
    return ESP_OK;
}

void app_mqtt_restart(void)
{
    app_mqtt_log("restarting with uri=%s", s_uri);
    mqtt_client_init_and_start();
}

int app_mqtt_publish(const char *topic, const char *payload)
{
    if (s_client == NULL || topic == NULL) {
        return -1;
    }
    if (payload == NULL) {
        payload = "";
    }

    int id = esp_mqtt_client_publish(s_client, topic, payload, 0, 0, 0);
    app_mqtt_log("tx %s = %s (id=%d)", topic, payload, id);
    return id;
}

esp_err_t app_mqtt_subscribe(const char *topic)
{
    if (s_client == NULL || topic == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int id = esp_mqtt_client_subscribe(s_client, topic, 0);
    if (id < 0) {
        return ESP_FAIL;
    }
    app_mqtt_log("subscribe %s (id=%d)", topic, id);
    return ESP_OK;
}

void app_mqtt_get_log(char *out, size_t size)
{
    if (out == NULL || size == 0) {
        return;
    }
    out[0] = '\0';

    if (s_log_lock != NULL &&
        xSemaphoreTake(s_log_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    if (s_log_count == 0) {
        if (s_log_lock != NULL) xSemaphoreGive(s_log_lock);
        return;
    }

    /* Read oldest first: it sits at (head - count + MQTT_LOG_LINES) % MQTT_LOG_LINES. */
    unsigned start = (s_log_head + MQTT_LOG_LINES - s_log_count) % MQTT_LOG_LINES;

    size_t written = 0;
    for (unsigned i = 0; i < s_log_count && written + 1 < size; i++) {
        const char *line = s_log_lines[(start + i) % MQTT_LOG_LINES];
        int n = snprintf(out + written, size - written, "%s\n", line);
        if (n < 0) break;
        if ((size_t)n >= size - written) {
            written = size - 1;
            break;
        }
        written += (size_t)n;
    }
    out[written] = '\0';

    if (s_log_lock != NULL) {
        xSemaphoreGive(s_log_lock);
    }
}

#ifndef APP_MQTT_H
#define APP_MQTT_H

/*
 * MQTT client running in its own FreeRTOS task.
 *
 * Broker URI and primary subscription topic are configurable at runtime
 * through the Settings panel. The client keeps a short ring buffer of log
 * lines that the panel can display.
 *
 * Every network task call (connect / subscribe / publish) is triggered by
 * the MQTT task itself or by the event handler; the UI task only writes
 * into thread-safe ring buffers or calls esp_mqtt_client_publish which
 * queues the data internally.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_URI_MAX        96
#define MQTT_TOPIC_MAX      128
#define MQTT_PAYLOAD_MAX    256
#define MQTT_LOG_LINE_MAX   128
#define MQTT_LOG_LINES      24

/* Snapshot of the current configuration. */
typedef struct {
    char uri[MQTT_URI_MAX];
    char topic[MQTT_TOPIC_MAX];
    char cmd_topic[MQTT_TOPIC_MAX];
    bool connected;
    int  uptime_s;   /* seconds since boot (0 while no SNTP yet) */
    unsigned log_lines; /* how many of the log buffer are filled */
} mqtt_status_t;

/* Create the MQTT client and start the worker task. Non blocking. */
void app_mqtt_start(void);

/* True while the broker connection is established. */
bool app_mqtt_is_connected(void);

/* Take a snapshot of the current config + live status. */
void app_mqtt_get_status(mqtt_status_t *out);

/* Override the broker URI and/or the primary subscription topic.
 * Pass NULL for the field you want to leave unchanged.
 * The change takes effect after app_mqtt_restart(). */
esp_err_t app_mqtt_set_config(const char *uri, const char *topic);

/* Reconnect with the current config: disconnect → destroy → init → start. */
void app_mqtt_restart(void);

/* Publish UTF-8 payload to the given topic. Returns the packet id (>= 0)
 * on success, or -1 on failure. safe to call from any task. */
int app_mqtt_publish(const char *topic, const char *payload);

/* Subscribe to an additional topic (on top of the primary one). */
esp_err_t app_mqtt_subscribe(const char *topic);

/* Copy the most recent log lines into `out` as a multi-line string.
 * `out` is terminated by "\n" after each line and ends with NUL. */
void app_mqtt_get_log(char *out, size_t size);

/* Append one line to the log ring buffer. Safe from any task. */
void app_mqtt_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* APP_MQTT_H */

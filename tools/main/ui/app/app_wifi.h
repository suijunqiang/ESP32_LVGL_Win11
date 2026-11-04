#ifndef APP_WIFI_H
#define APP_WIFI_H

/*
 * Wi-Fi station bring-up + runtime control.
 *
 * The module starts with the compile-time defaults below; the settings
 * panel on the desktop can override them at runtime and ask the station
 * to reconnect. app_wifi_is_connected() and app_wifi_get_ip() are safe
 * to call from any task (they read cached state).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SSID_MAX   33   /* 32 + NUL */
#define WIFI_PSK_MAX    65   /* 64 + NUL */
#define WIFI_SCAN_MAX   15   /* maximum APs returned by one scan */

typedef struct {
    char ssid[WIFI_SSID_MAX];
    int8_t rssi;
    uint8_t authmode;  /* WIFI_AUTH_* */
    uint8_t primary;   /* channel */
} wifi_ap_t;

/* Initialise netif/event loop, configure the station and start connecting.
 * This function does not block; the connection completes asynchronously. */
void app_wifi_start(void);

/* True while the station holds a valid IP address. */
bool app_wifi_is_connected(void);

/* Write the currently configured SSID / PSK. Never NULLs the output. */
void app_wifi_get_credentials(char *ssid_out, size_t ssid_size,
                              char *psk_out, size_t psk_size);

/* Override the SSID / PSK. The change takes effect after app_wifi_reconnect()
 * or the next reboot. Strings are copied; the buffers may be freed after
 * the call returns. Returns ESP_ERR_INVALID_ARG when the SSID is empty. */
esp_err_t app_wifi_set_credentials(const char *ssid, const char *psk);

/* Stop the station, re-read the stored SSID / PSK and start connecting
 * again. Does not block; use app_wifi_is_connected() to follow the state. */
void app_wifi_reconnect(void);

/* Blocking active scan. Writes up to `max_count` entries into `out`.
 * Returns the number of APs actually filled (may be > max_count in the
 * raw scan result, but only the top entries by RSSI are returned).
 * Time-out is ~3 s; safe to call from the LVGL task. */
int app_wifi_scan(wifi_ap_t *out, int max_count);

/* Write the current IPv4 address as a dotted string, or "" if not connected. */
void app_wifi_get_ip(char *ip_out, size_t ip_size);

/* Last RSSI reported by the station, or 0 when not connected. */
int8_t app_wifi_get_rssi(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_WIFI_H */

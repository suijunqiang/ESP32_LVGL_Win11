/*
 * Wi-Fi station bring-up + runtime control.
 *
 * The compile-time defaults below are used as first-boot credentials; the
 * Settings panel can override them at runtime and ask the station to
 * reconnect via app_wifi_reconnect().
 */

#include "app_wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_DEFAULT_SSID       "Tenda_8D20C0"
#define WIFI_DEFAULT_PASSWORD   "sjq111111"

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAILED_BIT     BIT1
#define WIFI_SCAN_DONE_BIT  BIT2

#define WIFI_SCAN_TIMEOUT_MS   4000

static const char *TAG = "wifi";

static EventGroupHandle_t s_wifi_events;
static volatile bool s_connected;
static bool s_sntp_started;

/* Runtime credentials (defaults copied at boot, overridable). */
static char s_ssid[WIFI_SSID_MAX] = WIFI_DEFAULT_SSID;
static char s_psk[WIFI_PSK_MAX] = WIFI_DEFAULT_PASSWORD;

/* Last known IPv4 string (updated on GOT_IP, cleared on DISCONNECTED). */
static char s_ip_string[16] = {0};

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        /* app_wifi_start() connects explicitly after the diagnostic scan. */
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;

        s_connected = false;
        s_ip_string[0] = '\0';
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_wifi_events, WIFI_FAILED_BIT);
        ESP_LOGW(TAG, "disconnected reason=%d rssi=%d, retrying",
                 ev != NULL ? ev->reason : -1, ev != NULL ? ev->rssi : 0);
        esp_wifi_connect();
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        xEventGroupSetBits(s_wifi_events, WIFI_SCAN_DONE_BIT);
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&event->ip_info.ip));

        snprintf(s_ip_string, sizeof(s_ip_string), IPSTR,
                 IP2STR(&event->ip_info.ip));

        s_connected = true;
        xEventGroupClearBits(s_wifi_events, WIFI_FAILED_BIT);
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);

        /* The system time is only useful once we are online. */
        if (!s_sntp_started) {
            setenv("TZ", "CST-8", 1);
            tzset();

            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_setservername(1, "ntp.aliyun.com");
            esp_sntp_init();
            s_sntp_started = true;
            ESP_LOGI(TAG, "SNTP started");
        }
    }
}

static void wifi_apply_credentials(void)
{
    wifi_config_t wifi_cfg = {0};

    /* Use memcpy instead of snprintf so the compiler does not warn about
     * truncation — both the runtime strings and the esp_wifi buffer cap
     * out at the same 32- / 64-byte limit. */
    memcpy(wifi_cfg.sta.ssid, s_ssid,
           sizeof(wifi_cfg.sta.ssid) - 1);
    memcpy(wifi_cfg.sta.password, s_psk,
           sizeof(wifi_cfg.sta.password) - 1);

    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
}

void app_wifi_start(void)
{
    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) {
        ESP_LOGE(TAG, "no memory for the wifi event group");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_apply_credentials();

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to \"%s\"", s_ssid);
    esp_wifi_connect();
}

bool app_wifi_is_connected(void)
{
    return s_connected;
}

void app_wifi_get_credentials(char *ssid_out, size_t ssid_size,
                              char *psk_out, size_t psk_size)
{
    if (ssid_out != NULL && ssid_size > 0) {
        snprintf(ssid_out, ssid_size, "%s", s_ssid);
    }
    if (psk_out != NULL && psk_size > 0) {
        snprintf(psk_out, psk_size, "%s", s_psk);
    }
}

esp_err_t app_wifi_set_credentials(const char *ssid, const char *psk)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    if (psk != NULL) {
        snprintf(s_psk, sizeof(s_psk), "%s", psk);
    } else {
        s_psk[0] = '\0';
    }

    ESP_LOGI(TAG, "credentials saved: ssid=\"%s\"", s_ssid);
    return ESP_OK;
}

void app_wifi_reconnect(void)
{
    ESP_LOGI(TAG, "reconnecting to \"%s\"", s_ssid);

    xEventGroupClearBits(s_wifi_events,
                         WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);

    esp_wifi_disconnect();
    esp_wifi_stop();

    wifi_apply_credentials();

    /* esp_wifi_start() alone does not connect — call connect explicitly. */
    esp_wifi_start();
    esp_wifi_connect();
}

int app_wifi_scan(wifi_ap_t *out, int max_count)
{
    if (out == NULL || max_count <= 0) {
        return 0;
    }

    xEventGroupClearBits(s_wifi_events, WIFI_SCAN_DONE_BIT);

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan_start failed: %s", esp_err_to_name(err));
        return 0;
    }

    /* Block until done or timeout. */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, WIFI_SCAN_DONE_BIT, pdTRUE, pdFALSE,
        pdMS_TO_TICKS(WIFI_SCAN_TIMEOUT_MS));

    if (!(bits & WIFI_SCAN_DONE_BIT)) {
        ESP_LOGW(TAG, "scan timed out");
        return 0;
    }

    uint16_t found = 0;
    err = esp_wifi_scan_get_ap_num(&found);
    if (err != ESP_OK || found == 0) {
        return 0;
    }

    /* Ask for more APs than we need so we can sort by RSSI below. */
    uint16_t ask = found < 30 ? found : 30;
    wifi_ap_record_t *records = calloc(ask, sizeof(wifi_ap_record_t));
    if (records == NULL) {
        return 0;
    }

    err = esp_wifi_scan_get_ap_records(&ask, records);
    if (err != ESP_OK) {
        free(records);
        return 0;
    }

    /* Sort by RSSI descending (simple insertion sort for small N). */
    for (uint16_t i = 1; i < ask; i++) {
        wifi_ap_record_t key = records[i];
        int16_t j = (int16_t)(i - 1);
        while (j >= 0 && records[j].rssi < key.rssi) {
            records[j + 1] = records[j];
            j--;
        }
        records[j + 1] = key;
    }

    int count = (int)(ask < (uint16_t)max_count ? ask : (uint16_t)max_count);
    for (int i = 0; i < count; i++) {
        snprintf(out[i].ssid, sizeof(out[i].ssid), "%s",
                 (char *)records[i].ssid);
        out[i].rssi = (int8_t)records[i].rssi;
        out[i].authmode = records[i].authmode;
        out[i].primary = records[i].primary;
    }

    free(records);
    return count;
}

void app_wifi_get_ip(char *ip_out, size_t ip_size)
{
    if (ip_out == NULL || ip_size == 0) {
        return;
    }

    snprintf(ip_out, ip_size, "%s", s_ip_string);
}

int8_t app_wifi_get_rssi(void)
{
    wifi_ap_record_t ap;

    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return (int8_t)ap.rssi;
    }

    return 0;
}

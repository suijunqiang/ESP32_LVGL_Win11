/*
 * Bluetooth Low Energy application built on NimBLE.
 *
 * Layout of this file:
 *   1. small helpers (string copy, address formatting, logging)
 *   2. device table filled by the GAP observer
 *   3. custom GATT service (read / write / notify)
 *   4. GAP event handler (scan results, connect, disconnect, subscribe)
 *   5. public API used by the Bluetooth panel
 */

#include "app_bt.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* Provided by NimBLE's config store but not exposed in a public header. */
void ble_store_config_init(void);

#define BT_DEFAULT_NAME   "esp32s3-win11"
#define BT_CONNECT_TIMEOUT_MS 30000

static const char *TAG = "bt";

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */

static SemaphoreHandle_t s_lock;      /* guards devices + log + value  */

static volatile bool s_host_ready;
static volatile bool s_scanning;
static volatile bool s_advertising;
static volatile bool s_connected;
static volatile uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_own_addr_type;

static char s_name[BT_NAME_MAX] = BT_DEFAULT_NAME;
static char s_own_addr[BT_ADDR_MAX];
static char s_peer_addr[BT_ADDR_MAX];

static bt_device_t s_devices[BT_MAX_DEVICES];
static unsigned s_device_count;

static char s_log_lines[BT_LOG_LINES][BT_LOG_LINE_MAX];
static unsigned s_log_head;
static unsigned s_log_count;

static char s_gatt_value[BT_VALUE_MAX] = "hello";
static uint16_t s_gatt_val_handle;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void bt_copy_str(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0) {
        return;
    }
    if (src == NULL) {
        src = "";
    }
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void bt_addr_str(const ble_addr_t *addr, char *out, size_t size)
{
    if (out == NULL || size < BT_ADDR_MAX) {
        if (out != NULL && size > 0) {
            out[0] = '\0';
        }
        return;
    }
    snprintf(out, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             addr->val[5], addr->val[4], addr->val[3],
             addr->val[2], addr->val[1], addr->val[0]);
}

/* Parse "aa:bb:cc:dd:ee:ff" (or "aabbccddeeff") into a NimBLE address. */
static bool bt_parse_addr(const char *str, ble_addr_t *out)
{
    unsigned v[6] = {0};
    if (str == NULL || out == NULL) {
        return false;
    }

    if (sscanf(str, "%x:%x:%x:%x:%x:%x",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6 &&
        sscanf(str, "%2x%2x%2x%2x%2x%2x",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
        return false;
    }

    out->type = BLE_ADDR_PUBLIC;
    for (int i = 0; i < 6; i++) {
        out->val[i] = (uint8_t)v[5 - i];
    }
    return true;
}

static void bt_lock(void)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void bt_unlock(void)
{
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

/* ------------------------------------------------------------------ */
/* Log ring buffer                                                    */
/* ------------------------------------------------------------------ */

static void bt_log_v(const char *fmt, va_list ap)
{
    char msg[BT_LOG_LINE_MAX];
    int n = vsnprintf(msg, sizeof(msg), fmt, ap);
    if (n < 0) {
        n = 0;
    }
    if ((unsigned)n >= sizeof(msg)) {
        msg[sizeof(msg) - 1] = '\0';
    }

    char line[BT_LOG_LINE_MAX];
    int prefix = snprintf(line, sizeof(line), "[%llu] ",
                          (unsigned long long)(esp_timer_get_time()
                                               / 1000000ULL));
    if (prefix < 0) {
        prefix = 0;
    }
    if ((unsigned)prefix >= sizeof(line)) {
        prefix = (int)sizeof(line) - 1;
        line[prefix] = '\0';
    }
    unsigned avail = sizeof(line) - (unsigned)prefix - 1;
    unsigned blen = (unsigned)n < avail ? (unsigned)n : avail;
    memcpy(line + prefix, msg, blen);
    line[prefix + blen] = '\0';

    bt_lock();
    unsigned i = s_log_head;
    s_log_head = (s_log_head + 1) % BT_LOG_LINES;
    if (s_log_count < BT_LOG_LINES) {
        s_log_count++;
    }
    bt_copy_str(s_log_lines[i], sizeof(s_log_lines[i]), line);
    bt_unlock();
}

void app_bt_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    bt_log_v(fmt, ap);
    va_end(ap);
}

void app_bt_get_log(char *out, size_t size)
{
    if (out == NULL || size == 0) {
        return;
    }
    out[0] = '\0';

    bt_lock();
    if (s_log_count == 0) {
        bt_unlock();
        return;
    }

    unsigned start = (s_log_head + BT_LOG_LINES - s_log_count) % BT_LOG_LINES;
    size_t written = 0;
    for (unsigned i = 0; i < s_log_count && written + 1 < size; i++) {
        const char *line = s_log_lines[(start + i) % BT_LOG_LINES];
        int n = snprintf(out + written, size - written, "%s\n", line);
        if (n < 0) {
            break;
        }
        if ((size_t)n >= size - written) {
            written = size - 1;
            break;
        }
        written += (size_t)n;
    }
    out[written] = '\0';
    bt_unlock();
}

/* ------------------------------------------------------------------ */
/* Device table                                                       */
/* ------------------------------------------------------------------ */

static void bt_device_add(const ble_addr_t *addr, int8_t rssi,
                          const char *name, bool connectable)
{
    char a[BT_ADDR_MAX];
    bt_addr_str(addr, a, sizeof(a));

    bt_lock();

    int slot = -1;
    for (unsigned i = 0; i < s_device_count; i++) {
        if (strcmp(s_devices[i].addr, a) == 0) {
            slot = (int)i;
            break;
        }
    }

    if (slot < 0) {
        if (s_device_count >= BT_MAX_DEVICES) {
            /* Replace the weakest entry only if this device is stronger. */
            int worst = 0;
            for (unsigned i = 1; i < s_device_count; i++) {
                if (s_devices[i].rssi < s_devices[worst].rssi) {
                    worst = (int)i;
                }
            }
            if (rssi <= s_devices[worst].rssi) {
                bt_unlock();
                return;
            }
            slot = worst;
        } else {
            slot = (int)s_device_count++;
        }
    }

    bt_device_t *d = &s_devices[slot];
    bt_copy_str(d->addr, sizeof(d->addr), a);
    d->rssi = rssi;
    d->addr_type = addr->type;
    d->connectable = connectable;
    if (name != NULL && name[0] != '\0') {
        bt_copy_str(d->name, sizeof(d->name), name);
    } else if (d->name[0] == '\0') {
        bt_copy_str(d->name, sizeof(d->name), "(unnamed)");
    }

    /* Keep the table sorted: strongest RSSI first. */
    for (unsigned i = 0; i + 1 < s_device_count; i++) {
        for (unsigned j = i + 1; j < s_device_count; j++) {
            if (s_devices[j].rssi > s_devices[i].rssi) {
                bt_device_t tmp = s_devices[i];
                s_devices[i] = s_devices[j];
                s_devices[j] = tmp;
            }
        }
    }

    bt_unlock();
}

unsigned app_bt_get_devices(bt_device_t *out, unsigned max)
{
    if (out == NULL || max == 0) {
        return 0;
    }

    bt_lock();
    unsigned n = s_device_count < max ? s_device_count : max;
    memcpy(out, s_devices, n * sizeof(bt_device_t));
    bt_unlock();

    return n;
}

void app_bt_clear_devices(void)
{
    bt_lock();
    s_device_count = 0;
    memset(s_devices, 0, sizeof(s_devices));
    bt_unlock();
}

/* ------------------------------------------------------------------ */
/* GATT service                                                       */
/* ------------------------------------------------------------------ */

/* Nordic UART style service, easy to inspect with any BLE scanner app. */
static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t s_chr_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

static int bt_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        bt_lock();
        int rc = os_mbuf_append(ctxt->om, s_gatt_value,
                                strlen(s_gatt_value));
        bt_unlock();
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        char value[BT_VALUE_MAX];
        uint16_t len = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, value, sizeof(value) - 1,
                                     &len);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        value[len] = '\0';

        bt_lock();
        bt_copy_str(s_gatt_value, sizeof(s_gatt_value), value);
        bt_unlock();

        app_bt_log("gatt write: %s", value);
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_chr_uuid.u,
                .access_cb = bt_gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_gatt_val_handle,
            }, {
                0, /* no more characteristics */
            }
        },
    },
    {
        0, /* no more services */
    },
};

static void bt_gatt_register_cb(struct ble_gatt_register_ctxt *ctxt,
                                void *arg)
{
    (void)arg;

    if (ctxt->op == BLE_GATT_REGISTER_OP_CHR) {
        ESP_LOGI(TAG, "characteristic registered, handle=%d",
                 ctxt->chr.val_handle);
    }
}

static int bt_gatt_svr_init(void)
{
#if CONFIG_BT_NIMBLE_GAP_SERVICE
    ble_svc_gap_init();
#endif
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        return rc;
    }

    return ble_gatts_add_svcs(s_gatt_svcs);
}

/* ------------------------------------------------------------------ */
/* GAP                                                                */
/* ------------------------------------------------------------------ */

static int bt_gap_event(struct ble_gap_event *event, void *arg);

static void bt_advertise(void)
{
    if (!s_host_ready || s_advertising) {
        return;
    }

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t *)s_name;
    fields.name_len = (uint8_t)strlen(s_name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        app_bt_log("adv fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &params, bt_gap_event, NULL);
    if (rc != 0) {
        app_bt_log("adv start rc=%d", rc);
        return;
    }

    s_advertising = true;
    app_bt_log("advertising as \"%s\"", s_name);
}

static int bt_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields fields;
        memset(&fields, 0, sizeof(fields));

        char name[BT_NAME_MAX] = "";
        if (ble_hs_adv_parse_fields(&fields, event->disc.data,
                                    event->disc.length_data) == 0 &&
            fields.name != NULL && fields.name_len > 0) {
            unsigned n = fields.name_len < BT_NAME_MAX - 1
                         ? fields.name_len
                         : BT_NAME_MAX - 1;
            memcpy(name, fields.name, n);
            name[n] = '\0';
        }

        bool connectable =
            event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
            event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;

        bt_device_add(&event->disc.addr, event->disc.rssi, name,
                      connectable);
        return 0;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        s_scanning = false;
        app_bt_log("scan finished (reason %d)", event->disc_complete.reason);
        return 0;

    case BLE_GAP_EVENT_CONNECT: {
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_connected = true;

            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(s_conn_handle, &desc) == 0) {
                bt_addr_str(&desc.peer_id_addr, s_peer_addr,
                            sizeof(s_peer_addr));
            }
            app_bt_log("connected handle=%d peer=%s",
                       (int)s_conn_handle, s_peer_addr);
            ESP_LOGI(TAG, "connected, peer=%s", s_peer_addr);
        } else {
            app_bt_log("connect failed (status %d)", event->connect.status);
            bt_advertise();
        }
        return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT:
        s_connected = false;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        app_bt_log("disconnected (reason %d)", event->disconnect.reason);
        ESP_LOGI(TAG, "disconnected");
        bt_advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        s_advertising = false;
        app_bt_log("advertising stopped (reason %d)",
                   event->adv_complete.reason);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        app_bt_log("client %s notifications",
                   event->subscribe.cur_notify ? "enabled" : "disabled");
        return 0;

    case BLE_GAP_EVENT_MTU:
        app_bt_log("mtu updated: %d", event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle,
                              &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Host lifecycle                                                     */
/* ------------------------------------------------------------------ */

static void bt_on_reset(int reason)
{
    s_host_ready = false;
    s_scanning = false;
    s_advertising = false;
    s_connected = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    app_bt_log("host reset (reason %d)", reason);
    ESP_LOGW(TAG, "host reset, reason=%d", reason);
}

static void bt_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        app_bt_log("no usable address rc=%d", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        app_bt_log("address type rc=%d", rc);
        return;
    }

    uint8_t val[6] = {0};
    ble_hs_id_copy_addr(s_own_addr_type, val, NULL);
    ble_addr_t own = { .type = s_own_addr_type, .val = {0} };
    memcpy(own.val, val, sizeof(own.val));
    bt_addr_str(&own, s_own_addr, sizeof(s_own_addr));

    s_host_ready = true;
    app_bt_log("host ready, address %s", s_own_addr);
    ESP_LOGI(TAG, "host synced, address %s", s_own_addr);

    /* Be discoverable right away, the panel can stop advertising. */
    bt_advertise();
}

static void bt_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_bt_start(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }

    bt_lock();
    s_device_count = 0;
    memset(s_devices, 0, sizeof(s_devices));
    s_log_head = 0;
    s_log_count = 0;
    memset(s_log_lines, 0, sizeof(s_log_lines));
    bt_unlock();

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", ret);
        app_bt_log("nimble init failed (%d)", ret);
        return;
    }

    ble_hs_cfg.reset_cb = bt_on_reset;
    ble_hs_cfg.sync_cb = bt_on_sync;
    ble_hs_cfg.gatts_register_cb = bt_gatt_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;

    if (bt_gatt_svr_init() != 0) {
        ESP_LOGE(TAG, "GATT service registration failed");
        app_bt_log("gatt init failed");
        return;
    }

    ble_svc_gap_device_name_set(s_name);
    ble_store_config_init();

    nimble_port_freertos_init(bt_host_task);
    app_bt_log("nimble host started");
}

bool app_bt_is_ready(void)
{
    return s_host_ready;
}

void app_bt_get_status(bt_status_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    out->host_ready = s_host_ready;
    out->scanning = s_scanning;
    out->advertising = s_advertising;
    out->connected = s_connected;
    out->conn_handle = s_connected ? (int)s_conn_handle : -1;

    bt_lock();
    bt_copy_str(out->name, sizeof(out->name), s_name);
    bt_copy_str(out->own_addr, sizeof(out->own_addr), s_own_addr);
    bt_copy_str(out->peer_addr, sizeof(out->peer_addr), s_peer_addr);
    bt_copy_str(out->gatt_value, sizeof(out->gatt_value), s_gatt_value);
    out->device_count = s_device_count;
    out->log_lines = s_log_count;
    bt_unlock();
}

esp_err_t app_bt_set_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    bt_lock();
    bt_copy_str(s_name, sizeof(s_name), name);
    bt_unlock();

    ble_svc_gap_device_name_set(s_name);
    app_bt_log("device name -> \"%s\"", s_name);

    /* Advertising fields carry the old name, restart if it is running. */
    if (s_advertising) {
        ble_gap_adv_stop();
        s_advertising = false;
        bt_advertise();
    }

    return ESP_OK;
}

esp_err_t app_bt_scan_start(void)
{
    if (!s_host_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_scanning) {
        return ESP_OK;
    }

    struct ble_gap_disc_params params;
    memset(&params, 0, sizeof(params));
    params.passive = 0;
    params.filter_duplicates = 0;   /* keep reporting so RSSI stays fresh */
    params.itvl = 0;
    params.window = 0;

    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &params,
                          bt_gap_event, NULL);
    if (rc != 0) {
        app_bt_log("scan start rc=%d", rc);
        return ESP_FAIL;
    }

    s_scanning = true;
    app_bt_log("scanning for devices...");
    return ESP_OK;
}

void app_bt_scan_stop(void)
{
    if (!s_scanning) {
        return;
    }

    int rc = ble_gap_disc_cancel();
    s_scanning = false;
    app_bt_log("scan stopped (rc=%d)", rc);
}

esp_err_t app_bt_advertise_start(void)
{
    if (!s_host_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    bt_advertise();
    return s_advertising ? ESP_OK : ESP_FAIL;
}

void app_bt_advertise_stop(void)
{
    if (!s_advertising) {
        return;
    }
    ble_gap_adv_stop();
    s_advertising = false;
    app_bt_log("advertising stopped");
}

esp_err_t app_bt_connect(const char *addr_str)
{
    if (!s_host_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_addr_t addr;
    if (!bt_parse_addr(addr_str, &addr)) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Prefer the address type the scanner reported for this device. */
    bt_lock();
    for (unsigned i = 0; i < s_device_count; i++) {
        if (strcmp(s_devices[i].addr, addr_str) == 0) {
            addr.type = s_devices[i].addr_type;
            break;
        }
    }
    bt_unlock();

    if (s_scanning) {
        ble_gap_disc_cancel();
        s_scanning = false;
    }

    int rc = ble_gap_connect(s_own_addr_type, &addr, BT_CONNECT_TIMEOUT_MS,
                             NULL, bt_gap_event, NULL);
    if (rc != 0) {
        app_bt_log("connect rc=%d", rc);
        return ESP_FAIL;
    }

    app_bt_log("connecting to %s...", addr_str);
    return ESP_OK;
}

void app_bt_disconnect(void)
{
    if (!s_connected || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

esp_err_t app_bt_gatt_set_value(const char *value)
{
    bt_lock();
    bt_copy_str(s_gatt_value, sizeof(s_gatt_value), value);
    bt_unlock();

    app_bt_log("gatt value -> %s", s_gatt_value);

    if (s_gatt_val_handle != 0 && s_connected) {
        ble_gatts_chr_updated(s_gatt_val_handle);
    }
    return ESP_OK;
}

void app_bt_gatt_notify(void)
{
    if (s_gatt_val_handle == 0) {
        return;
    }
    if (!s_connected) {
        app_bt_log("notify skipped: no connection");
        return;
    }
    ble_gatts_chr_updated(s_gatt_val_handle);
    app_bt_log("notify: %s", s_gatt_value);
}

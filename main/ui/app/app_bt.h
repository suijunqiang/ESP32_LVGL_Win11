#ifndef APP_BT_H
#define APP_BT_H

/*
 * Bluetooth Low Energy (NimBLE) application.
 *
 * The module runs the NimBLE host in its own FreeRTOS task and exposes the
 * fundamental BLE operations the Bluetooth panel needs:
 *
 *   - GAP observer:  scan for nearby BLE peripherals (name / address / RSSI)
 *   - GAP peripheral: advertise a connectable, discoverable device
 *   - GAP central:   connect to a scanned device, disconnect again
 *   - GATT server:   one custom service with a readable / writable /
 *                    notifiable characteristic
 *
 * GATT operations happen on the NimBLE host task (callback context), the UI
 * only reads snapshots and writes into thread-safe buffers, so every entry
 * point below is safe to call from the LVGL task.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BT_NAME_MAX       32
#define BT_ADDR_MAX       18    /* "aa:bb:cc:dd:ee:ff" + NUL           */
#define BT_MAX_DEVICES    16
#define BT_VALUE_MAX      64    /* GATT characteristic value           */
#define BT_LOG_LINE_MAX   128
#define BT_LOG_LINES      24

/* One device found while scanning. */
typedef struct {
    char addr[BT_ADDR_MAX];
    char name[BT_NAME_MAX];
    int8_t rssi;
    uint8_t addr_type;
    bool connectable;
} bt_device_t;

/* Snapshot of the adapter state. */
typedef struct {
    bool host_ready;      /* NimBLE synced, host can run commands  */
    bool scanning;
    bool advertising;
    bool connected;
    char name[BT_NAME_MAX];   /* local device name               */
    char own_addr[BT_ADDR_MAX];  /* local public address         */
    char peer_addr[BT_ADDR_MAX]; /* connected peer, "" if none   */
    int conn_handle;      /* -1 while not connected               */
    unsigned device_count;/* entries written by app_bt_get_devices */
    unsigned log_lines;
    char gatt_value[BT_VALUE_MAX];
} bt_status_t;

/* Initialize the controller and the NimBLE host. Call once from app_main. */
void app_bt_start(void);

/* True once the host synced and GAP/GATT can be used. */
bool app_bt_is_ready(void);

/* Take a snapshot of the adapter state. */
void app_bt_get_status(bt_status_t *out);

/* Change the advertised / GAP device name. */
esp_err_t app_bt_set_name(const char *name);

/* Copy the scan results into `out` (sorted by RSSI, strongest first).
 * Returns the number of entries written. */
unsigned app_bt_get_devices(bt_device_t *out, unsigned max);

/* Forget the current scan results. */
void app_bt_clear_devices(void);

/* Start / stop an active GAP scan. Scanning is asynchronous: new devices
 * show up in app_bt_get_devices() as the controller reports them. */
esp_err_t app_bt_scan_start(void);
void app_bt_scan_stop(void);

/* Start / stop connectable advertising (the board becomes discoverable). */
esp_err_t app_bt_advertise_start(void);
void app_bt_advertise_stop(void);

/* Connect to a device by its "aa:bb:cc:dd:ee:ff" address. */
esp_err_t app_bt_connect(const char *addr);

/* Terminate the current connection (no-op while disconnected). */
void app_bt_disconnect(void);

/* Write the GATT characteristic value and notify subscribers. */
esp_err_t app_bt_gatt_set_value(const char *value);

/* Re-send the current value to every subscribed client. */
void app_bt_gatt_notify(void);

/* Copy the most recent log lines into `out` (newline separated). */
void app_bt_get_log(char *out, size_t size);

/* Append one line to the log ring buffer. Safe from any task. */
void app_bt_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* APP_BT_H */

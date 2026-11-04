/*
 * The application catalogue of the Windows 11 demo.
 *
 * See ui_win_icons.h for the description of the tables.
 */

#include "ui_win_icons.h"
#include "lv_i18n.h"

/* Each `label` here is both the EN fallback text and the lv_i18n msg_id.
 * The actual translation is resolved at the call site via `_(app->label)`. */
const win_app_t win_apps[WIN_APP_COUNT] = {
    { WIN_APP_EDGE,      LV_SYMBOL_GPS,        "icon_edge",     LV_COLOR_MAKE(0x0F, 0x7C, 0xBF) },
    { WIN_APP_EXPLORER,  LV_SYMBOL_DIRECTORY,  "icon_explorer", LV_COLOR_MAKE(0xF2, 0xA3, 0x3C) },
    { WIN_APP_SETTINGS,  LV_SYMBOL_SETTINGS,   "icon_settings", LV_COLOR_MAKE(0x6E, 0x7B, 0x8B) },
    { WIN_APP_STORE,     LV_SYMBOL_DOWNLOAD,   "icon_store",    LV_COLOR_MAKE(0x7B, 0x3F, 0xE4) },
    { WIN_APP_NOTEPAD,   LV_SYMBOL_LIST,       "icon_notepad",  LV_COLOR_MAKE(0x2F, 0x7B, 0xD9) },
    { WIN_APP_MAIL,      LV_SYMBOL_ENVELOPE,   "icon_mail",     LV_COLOR_MAKE(0x0A, 0x64, 0xC8) },
    { WIN_APP_MUSIC,     LV_SYMBOL_AUDIO,      "icon_media",    LV_COLOR_MAKE(0xE8, 0x44, 0x7A) },
    { WIN_APP_PHOTOS,    LV_SYMBOL_IMAGE,      "icon_photos",   LV_COLOR_MAKE(0x1B, 0xA3, 0x7B) },
    { WIN_APP_TERMINAL,  LV_SYMBOL_KEYBOARD,   "icon_terminal", LV_COLOR_MAKE(0x3C, 0x3C, 0x46) },
    { WIN_APP_WIFI,      LV_SYMBOL_WIFI,       "icon_wifi",     LV_COLOR_MAKE(0x17, 0xA2, 0xB8) },
    { WIN_APP_MQTT,      "MQ",                 "icon_mqtt",     LV_COLOR_MAKE(0x8C, 0x5C, 0xF7) },
    { WIN_APP_BT,        LV_SYMBOL_BLUETOOTH,  "icon_bt",       LV_COLOR_MAKE(0x1B, 0x6F, 0xE8) },
    { WIN_APP_CAM,       LV_SYMBOL_IMAGE,      "icon_camera",   LV_COLOR_MAKE(0xE8, 0x9F, 0x1B) },
    { WIN_APP_UI,        LV_SYMBOL_PLAY,       "icon_ui",       LV_COLOR_MAKE(0xD9, 0x46, 0xEF) },
};

const win_app_id_t win_taskbar_apps[] = {
    WIN_APP_EDGE,
    WIN_APP_WIFI,
    WIN_APP_MQTT,
    WIN_APP_BT,
    WIN_APP_CAM,
    WIN_APP_STORE,
};

const unsigned win_taskbar_app_count =
    sizeof(win_taskbar_apps) / sizeof(win_taskbar_apps[0]);

const win_app_id_t win_start_pinned[] = {
    WIN_APP_EDGE,
    WIN_APP_EXPLORER,
    WIN_APP_WIFI,
    WIN_APP_MQTT,
    WIN_APP_BT,
    WIN_APP_CAM,
    WIN_APP_SETTINGS,
    WIN_APP_NOTEPAD,
    WIN_APP_MAIL,
};

const unsigned win_start_pinned_count =
    sizeof(win_start_pinned) / sizeof(win_start_pinned[0]);

const win_shortcut_t win_desktop_shortcuts[] = {
    { LV_SYMBOL_DRIVE,     "shortcut_thispc",   LV_COLOR_MAKE(0x4F, 0x8C, 0xFF), WIN_APP_EXPLORER },
    { LV_SYMBOL_TRASH,     "shortcut_recycle",  LV_COLOR_MAKE(0x8A, 0xA0, 0xB4), WIN_APP_EXPLORER },
    { LV_SYMBOL_DIRECTORY, "shortcut_doc",      LV_COLOR_MAKE(0xF2, 0xA3, 0x3C), WIN_APP_EXPLORER },
    { LV_SYMBOL_FILE,      "shortcut_readme",   LV_COLOR_MAKE(0x9A, 0xA5, 0xB1), WIN_APP_NOTEPAD },
    { LV_SYMBOL_AUDIO,     "shortcut_media",    LV_COLOR_MAKE(0xE8, 0x44, 0x7A), WIN_APP_MUSIC },
    { LV_SYMBOL_IMAGE,     "shortcut_photos",   LV_COLOR_MAKE(0x1B, 0xA3, 0x7B), WIN_APP_PHOTOS },
    { LV_SYMBOL_KEYBOARD,  "shortcut_terminal", LV_COLOR_MAKE(0x3C, 0x3C, 0x46), WIN_APP_TERMINAL },
    { LV_SYMBOL_WIFI,      "shortcut_wifi",     LV_COLOR_MAKE(0x17, 0xA2, 0xB8), WIN_APP_WIFI },
    { LV_SYMBOL_SHUFFLE,   "shortcut_mqtt",     LV_COLOR_MAKE(0x8C, 0x5C, 0xF7), WIN_APP_MQTT },
    { LV_SYMBOL_BLUETOOTH, "shortcut_bt",       LV_COLOR_MAKE(0x1B, 0x6F, 0xE8), WIN_APP_BT },
    { LV_SYMBOL_IMAGE,     "shortcut_cam",      LV_COLOR_MAKE(0xE8, 0x9F, 0x1B), WIN_APP_CAM },
    { LV_SYMBOL_PLAY,      "shortcut_ui",       LV_COLOR_MAKE(0xD9, 0x46, 0xEF), WIN_APP_UI },
};

const unsigned win_desktop_shortcut_count =
    sizeof(win_desktop_shortcuts) / sizeof(win_desktop_shortcuts[0]);

const win_app_t *win_app_get(win_app_id_t id)
{
    if (id < 0 || id >= WIN_APP_COUNT) {
        return &win_apps[0];
    }

    return &win_apps[id];
}

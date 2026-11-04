/*
 * The application catalogue of the Windows 11 demo.
 *
 * See ui_win_icons.h for the description of the tables.
 */

#include "ui_win_icons.h"

const win_app_t win_apps[WIN_APP_COUNT] = {
    { WIN_APP_EDGE,      LV_SYMBOL_GPS,       "Edge",       LV_COLOR_MAKE(0x0F, 0x7C, 0xBF) },
    { WIN_APP_EXPLORER,  LV_SYMBOL_DIRECTORY, "Explorer",   LV_COLOR_MAKE(0xF2, 0xA3, 0x3C) },
    { WIN_APP_SETTINGS,  LV_SYMBOL_SETTINGS,  "Settings",   LV_COLOR_MAKE(0x6E, 0x7B, 0x8B) },
    { WIN_APP_STORE,     LV_SYMBOL_DOWNLOAD,  "Store",      LV_COLOR_MAKE(0x7B, 0x3F, 0xE4) },
    { WIN_APP_NOTEPAD,   LV_SYMBOL_LIST,      "Notepad",    LV_COLOR_MAKE(0x2F, 0x7B, 0xD9) },
    { WIN_APP_MAIL,      LV_SYMBOL_ENVELOPE,  "Mail",       LV_COLOR_MAKE(0x0A, 0x64, 0xC8) },
    { WIN_APP_MUSIC,     LV_SYMBOL_AUDIO,     "Media",      LV_COLOR_MAKE(0xE8, 0x44, 0x7A) },
    { WIN_APP_PHOTOS,    LV_SYMBOL_IMAGE,     "Photos",     LV_COLOR_MAKE(0x1B, 0xA3, 0x7B) },
    { WIN_APP_TERMINAL,  LV_SYMBOL_KEYBOARD,  "Terminal",   LV_COLOR_MAKE(0x3C, 0x3C, 0x46) },
    { WIN_APP_WIFI,      LV_SYMBOL_WIFI,      "Wi-Fi",      LV_COLOR_MAKE(0x17, 0xA2, 0xB8) },
    { WIN_APP_MQTT,      "MQ",                "MQTT",       LV_COLOR_MAKE(0x8C, 0x5C, 0xF7) },
    { WIN_APP_BT,        LV_SYMBOL_BLUETOOTH,  "Bluetooth",  LV_COLOR_MAKE(0x1B, 0x6F, 0xE8) },
    { WIN_APP_CAM,       LV_SYMBOL_IMAGE,      "Camera",     LV_COLOR_MAKE(0xE8, 0x9F, 0x1B) },
    { WIN_APP_UI,        LV_SYMBOL_PLAY,       "UI",         LV_COLOR_MAKE(0xD9, 0x46, 0xEF) },
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
    { LV_SYMBOL_DRIVE,     "ThisPC",     LV_COLOR_MAKE(0x4F, 0x8C, 0xFF), WIN_APP_EXPLORER },
    { LV_SYMBOL_TRASH,     "Recycle", LV_COLOR_MAKE(0x8A, 0xA0, 0xB4), WIN_APP_EXPLORER },
    { LV_SYMBOL_DIRECTORY, "Doc",   LV_COLOR_MAKE(0xF2, 0xA3, 0x3C), WIN_APP_EXPLORER },
    { LV_SYMBOL_FILE,      "Readme",  LV_COLOR_MAKE(0x9A, 0xA5, 0xB1), WIN_APP_NOTEPAD },
    { LV_SYMBOL_AUDIO,     "Media",       LV_COLOR_MAKE(0xE8, 0x44, 0x7A), WIN_APP_MUSIC },
    { LV_SYMBOL_IMAGE,     "Photos",      LV_COLOR_MAKE(0x1B, 0xA3, 0x7B), WIN_APP_PHOTOS },
    { LV_SYMBOL_KEYBOARD,  "Terminal",    LV_COLOR_MAKE(0x3C, 0x3C, 0x46), WIN_APP_TERMINAL },
    { LV_SYMBOL_WIFI,      "Wi-Fi",       LV_COLOR_MAKE(0x17, 0xA2, 0xB8), WIN_APP_WIFI },
    { LV_SYMBOL_SHUFFLE,   "MQTT",        LV_COLOR_MAKE(0x8C, 0x5C, 0xF7), WIN_APP_MQTT },
    { LV_SYMBOL_BLUETOOTH, "BT",          LV_COLOR_MAKE(0x1B, 0x6F, 0xE8), WIN_APP_BT },
    { LV_SYMBOL_IMAGE,     "Cam",         LV_COLOR_MAKE(0xE8, 0x9F, 0x1B), WIN_APP_CAM },
    { LV_SYMBOL_PLAY,      "UI",          LV_COLOR_MAKE(0xD9, 0x46, 0xEF), WIN_APP_UI },
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

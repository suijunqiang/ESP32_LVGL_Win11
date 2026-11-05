#include "./lv_i18n.h"


////////////////////////////////////////////////////////////////////////////////
// Define plural operands
// http://unicode.org/reports/tr35/tr35-numbers.html#Operands

// Integer version, simplified

#define UNUSED(x) (void)(x)

static inline uint32_t op_n(int32_t val) { return (uint32_t)(val < 0 ? -val : val); }
static inline uint32_t op_i(uint32_t val) { return val; }
// always zero, when decimal part not exists.
static inline uint32_t op_v(uint32_t val) { UNUSED(val); return 0;}
static inline uint32_t op_w(uint32_t val) { UNUSED(val); return 0; }
static inline uint32_t op_f(uint32_t val) { UNUSED(val); return 0; }
static inline uint32_t op_t(uint32_t val) { UNUSED(val); return 0; }

static lv_i18n_phrase_t en_us_singulars[] = {
    {"start", "Start"},
    {"apps", "Apps"},
    {"restart", "Restart"},
    {"sub", "Sub"},
    {"pub", "Pub"},
    {"devices", "Devices"},
    {"gatt", "GATT"},
    {"log", "Log"},
    {"view", "View"},
    {"settings_tab", "Settings"},
    {"capture", "Capture"},
    {"auto", "Auto"},
    {"frame_size", "frame size"},
    {"swap", "Swap"},
    {"test", "Test"},
    {"reinit", "Reinit"},
    {"scan", "Scan"},
    {"adv", "Adv"},
    {"disc", "Disc"},
    {"clear", "Clear"},
    {"set", "Set"},
    {"notify", "Notify"},
    {"addr_label", "addr %s"},
    {"peer_label", "peer %s"},
    {"addr_peer_fmt", "addr %s\npeer %s"},
    {"val_label", "val: %s"},
    {"not_connected", "not connected"},
    {"ssid_empty", "SSID empty"},
    {"hidden", "(hidden)"},
    {"no_ap", "no AP"},
    {"reconnecting", "reconnecting to \"%s\"..."},
    {"connect", "Connect"},
    {"press_scan_ap", "press Scan to list APs"},
    {"press_scan_dev", "press Scan to look for devices"},
    {"uptime", "uptime %ds"},
    {"mqtt_published", "published #%d to %s"},
    {"mqtt_subscribed", "subscribed"},
    {"mqtt_sub_fail", "subscribe failed"},
    {"mqtt_restarting", "restarting..."},
    {"bt_starting", "adapter starting"},
    {"bt_linked", "linked %s"},
    {"bt_ready", "ready  adv:%s  scan:%s  %d dev"},
    {"on", "on"},
    {"off", "off"},
    {"dbm", "%d dBm"},
    {"dash", "---"},
    {"temp_label", "22"},
    {"placeholder_dotdotdot", "..."},
    {"start_search", "Search apps and settings"},
    {"pinned", "Pinned"},
    {"recommended", "Recommended"},
    {"bt_label", "Bluetooth"},
    {"wifi_label", "Wi-Fi"},
    {"night_light", "Night light"},
    {"airplane_mode", "Airplane mode"},
    {"notepad_doc", "Documents\nDownloads\nPictures\nProjects"},
    {"notepad_line_prompt", "C:\\Users\\Sui> "},
    {"website", "https://lvgl.io"},
    {"lvgl_tagline_1", "Light and Versatile"},
    {"lvgl_tagline_2", "Embedded graphics library"},
    {"lvgl_tagline_3", "for MCU and MPU"},
    {"win11_theme", "Windows 11 theme"},
    {"user_name", "Sui Junqiang"},
    {"time_1_12", "1:12"},
    {"time_3_04", "3:04"},
    {"prompt_ls", "ls"},
    {"prompt_cursor", "_"},
    {"app_open", "Open"},
    {"digest_1", "Windows 11 design guide"},
    {"digest_2", "Your weekly digest"},
    {"cam_not_ready", "camera not ready: %s"},
    {"cam_test_pattern", "test pattern  %dx%d  frames=%d"},
    {"cam_live", "live  %dx%d  swap:%s  frames=%d"},
    {"item_music", "Music"},
    {"item_music_sub", "Now playing"},
    {"item_photos", "Photos"},
    {"item_photos_sub", "Memories"},
    {"item_chat", "Chat"},
    {"item_chat_sub", "Messages"},
    {"item_home", "Home"},
    {"item_home_sub", "Control"},
    {"item_map", "Map"},
    {"item_map_sub", "Navigate"},
    {"item_store", "Store"},
    {"item_store_sub", "Apps"},
    {"item_battery", "Battery"},
    {"item_battery_sub", "Power"},
    {"item_settings", "Settings"},
    {"item_settings_sub", "System"},
    {"planet_horizon", "HORIZON"},
    {"planet_horizon_sub", "last stable orbit"},
    {"planet_inner", "INNER"},
    {"planet_inner_sub", "spiral dust arm"},
    {"planet_ember", "EMBER"},
    {"planet_ember_sub", "warm gas giant"},
    {"planet_glyph", "GLYPH"},
    {"planet_glyph_sub", "ancient signal"},
    {"planet_moss", "MOSS"},
    {"planet_moss_sub", "life-bearing rock"},
    {"planet_void", "VOID"},
    {"planet_void_sub", "dark sibling"},
    {"planet_ember2", "EMBER II"},
    {"planet_ember2_sub", "second sun"},
    {"planet_horizon2", "HORIZON II"},
    {"planet_horizon2_sub", "far rim"},
    {"icon_edge", "Edge"},
    {"icon_explorer", "Explorer"},
    {"icon_settings", "Settings"},
    {"icon_store", "Store"},
    {"icon_notepad", "Notepad"},
    {"icon_mail", "Mail"},
    {"icon_media", "Media"},
    {"icon_photos", "Photos"},
    {"icon_terminal", "Terminal"},
    {"icon_wifi", "Wi-Fi"},
    {"icon_mqtt", "MQTT"},
    {"icon_bt", "Bluetooth"},
    {"icon_camera", "Camera"},
    {"icon_ui", "UI"},
    {"shortcut_thispc", "ThisPC"},
    {"shortcut_recycle", "Recycle"},
    {"shortcut_doc", "Doc"},
    {"shortcut_readme", "Readme"},
    {"shortcut_media", "Media"},
    {"shortcut_photos", "Photos"},
    {"shortcut_terminal", "Terminal"},
    {"shortcut_wifi", "Wi-Fi"},
    {"shortcut_mqtt", "MQTT"},
    {"shortcut_bt", "BT"},
    {"shortcut_cam", "Cam"},
    {"shortcut_ui", "UI"},
    {"user_initials", "S"},
    {"lang_toggle_en", "ENG"},
    {"lang_toggle_zh", "中文"},
    {"lang_en", "English"},
    {"lang_zh", "简体中文"},
    {"dot_breathe", "BREATHE"},
    {"dot_breathe_sub", "a sphere that inhales and exhales"},
    {"dot_scan", "SCAN"},
    {"dot_scan_sub", "a bright band sweeping around"},
    {"dot_spin", "SPIN"},
    {"dot_spin_sub", "the globe turns on its axis"},
    {"dot_swirl", "SWIRL"},
    {"dot_swirl_sub", "bands rotating at different speeds"},
    {"dot_lattice", "LATTICE"},
    {"dot_lattice_sub", "the six faces of a cube"},
    {"dot_shells", "SHELLS"},
    {"dot_shells_sub", "two nested spheres, counter rotating"},
    {"dot_orbit", "ORBIT"},
    {"dot_orbit_sub", "four tilted rings"},
    {"dot_weave", "WEAVE"},
    {"dot_weave_sub", "meridians and parallels"},
    {"dot_shape", "SHAPE"},
    {"dot_shape_sub", "sphere, cube and torus in turn"},
    {"mic_title", "MIC"},
    {"mic_meta", "44.1k / 48k"},
    {"mic_input", "INPUT LEVEL"},
    {NULL, NULL} // End mark
};



static uint8_t en_us_plural_fn(int32_t num)
{
    uint32_t n = op_n(num); UNUSED(n);
    uint32_t i = op_i(n); UNUSED(i);
    uint32_t v = op_v(n); UNUSED(v);

    if ((i == 1 && v == 0)) return LV_I18N_PLURAL_TYPE_ONE;
    return LV_I18N_PLURAL_TYPE_OTHER;
}

static const lv_i18n_lang_t en_us_lang = {
    .locale_name = "en-US",
    .singulars = en_us_singulars,

    .locale_plural_fn = en_us_plural_fn
};

static lv_i18n_phrase_t zh_cn_singulars[] = {
    {"start", "开始"},
    {"apps", "应用"},
    {"restart", "重启"},
    {"sub", "订阅"},
    {"pub", "发布"},
    {"devices", "设备"},
    {"gatt", "GATT"},
    {"log", "日志"},
    {"view", "画面"},
    {"settings_tab", "设置"},
    {"capture", "拍摄"},
    {"auto", "自动"},
    {"frame_size", "分辨率"},
    {"swap", "交换"},
    {"test", "测试"},
    {"reinit", "重初始化"},
    {"scan", "扫描"},
    {"adv", "广播"},
    {"disc", "发现"},
    {"clear", "清除"},
    {"set", "设置"},
    {"notify", "通知"},
    {"addr_label", "地址 %s"},
    {"peer_label", "对端 %s"},
    {"addr_peer_fmt", "地址 %s\n对端 %s"},
    {"val_label", "值: %s"},
    {"not_connected", "未连接"},
    {"ssid_empty", "SSID 为空"},
    {"hidden", "(隐藏)"},
    {"no_ap", "无热点"},
    {"reconnecting", "正在重连 \"%s\"..."},
    {"connect", "连接"},
    {"press_scan_ap", "点扫描列出热点"},
    {"press_scan_dev", "点扫描查找设备"},
    {"uptime", "运行 %ds"},
    {"mqtt_published", "已发布 #%d 到 %s"},
    {"mqtt_subscribed", "已订阅"},
    {"mqtt_sub_fail", "订阅失败"},
    {"mqtt_restarting", "正在重启..."},
    {"bt_starting", "适配器启动中"},
    {"bt_linked", "已连接 %s"},
    {"bt_ready", "就绪 广播:%s 扫描:%s %d 设备"},
    {"on", "开"},
    {"off", "关"},
    {"dbm", "%d dBm"},
    {"dash", "---"},
    {"temp_label", "22"},
    {"placeholder_dotdotdot", "..."},
    {"start_search", "搜索应用和设置"},
    {"pinned", "已固定"},
    {"recommended", "推荐"},
    {"bt_label", "蓝牙"},
    {"wifi_label", "Wi-Fi"},
    {"night_light", "夜间模式"},
    {"airplane_mode", "飞行模式"},
    {"notepad_doc", "文档\n下载\n图片\n项目"},
    {"notepad_line_prompt", "C:\\用户\\Sui> "},
    {"website", "https://lvgl.io"},
    {"lvgl_tagline_1", "轻量 且 灵活"},
    {"lvgl_tagline_2", "嵌入式图形库"},
    {"lvgl_tagline_3", "适用于 MCU 和 MPU"},
    {"win11_theme", "Windows 11 主题"},
    {"user_name", "隋军强"},
    {"time_1_12", "1:12"},
    {"time_3_04", "3:04"},
    {"prompt_ls", "ls"},
    {"prompt_cursor", "_"},
    {"app_open", "打开"},
    {"digest_1", "Windows 11 设计指南"},
    {"digest_2", "每周精选"},
    {"cam_not_ready", "摄像头未就绪: %s"},
    {"cam_test_pattern", "测试图  %dx%d  帧=%d"},
    {"cam_live", "实时  %dx%d  交换:%s  帧=%d"},
    {"item_music", "音乐"},
    {"item_music_sub", "正在播放"},
    {"item_photos", "照片"},
    {"item_photos_sub", "回忆"},
    {"item_chat", "聊天"},
    {"item_chat_sub", "消息"},
    {"item_home", "家庭"},
    {"item_home_sub", "控制"},
    {"item_map", "地图"},
    {"item_map_sub", "导航"},
    {"item_store", "商店"},
    {"item_store_sub", "应用"},
    {"item_battery", "电量"},
    {"item_battery_sub", "电源"},
    {"item_settings", "设置"},
    {"item_settings_sub", "系统"},
    {"planet_horizon", "视界"},
    {"planet_horizon_sub", "最后的稳定轨道"},
    {"planet_inner", "内层"},
    {"planet_inner_sub", "旋臂尘埃"},
    {"planet_ember", "余烬"},
    {"planet_ember_sub", "温暖的气态巨星"},
    {"planet_glyph", "符印"},
    {"planet_glyph_sub", "古老信号"},
    {"planet_moss", "苔青"},
    {"planet_moss_sub", "孕育生命的岩石"},
    {"planet_void", "虚空"},
    {"planet_void_sub", "黑暗的兄弟"},
    {"planet_ember2", "余烬 II"},
    {"planet_ember2_sub", "第二颗太阳"},
    {"planet_horizon2", "视界 II"},
    {"planet_horizon2_sub", "远方之环"},
    {"icon_edge", "浏览器"},
    {"icon_explorer", "文件"},
    {"icon_settings", "设置"},
    {"icon_store", "商店"},
    {"icon_notepad", "记事本"},
    {"icon_mail", "邮件"},
    {"icon_media", "媒体"},
    {"icon_photos", "图片"},
    {"icon_terminal", "终端"},
    {"icon_wifi", "Wi-Fi"},
    {"icon_mqtt", "MQTT"},
    {"icon_bt", "蓝牙"},
    {"icon_camera", "相机"},
    {"icon_ui", "UI"},
    {"shortcut_thispc", "此电脑"},
    {"shortcut_recycle", "回收站"},
    {"shortcut_doc", "文档"},
    {"shortcut_readme", "自述"},
    {"shortcut_media", "媒体"},
    {"shortcut_photos", "图片"},
    {"shortcut_terminal", "终端"},
    {"shortcut_wifi", "Wi-Fi"},
    {"shortcut_mqtt", "MQTT"},
    {"shortcut_bt", "蓝牙"},
    {"shortcut_cam", "相机"},
    {"shortcut_ui", "UI"},
    {"user_initials", "隋"},
    {"lang_toggle_en", "ENG"},
    {"lang_toggle_zh", "中文"},
    {"lang_en", "English"},
    {"lang_zh", "简体中文"},
    {"dot_breathe", "呼吸"},
    {"dot_breathe_sub", "球面随节奏胀缩"},
    {"dot_scan", "扫描"},
    {"dot_scan_sub", "亮带绕球面扫过"},
    {"dot_spin", "自转"},
    {"dot_spin_sub", "球体绕轴转动"},
    {"dot_swirl", "旋涡"},
    {"dot_swirl_sub", "分层反向旋转"},
    {"dot_lattice", "晶格"},
    {"dot_lattice_sub", "立方体的六个面"},
    {"dot_shells", "双壳"},
    {"dot_shells_sub", "内外球反向旋转"},
    {"dot_orbit", "轨道"},
    {"dot_orbit_sub", "四个倾斜的圆环"},
    {"dot_weave", "编织"},
    {"dot_weave_sub", "经线与纬线交织"},
    {"dot_shape", "变形"},
    {"dot_shape_sub", "球体、立方体与圆环"},
    {"mic_title", "麦克风"},
    {"mic_meta", "44.1k / 48k"},
    {"mic_input", "输入电平"},
    {NULL, NULL} // End mark
};



static uint8_t zh_cn_plural_fn(int32_t num)
{



    return LV_I18N_PLURAL_TYPE_OTHER;
}

static const lv_i18n_lang_t zh_cn_lang = {
    .locale_name = "zh-CN",
    .singulars = zh_cn_singulars,

    .locale_plural_fn = zh_cn_plural_fn
};

const lv_i18n_language_pack_t lv_i18n_language_pack[] = {
    &en_us_lang,
    &zh_cn_lang,
    NULL // End mark
};

////////////////////////////////////////////////////////////////////////////////


// Internal state
static const lv_i18n_language_pack_t * current_lang_pack;
static const lv_i18n_lang_t * current_lang;


/**
 * Reset internal state. For testing.
 */
void __lv_i18n_reset(void)
{
    current_lang_pack = NULL;
    current_lang = NULL;
}

/**
 * Set the languages for internationalization
 * @param langs pointer to the array of languages. (Last element has to be `NULL`)
 */
int lv_i18n_init(const lv_i18n_language_pack_t * langs)
{
    if(langs == NULL) return -1;
    if(langs[0] == NULL) return -1;

    current_lang_pack = langs;
    current_lang = langs[0];     /*Automatically select the first language*/
    return 0;
}

/**
 * Change the localization (language)
 * @param l_name name of the translation locale to use. E.g. "en-GB"
 */
int lv_i18n_set_locale(const char * l_name)
{
    if(current_lang_pack == NULL) return -1;

    uint16_t i;

    for(i = 0; current_lang_pack[i] != NULL; i++) {
        // Found -> finish
        if(strcmp(current_lang_pack[i]->locale_name, l_name) == 0) {
            current_lang = current_lang_pack[i];
            return 0;
        }
    }

    return -1;
}


static const char * __lv_i18n_get_text_core(lv_i18n_phrase_t * trans, const char * msg_id)
{
    uint16_t i;
    for(i = 0; trans[i].msg_id != NULL; i++) {
        if(strcmp(trans[i].msg_id, msg_id) == 0) {
            /*The msg_id has found. Check the translation*/
            if(trans[i].translation) return trans[i].translation;
        }
    }

    return NULL;
}


/**
 * Get the translation from a message ID
 * @param msg_id message ID
 * @return the translation of `msg_id` on the set local
 */
const char * lv_i18n_get_text(const char * msg_id)
{
    if(current_lang == NULL) return msg_id;

    const lv_i18n_lang_t * lang = current_lang;
    const void * txt;

    // Search in current locale
    if(lang->singulars != NULL) {
        txt = __lv_i18n_get_text_core(lang->singulars, msg_id);
        if (txt != NULL) return txt;
    }

    // Try to fallback
    if(lang == current_lang_pack[0]) return msg_id;
    lang = current_lang_pack[0];

    // Repeat search for default locale
    if(lang->singulars != NULL) {
        txt = __lv_i18n_get_text_core(lang->singulars, msg_id);
        if (txt != NULL) return txt;
    }

    return msg_id;
}

/**
 * Get the translation from a message ID and apply the language's plural rule to get correct form
 * @param msg_id message ID
 * @param num an integer to select the correct plural form
 * @return the translation of `msg_id` on the set local
 */
const char * lv_i18n_get_text_plural(const char * msg_id, int32_t num)
{
    if(current_lang == NULL) return msg_id;

    const lv_i18n_lang_t * lang = current_lang;
    const void * txt;
    lv_i18n_plural_type_t ptype;

    // Search in current locale
    if(lang->locale_plural_fn != NULL) {
        ptype = lang->locale_plural_fn(num);

        if(lang->plurals[ptype] != NULL) {
            txt = __lv_i18n_get_text_core(lang->plurals[ptype], msg_id);
            if (txt != NULL) return txt;
        }
    }

    // Try to fallback
    if(lang == current_lang_pack[0]) return msg_id;
    lang = current_lang_pack[0];

    // Repeat search for default locale
    if(lang->locale_plural_fn != NULL) {
        ptype = lang->locale_plural_fn(num);

        if(lang->plurals[ptype] != NULL) {
            txt = __lv_i18n_get_text_core(lang->plurals[ptype], msg_id);
            if (txt != NULL) return txt;
        }
    }

    return msg_id;
}

/**
 * Get the name of the currently used locale.
 * @return name of the currently used locale. E.g. "en-GB"
 */
const char * lv_i18n_get_current_locale(void)
{
    if(!current_lang) return NULL;
    return current_lang->locale_name;
}

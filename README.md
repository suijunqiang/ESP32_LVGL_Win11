# ESP32-S3 Windows 11 Desktop (LVGL 9.6)

![Demo](http://suijunqiang.top:10000/img/lvgl/esp32320240.gif)

A Windows 11 style desktop running on an ESP32-S3 with an ILI9341 320×240
SPI panel and an XPT2046 touch controller. The UI is built with LVGL 9 and
runs inside a dedicated FreeRTOS task; Wi-Fi, SNTP and MQTT run independently
so the render loop is never blocked by the network.

## Features

**Windows 11 desktop**
- Gradient wallpaper, draggable desktop icons, Explorer / Photos / Terminal /
  Notepad tiles.
- Start menu overlay, taskbar with Start button, an **Apps** dropdown, a
  **language dropdown** (`Eng` / `简体`) and a notifications tray
  (Wi-Fi / MQTT state, clock), plus a weather widget.
- Multi-window manager with open / hide / show / close lifecycle.

**Multi-language UI (English / 简体中文)**
- English is the boot default; pick `简体` from the taskbar dropdown to
  switch at runtime. The desktop is rebuilt with the new strings and fonts,
  so every screen, menu and app window follows the choice.
- Strings live in YAML (`translations/en-US.yml`, `translations/zh-CN.yml`)
  and are compiled to C by `lv_i18n`; code uses gettext-style `_("key")`.
- Chinese uses generated Noto Sans SC subsets (14 px / 16 px) because LVGL's
  built-in Source Han Sans CJK fonts only carry ~1,200 hanzi — too few for
  the UI labels. The subsets fall back to the built-in font for
  `LV_SYMBOL_*` glyphs, so icons keep rendering.

**Apps page — a left-tab LVGL tabview with three interactive pages**

1. **Honeycomb watch (tab 1).** A circular grid of app tiles that rotates
   like a watch bezel. Flick gestures continue spinning with inertia; the
   centre tile is always the active app and can be tapped to launch.
2. **Warmhole galaxy (tab 2).** A particle-driven galaxy with a black hole
   core, orbiting planets and gold / blue inflow particles. Drag the ring
   left or right, or tap a planet — the selected planet's name and subtitle
   appear at the top and the gold dot highlights it at the bottom.
3. **Silk coverflow (tab 3).** A fanned coverflow of 8 menu cards (Music,
   Photos, Chat, Home, Map, Store, Battery, Settings) arranged on a
   horizontal arc with vertical falloff and outward-angled side cards.
   Scrolling is smooth, drags start inside the cards, and tap-to-select
   snaps to the new front card.

**Infrastructure**
- Touch input with calibrated XPT2046 mapping.
- Wi-Fi station + SNTP time sync (`Asia/Shanghai`).
- MQTT: status heartbeat every 10 s, remote command topic.
- On-demand LVGL screenshot streamed over UART as PPM6.
- View/window manager with a full six-phase lifecycle
  (`init`/`open`/`pause`/`resume`/`close`/`deinit`) so hidden views release
  RAM and stop their animations.

## Localization

LVGL 9.6 ships `lv_translation`, but it is off by default, undocumented and
not the recommended path, so the project uses the companion tool
[lv_i18n](https://github.com/lvgl/lv_i18n) instead.

| Piece | File |
| --- | --- |
| Source strings (English, the msg-id catalogue) | `translations/en-US.yml` |
| Chinese translations | `translations/zh-CN.yml` |
| Generated lookup tables | `main/ui/i18n/lv_i18n.c/.h` |
| Locale + font switching wrapper | `main/ui/i18n/app_i18n.c/.h` |
| Chinese glyph data | `main/ui/i18n/lv_font_zh_14.c`, `lv_font_zh_16.c` |

Code marks user-visible text with `_("key")`. `_()` is a function call, so it
cannot appear in `static const` initialisers — tables store the key strings and
call `_()` at each use site. `app_i18n_set_locale()` swaps the `WIN_FONT_*`
pointers (Montserrat for English, Noto Sans SC for Chinese) and the taskbar
dropdown rebuilds the desktop through `ui_win_desktop_deinit()` +
`ui_win_desktop_open()`, deferred with `lv_async_call()` so the dropdown is
not deleted inside its own event callback.

The generated translation tables and Chinese fonts are committed, so a normal
build only needs ESP-IDF. Regenerating them needs Node.js:

```bash
# 0. Extract the SC face (index 2) from Noto Sans CJK and collect the
#    characters used by the Chinese translations.
python3 - <<'EOF'
from fontTools.ttLib import TTCollection
TTCollection('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc') \
    .fonts[2].save('/tmp/NotoSansSC-Regular.otf')
txt = open('translations/zh-CN.yml').read()
open('/tmp/zh_chars.txt', 'w').write(
    ''.join(sorted({c for c in txt if ord(c) > 127})))
EOF

# 1. Translations -> C tables (rerun after editing the YAML files)
node_modules/.bin/lv_i18n compile -t 'translations/*.yml' \
    -o 'main/ui/i18n' -l 'en-US'

# 2. Subset Noto Sans SC to the characters in zh_chars.txt plus ASCII.
node_modules/.bin/lv_font_conv --size 14 --bpp 4 --format lvgl \
    --no-kerning --no-compress \
    --font /tmp/NotoSansSC-Regular.otf -r 0x20-0x7E \
    --symbols "$(cat /tmp/zh_chars.txt)" \
    --lv-font-name lv_font_zh_14 \
    --lv-fallback lv_font_source_han_sans_sc_14_cjk \
    -o main/ui/i18n/lv_font_zh_14.c
# repeat with --size 16 / lv_font_zh_16 /
# lv_font_source_han_sans_sc_16_cjk
```

`--lv-fallback` makes LVGL fall back to its built-in CJK font, which is what
supplies the `LV_SYMBOL_*` icon glyphs. If you add new Chinese strings, also
add their characters to `/tmp/zh_chars.txt` and regenerate both sizes — an
existing key that gains a new character will otherwise render as a blank box.
`CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_14_CJK` / `_16_CJK` must stay enabled in
`sdkconfig.defaults` for that fallback to exist.

## Hardware

| Signal | GPIO | Notes |
| --- | --- | --- |
| LCD MOSI | 47 | SPI2 |
| LCD SCLK | 21 | SPI2 |
| LCD MISO | 48 | shared with XPT2046 SDO |
| LCD CS | 42 | |
| LCD DC | 41 | |
| LCD RST | — | not wired, panel resets with the chip |
| Touch CS | 46 | XPT2046 on the same SPI bus |
| Touch IRQ | 2 | |

Panel: ILI9341, 320×240 landscape, 60 MHz SPI, 16-bit RGB565.
The pin map lives in [main/board_display.c](main/board_display.c); it was
verified on the real PCB (the vendor tutorial map does not match this board).

## Project layout

```
main/
  main.c              boot order only: NVS -> Wi-Fi -> MQTT -> display -> UI
  board_display.c/.h  SPI bus, ILI9341 panel, LVGL display, touch, tick timer
  app_ui.c/.h         LVGL task (owns every LVGL call) + clock/tray helpers
  app_wifi.c/.h       station mode, SNTP
  app_mqtt.c/.h       MQTT client task, status heartbeat, command handling
  screenshot.c/.h     LVGL snapshot -> PPM6 over UART
  ui/
    ui_manager.c/.h   view stack + lifecycle engine (framework, view agnostic)
    ui_views.c/.h     view ids, ui_view_t descriptor, registry
    ui_page_base.c/.h shared page container + default lifecycle helpers
    i18n/
      app_i18n.c/.h       locale + font switching wrapper
      lv_i18n.c/.h        generated translation tables (do not hand-edit)
      lv_font_zh_14.c      generated Noto Sans SC 14 px subset
      lv_font_zh_16.c      generated Noto Sans SC 16 px subset
    win/
      ui_win_desktop.c/.h   desktop root: wallpaper, icons, start menu, taskbar
      ui_win_app_ui.c/.h    Apps tabview: honeycomb / galaxy / silk coverflow
      ui_win_start.c/.h     Start menu overlay
      ui_win_widgets.c/.h   reusable widgets (title bar, buttons, chips, tray)
      ui_win_icons.c/.h     app / shortcut tables with translation keys
      ui_win_theme.h        colour palette, spacing, font pointers
translations/
  en-US.yml             English source strings (msg-id catalogue)
  zh-CN.yml             Simplified Chinese translations
tools/
  capture_screenshot.py     host-side UART capture tool
```

### View lifecycle

Views implement six optional callbacks:

| Phase | When it runs |
| --- | --- |
| `init` | first time the view is pushed (widgets are built here) |
| `open` | the view became the active view |
| `pause` | another view was pushed on top |
| `resume` | the top view was popped again |
| `close` | the view leaves the stack |
| `deinit` | `close` finished, widgets are released |

`ui_manager` drives them:

```
push   : current.pause -> new.init (once) -> stack push -> new.open
pop    : old.close -> old.deinit -> back.resume
switch : close + deinit every view on the stack, then a fresh push
```

The root view is `VIEW_WIN_DESKTOP`; boot calls
`ui_views_register_all()` then `ui_manager_init_with(VIEW_WIN_DESKTOP)`.
Adding a view means one `UI_VIEW_DECLARE()` in `ui_views.h` and one
`UI_VIEW_DESC()` line in `ui_views.c`.

Window instances inside the desktop follow the same contract: each app has
a small vtable (`open`/`show`/`hide`/`close`) wired to the underlying view
callbacks, so launching and minimising a window correctly starts and stops
its animation timers.

## Prerequisites

- ESP-IDF v6.0.x (tested with v6.0.2) and the matching Xtensa toolchain.
  This repo uses `managed_components/` for LVGL 9.6 and
  `esp_lcd_ili9341` 2.1.0.
- Board connected over USB Serial/JTAG (shows up as `/dev/ttyACM0`).
- For screenshot host tooling: `python3`, `pyserial`; optional `Pillow`
  (PNG output) and `mosquitto-clients` (`--trigger`).
- Optional, only to regenerate strings or the Chinese fonts: Node.js with
  `lv_i18n` and `lv_font_conv`, plus `fonttools`. The generated `.c` files
  are committed, so a plain build does not need them.

## Build

```bash
# Load the ESP-IDF environment (adjust the path to your install)
. /mnt/d4a5750c-d360-4076-9135-eb9eef1d30db/sw/.espressif/v6.0.2/esp-idf/export.sh

cd ~/src/esp32s3-lvgl-win11
idf.py build
```

Target and required options (`esp32s3`, 16 MB flash, octal PSRAM,
LVGL RGB565 big-endian, snapshot support, Montserrat fonts and the Source Han
Sans SC CJK fonts used as the Chinese fallback) are pinned in
[sdkconfig.defaults](sdkconfig.defaults). On a fresh checkout run
`idf.py set-target esp32s3` once; that regenerates `sdkconfig` *from the
defaults*, so the LVGL colour and font options survive — editing `sdkconfig`
alone is not enough.

Flash layout is custom ([partitions.csv](partitions.csv)): 3 MB factory app
because Wi-Fi + MQTT + mbedTLS exceed the default 1 MB slot.

Useful extras:

```bash
idf.py size          # app/DRAM/IRAM usage
idf.py fullclean     # rebuild from scratch
idf.py menuconfig    # inspect or change options
```

## Flash and run

```bash
idf.py -p /dev/ttyACM0 flash
idf.py -p /dev/ttyACM0 monitor
```

Exit the monitor with `Ctrl+]`. Console baud rate is 115200.

Expected boot log:

```
I (xxx) main: starting Wi-Fi
I (xxx) main: starting MQTT task
I (xxx) main: starting display and touch
I (xxx) wifi: connected with <SSID>, rssi: -39
I (xxx) wifi: got ip 192.168.0.75
I (xxx) ui_mgr: registered VIEW_WIN_DESKTOP
I (xxx) ui_mgr: VIEW_WIN_DESKTOP: init
I (xxx) ui_mgr: VIEW_WIN_DESKTOP: open
I (xxx) mqtt: connected to mqtt://suijunqiang.top:1883
```

Force a clean reflash if the board does not boot (erases NVS and the app):

```bash
idf.py -p /dev/ttyACM0 erase-flash
idf.py -p /dev/ttyACM0 flash monitor
```

## Debug

**Logs.** `idf.py monitor` is the primary tool. Log levels can be raised per
tag at runtime, e.g. `esp_log_level_set("ui_mgr", ESP_LOG_DEBUG)`.

**Crashes.** Panic output includes the backtrace; decode it with:

```bash
idf.py monitor           # live panic + "Backtrace:" lines
idf.py size              # check stack/heap head-room
```

**Step debugging over USB-JTAG.** Use two terminals:

```bash
# Terminal 1 - start the GDB server
idf.py -p /dev/ttyACM0 openocd

# Terminal 2 - attach GDB to the running target
idf.py -p /dev/ttyACM0 gdb
```

Useful GDB commands: `mon reset halt`, `b ui_win_desktop_open`,
`continue`, `info threads`, `bt`. The ESP-IDF VS Code extension wraps the
same flow (`ESP-IDF: OpenOCD Server` + `ESP-IDF: GDB`).

**Heap/PSRAM.** The app has 8 MB octal PSRAM; internal RAM is the scarce
resource. The MQTT heartbeat reports free internal heap every 10 s.

## Wi-Fi and MQTT

Credentials and broker live in [main/app_wifi.c](main/app_wifi.c) and
[main/app_mqtt.c](main/app_mqtt.c). The client ID is derived from the MAC
(`esp32s3-win11-xxxxxx`).

| Topic | Direction | Payload |
| --- | --- | --- |
| `esp32s3/win11/status` | device → broker | retained `{"state":"online"}` / LWT `offline`; heartbeat with uptime, heap, time |
| `esp32s3/win11/cmd` | broker → device | `screenshot` triggers a screen capture |

```bash
mosquitto_sub -h suijunqiang.top -t esp32s3/win11/status
mosquitto_pub -h suijunqiang.top -t esp32s3/win11/cmd -m screenshot
```

## Screenshot capture

The firmware can dump the current LVGL screen over the same UART used for
logs. This is useful when a camera pointed at the panel is not good enough
for debugging colours, gradients or layout.

**How it works**

1. Any task (here: the MQTT command handler) calls `screenshot_request()`.
2. The LVGL task calls `screenshot_check_and_do()` after
   `lv_timer_handler()`, so the snapshot is taken from the render thread.
3. `lv_snapshot_take()` renders the active screen into a full-frame
   `lv_draw_buf_t` (RGB565, 320×240).
4. The buffer is converted row by row into RGB888 and written to stdout
   between the markers below. Other log tags are silenced during the
   transfer so the byte stream is not corrupted.

```
SCREENSHOT_BEGIN #1 320x240 P6
<320*240*3 binary bytes, RGB888>
SCREENSHOT_END #1
```

The bytes between the markers are a valid PPM6 image. `cf=0x12`
(`LV_COLOR_FORMAT_RGB565`, little-endian) and `0x1B`
(`RGB565_SWAPPED`, big-endian) are both handled.

**Host-side capture**

```bash
# Listen first, publish the MQTT command automatically, save PPM + PNG
python3 tools/capture_screenshot.py --trigger \
    -p /dev/ttyACM0 \
    -o /tmp/esp32_screenshot.ppm \
    --png /tmp/esp32_screenshot.png
```

`--trigger` waits for the MQTT `connected` log (or a short fallback) before
publishing `screenshot` to `esp32s3/win11/cmd`. Without `--trigger` the
script only listens, so start it first and publish from another terminal:

```bash
mosquitto_pub -h suijunqiang.top -t esp32s3/win11/cmd -m screenshot
```

Notes:

- At 115200 baud a 320×240 RGB888 frame is ~230 KB ≈ 20 s of streaming.
  The LVGL task is busy during the transfer, so the UI is frozen meanwhile.
- The frame is the LVGL render output (before the panel), which makes it the
  right reference when comparing "what LVGL drew" with "what the glass shows".
- The script deasserts DTR/RTS before opening the port; otherwise opening
  `/dev/ttyACM0` resets the ESP32-S3 and the capture command is lost.

## Configuration notes

- `CONFIG_LV_COLOR_FORMAT_RGB565_SWAPPED=y` and `CONFIG_LV_COLOR_DEPTH_16=y`
  must stay in `sdkconfig.defaults`. The ILI9341 expects big-endian RGB565;
  if the depth falls back to 24-bit, gradients and translucency render with
  wrong hues. Editing only `sdkconfig` is not enough — every reconfigure
  re-applies the defaults.
- The panel is initialised in RGB order (`LCD_RGB_ELEMENT_ORDER_RGB`) and
  with `mirror(false, false)`; touch X runs straight and Y is inverted.
  Changing the panel mirror requires flipping the touch axis as well.
- LVGL snapshot support requires `CONFIG_LV_USE_SNAPSHOT=y`; the snapshot
  buffer is allocated in PSRAM.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Panel shows swapped red/blue | `sdkconfig.defaults` colour format / depth (see above) |
| Washed-out gradients | vendor gamma/VCOM table passed via `vendor_config` |
| Icons drawn on the left, hit-boxes on the right | display mirror and touch axis are flipped independently |
| MQTT not connected at boot | client starts before Wi-Fi has an IP; it retries automatically |
| Some Chinese labels show blank boxes | the character is missing from the font subset — regenerate `lv_font_zh_14/16` (see [Localization](#localization)) |
| Taskbar language switch does nothing | check the locale log plus `ui_win_desktop_deinit/open` in the rebuild path |
| Screenshot times out | MQTT connected? another program holding `/dev/ttyACM0`? |
| `lv_snapshot_take` returns NULL | PSRAM disabled, or `LV_USE_SNAPSHOT` off |

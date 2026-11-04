#!/usr/bin/env bash
# Build and flash script for the ESP32-S3 Win11 demo.
#
# Usage:
#   ./flash.sh          # build + flash + monitor (default port /dev/ttyACM0)
#   ./flash.sh flash    # build + flash only
#   ./flash.sh build    # build only
#   ./flash.sh monitor  # open the serial monitor only
#   PORT=/dev/ttyUSB1 ./flash.sh flash   # override port
#
# The ESP-IDF export path below matches the workspace layout. Adjust if
# your IDF is installed somewhere else.

set -euo pipefail

# ---- ESP-IDF setup -----------------------------------------------------
IDF_PATH="${IDF_PATH:-/mnt/d4a5750c-d360-4076-9135-eb9eef1d30db/sw/.espressif/v6.0.2/esp-idf}"
if [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "ERROR: cannot find ESP-IDF export.sh at $IDF_PATH" >&2
    echo "Set IDF_PATH=/path/to/esp-idf and retry." >&2
    exit 1
fi

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${PORT:-/dev/ttyACM0}"

# shellcheck disable=SC1091
source "$IDF_PATH/export.sh" >/dev/null

cd "$PROJECT_DIR"

# ---- Actions -----------------------------------------------------------
action="${1:-all}"

do_build() {
    echo "==> idf.py build"
    idf.py build
}

do_flash() {
    echo "==> idf.py -p $PORT flash"
    idf.py -p "$PORT" flash
}

do_monitor() {
    echo "==> idf.py -p $PORT monitor (Ctrl+] to exit)"
    idf.py -p "$PORT" monitor
}

case "$action" in
    build)   do_build ;;
    flash)   do_build; do_flash ;;
    monitor) do_monitor ;;
    all)     do_build; do_flash; do_monitor ;;
    *)
        echo "Usage: $0 [build|flash|monitor|all]" >&2
        exit 1
        ;;
esac

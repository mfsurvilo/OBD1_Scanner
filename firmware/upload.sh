#!/usr/bin/env bash
#
# Developer USB flash (method 1 of 3): flash BOTH the firmware and the LittleFS
# filesystem image to a USB-connected ESP32-S3 in a single combined pio command.
#
# The filesystem image is built from ../pwa_app (see platformio.ini: data_dir),
# so the PWA in pwa_app/ is the single source of truth.
#
# Usage:
#   ./upload.sh              # uses the "blink_red" environment
#   ./upload.sh blink_green  # override the environment
#   ENV=blink_green ./upload.sh   # or via the ENV variable
#
# The other two flashing methods:
#   - End users, USB backup : ../firmware_server/flasher/  (browser / .exe)
#   - Over the air (Wi-Fi)  : the PWA's "Combined update (.ota)" form

set -euo pipefail

# Run from this script's directory so pio finds platformio.ini.
cd "$(dirname "$0")"

ENV="${1:-${ENV:-blink_red}}"

echo "==> Flashing firmware + filesystem over USB (env: $ENV)"
# One combined pio invocation: uploadfs (LittleFS image) + upload (app) together.
pio run -e "$ENV" -t uploadfs -t upload

echo "==> Done: firmware + filesystem uploaded."

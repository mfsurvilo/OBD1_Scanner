#!/usr/bin/env bash
#
# Developer USB flash (method 1 of 3): flash BOTH the firmware and the LittleFS
# filesystem image to a USB-connected ESP32-S3 in a single combined pio command.
#
# The filesystem image is built from ../pwa_app (see platformio.ini: data_dir),
# so the PWA in pwa_app/ is the single source of truth.
#
# Usage:
#   ./upload.sh              # uses the "obd1" environment
#   ./upload.sh badhealth    # override the environment (test builds)
#   ENV=badhealth ./upload.sh   # or via the ENV variable
#
# The other two flashing methods:
#   - End users, USB recovery : ../firmware_server/flasher/  (browser)
#   - Over the air (Wi-Fi)     : the PWA's "Update now" button

set -euo pipefail

# Run from this script's directory so pio finds platformio.ini.
cd "$(dirname "$0")"

ENV="${1:-${ENV:-obd1}}"

echo "==> Flashing firmware + filesystem over USB (env: $ENV)"
# One combined pio invocation: uploadfs (LittleFS image) + upload (app) together.
pio run -e "$ENV" -t uploadfs -t upload

echo "==> Done: firmware + filesystem uploaded."

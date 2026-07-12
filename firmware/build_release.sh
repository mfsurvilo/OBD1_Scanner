#!/usr/bin/env bash
#
# Build the shippable firmware and package the distributable images, staging
# them for a GitHub Release (../firmware_server/firmware/).
#
#   ./build_release.sh
#
# Produces three release assets (stable names — the tag carries the version):
#   firmware.bin     app image        (OTA: pulled by the "Update now" button)
#   filesystem.bin   LittleFS / PWA   (OTA: pulled by the "Update now" button)
#   factory.bin      full flash image (USB recovery via the flasher page)

set -euo pipefail
cd "$(dirname "$0")"

ENV="obd1"
OUT="../firmware_server/firmware"
# Prefer the project venv python (has esptool); fall back to system python3.
PY="../venv/bin/python"
[ -x "$PY" ] || PY="python3"

build=".pio/build/$ENV"
mkdir -p "$OUT"

echo "==> Building $ENV (app + filesystem)"
pio run -e "$ENV"
pio run -e "$ENV" -t buildfs

echo "==> Packaging factory (USB recovery) image"
"$PY" scripts/pack_images.py "$build"

cp "$build/firmware.bin"  "$OUT/firmware.bin"
cp "$build/littlefs.bin"  "$OUT/filesystem.bin"
cp "$build/factory.bin"   "$OUT/factory.bin"

echo "==> Done. Release images ($OUT):"; ls -1 "$OUT"

#!/usr/bin/env bash
#
# Build the two sample firmwares and package every distributable image, then
# stage them for the download server (../firmware_server/firmware/).
#
#   ./build_firmwares.sh
#
# For each variant (blink-red / blink-green) this produces:
#   <name>.bin           app image only        (OTA: "Firmware update")
#   <name>.ota           firmware + filesystem (OTA: "Combined update")
#   <name>-factory.bin   full flash image      (USB: esptool write_flash 0x0)

set -euo pipefail
cd "$(dirname "$0")"

OUT="../firmware_server/firmware"
FLASHER="../firmware_server/flasher"       # ESP Web Tools static site
# Prefer the project venv python (has esptool); fall back to system python3.
PY="../venv/bin/python"
[ -x "$PY" ] || PY="python3"

VERSION="$(git describe --tags --always --dirty 2>/dev/null || echo v0.0.0-dev)"

mkdir -p "$OUT" "$FLASHER"

for env in blink_red blink_green; do
  name="${env//_/-}"                         # blink_red -> blink-red
  build=".pio/build/$env"
  echo "==> Building $env (app + filesystem)"
  pio run -e "$env"
  pio run -e "$env" -t buildfs

  echo "==> Packaging images for $env"
  "$PY" scripts/pack_images.py "$env" "$build"

  cp "$build/firmware.bin"        "$OUT/$name.bin"
  cp "$build/$name.ota"           "$OUT/$name.ota"
  cp "$build/$name-factory.bin"   "$OUT/$name-factory.bin"

  # Stage the ESP Web Tools flasher: factory image + its manifest.
  cp "$build/$name-factory.bin"   "$FLASHER/$name-factory.bin"
  cat > "$FLASHER/manifest-$name.json" <<EOF
{
  "name": "OBD1 Scanner ($name)",
  "version": "$VERSION",
  "new_install_prompt_erase": true,
  "builds": [
    {
      "chipFamily": "ESP32-S3",
      "parts": [ { "path": "$name-factory.bin", "offset": 0 } ]
    }
  ]
}
EOF
done

echo "==> Done."
echo "    Download images ($OUT):"; ls -1 "$OUT"
echo "    Web flasher ($FLASHER):"; ls -1 "$FLASHER"

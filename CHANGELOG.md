# Changelog

All notable changes to the firmware and the PWA. Each release groups changes
into a **Firmware** section (the ESP32-S3 app) and a **PWA** section (the web
console served from the device). Versions match the git tags used to cut
releases (`vX.Y`).

## v0.4 — 2026-07-12

### Firmware
- Heartbeat LED is **blue** (was green in v0.3). The color difference makes an
  over-the-air update visible: flash v0.3 (green), hit **Update now**, and the
  heartbeat turns blue once v0.4 is running.

### PWA
- No changes from v0.3.

## v0.3 — 2026-07-12

First release on the new single-firmware / stable-asset release system.
Heartbeat LED is **green**.

### Firmware
- Collapsed the two sample builds (blink-red / blink-green) into a single
  shippable firmware (`env:obd1`). Dropped the `FW_VARIANT` build identity and
  the `/status.variant` field.
- Reworked over-the-air updates: the **Update now** button pulls `firmware.bin`
  (app) and `filesystem.bin` (LittleFS/PWA) separately from the newest release
  and flashes both. Retired the `.ota` combined-container format and the
  `/update/combined` endpoint.
- The update check lists all releases and picks the **highest version number**
  itself, rather than trusting GitHub's `/releases/latest` (which orders by the
  release's creation date and can point at the wrong version). The pull then
  fetches that version's assets by explicit tag.
- Release assets now have stable names (`firmware.bin`, `filesystem.bin`,
  `factory.bin`); the git tag carries the version.

### PWA
- Removed the manual `.ota`/`.bin` upload cards entirely; updating is the
  one-button internet **Update now** flow.
- Dropped the red/green variant color theming and the Firmware/variant status row.

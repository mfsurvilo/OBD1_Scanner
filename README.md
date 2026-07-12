# OBD1 Scanner

An ESP32-S3 based scanner for OBD-I era ECUs. The device hosts its own Wi-Fi
access point and serves a web console (PWA) straight off the chip — no app to
install — and updates itself over the internet with one button.

- **Firmware:** Arduino/ESP32-S3 (PlatformIO), FreeRTOS tasks, AP+STA Wi-Fi,
  OTA with bootloader rollback.
- **Console (PWA):** served from the device's LittleFS partition; live status
  over WebSocket, Wi-Fi provisioning, one-button update.
- **Delivery:** GitHub Releases hold the images; a static GitHub Pages page
  flashes a device over USB for recovery.

## How the pieces fit together

```
  ┌────────────┐   serves    ┌──────────────┐
  │  firmware  │ ──────────▶ │  PWA console │   (pwa_app/, flashed into LittleFS)
  │  (obd1)    │             │  obd1.local  │
  └─────┬──────┘             └──────┬───────┘
        │ heartbeat + /status       │ "Update now"
        │                           ▼
        │                    GET /update/check   ── picks highest release version
        │                    POST /update/pull   ── pulls firmware.bin + filesystem.bin
        ▼                                            from that release, flashes both
  GitHub Releases  ◀── build_release.sh / release.yml (firmware.bin, filesystem.bin, factory.bin)
        ▲
        │ mirrors factory.bin
  GitHub Pages flasher (firmware_server/flasher/)  ── USB recovery, version dropdown
```

Single sources of truth: `pwa_app/` is the only copy of the console, and
`firmware_server/flasher/versions.json` is the only list of published versions.

## Repo layout

| Path | What it is |
|---|---|
| `firmware/` | PlatformIO project — the device firmware, build/upload scripts, on-target tests |
| `pwa_app/` | The web console served from the device (flashed as the LittleFS image) |
| `firmware_server/` | The static USB-recovery flasher (GitHub Pages) — see its [README](firmware_server/README.md) |
| `tests/` | Host contract tests + hardware-in-the-loop scripts — see its [README](tests/README.md) |
| `protocol/`, `PROTOCOL.md` | OBD-I protocol notes |
| `board/` | KiCad hardware design |
| `developer_tools/` | Misc developer utilities |
| `CHANGELOG.md` | Per-version changes, split into Firmware / PWA sections |

## Quick start

**Flash a board over USB (developer):**
```bash
cd firmware && ./upload.sh          # builds + flashes firmware and the PWA image
```

**Use the device:** join Wi-Fi `OBD1_Scanner` (password `subaru92`), then open
`http://obd1.local/` (or `http://192.168.4.1/`).

## Flashing — three ways

1. **Developer USB** — `firmware/upload.sh` (one combined `pio` command: app + LittleFS).
2. **End-user USB recovery** — the GitHub Pages flasher: pick a version, connect,
   and flash the full image over WebSerial (desktop Chrome/Edge/Opera).
3. **Over-the-air** — the console's **Update now** button (details below).

## Updates (OTA)

The device updates itself over your home Wi-Fi — no file downloads:

1. In the console, save your home Wi-Fi credentials (**Internet update**).
2. Hit **Update now**. The firmware lists the repo's releases, picks the
   **highest version number** (it does *not* rely on GitHub's date-ordered
   `/releases/latest`), and pulls that release's `firmware.bin` (app) and
   `filesystem.bin` (PWA) by explicit tag.
3. It flashes both and reboots. A freshly-updated image boots on probation and
   must self-confirm healthy; if it doesn't, the bootloader rolls back to the
   previous slot.

## Releases

Each release publishes three stable-named assets (the git **tag** carries the
version): `firmware.bin` (app, OTA), `filesystem.bin` (PWA, OTA), and
`factory.bin` (full flash, USB recovery).

To cut a release:
```bash
git tag v0.5 && git push origin v0.5      # release.yml builds + publishes the assets
```
Then add the version to `firmware_server/flasher/versions.json` (newest first,
`latest: true` on it) and push to `master` so the Pages flasher lists it.

Full details — asset layout, the Pages CORS mirror, and the CI workflows — are
in **[`firmware_server/README.md`](firmware_server/README.md)**.

> Tip: cut and push one release tag at a time. Pushing a tag and a `master`
> change together can race the `release` and `pages` workflows.

## Testing

| Layer | What | Runs in CI? |
|---|---|---|
| 0 — host contract (`pytest`) | partition layout, `/status` + `/update/*` surface, image sizes | ✅ `ci.yml` |
| 4–5 — hardware (`hil_roundtrip.py`) | OTA app+fs round-trip, rollback, downgrade matrix | ❌ manual/local |

```bash
python -m pytest tests/ -q          # Layer 0, no hardware (what CI runs)
```

Hardware tests need a real board on the `OBD1_Scanner` AP with USB attached.
Full commands and modes are in **[`tests/README.md`](tests/README.md)**.

## Building

```bash
cd firmware
./upload.sh                # build + USB-flash app + PWA
./build_release.sh         # build + package firmware.bin / filesystem.bin / factory.bin
pio run -e obd1            # just compile the firmware
```
Requires PlatformIO and `esptool` (both in the project `venv/`).

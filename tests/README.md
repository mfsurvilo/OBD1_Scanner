# Regression tests

Layers of the test pyramid that guard the firmware release contracts.

## Layer 0 — host contract tests (no hardware) — `pytest`

Lock the release contracts that let any version flash/update any other:

- `test_partition_layout.py` — OTA partition offsets/sizes are frozen (golden copy).
- `test_api_contract.py` — `/update/*` endpoints, `/status` keys, rollback wiring.
- `test_image_sizes.py` — built app/fs fit their partitions (skips if not built).
- `test_release_contract.py` — `versions.json` sanity + the asset filenames agree
  across the firmware OTA, `build_release.sh`, and `release.yml`.

Run:

```bash
cd <repo root>
../venv/bin/python -m pytest tests/ -q      # or: python -m pytest tests/
```

CI-friendly — no board required. `test_image_sizes.py` needs
`firmware/build_release.sh` to have run.

## Layer 1 — native unit tests (no hardware) — `pio test`

Pure firmware logic compiled + unit-tested natively with Unity (no board, no
toolchain download):

- `test_version_compare` — `include/version_compare.h`, the semver ordering that
  picks the highest release version (the logic behind the OTA update check).

```bash
cd firmware && pio test -e native
```

## Layers 4–5 — hardware round-trip — `hil_roundtrip.py`

Needs a real device on the **OBD1_Scanner** AP with USB plugged in (Wi-Fi + USB
are independent). On Linux, `--wifi PASSWORD` auto-joins via `nmcli` and restores
your previous network afterward.

```bash
# OTA app+fs, verify reboot into the other slot + self-confirm, both ways (Layer 4)
python tests/hil_roundtrip.py --wifi subaru92
python tests/hil_roundtrip.py --skip-usb            # baseline already running

# Rollback proof: OTA an unhealthy build, expect auto-revert to baseline (Layer 5)
python tests/hil_roundtrip.py --fault-injection --skip-usb

# Reversibility across real releases: flash baseline, then OTA each older app image
python tests/hil_roundtrip.py --downgrade-matrix ~/releases/*/firmware.bin --skip-usb
```

Needs `requests` + `esptool` + `platformio` on PATH.

## CI

CI runs Layer 0 (pytest) and Layer 1 (`pio test -e native`) on every push/PR.
Hardware layers (4–5) are manual/local.

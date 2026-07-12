# Regression tests

Layers of the test pyramid that guard firmware forward/backward compatibility.

## Layer 1 — native unit tests (no hardware) — `pio test`

The **real** `.ota` parser (`firmware/include/ota_container.h`, the same code
the firmware runs) unit-tested natively with Unity, over adversarial chunk
boundaries:

```bash
cd firmware && pio test -e native
```

## Layer 0 — host contract tests (no hardware) — `pytest`

Lock the release contracts that let any version flash any other:

- `test_partition_layout.py` — OTA partition offsets/sizes are frozen (golden copy).
- `test_ota_format.py` — the `.ota` container format; packer ↔ firmware parser agree.
- `test_api_contract.py` — `/update/*` endpoints, `/status` keys, rollback wiring.
- `test_image_sizes.py` — built app/fs fit their partitions (skips if not built).

Run:

```bash
cd <repo root>
../venv/bin/python -m pytest tests/ -q      # or: python -m pytest tests/
```

CI-friendly — no board required. `test_image_sizes.py` needs
`firmware/./build_firmwares.sh` to have run.

## Layers 4–5 — hardware round-trip — `hil_roundtrip.py`

Needs a real device on the **OBD1_Scanner** AP with USB plugged in (Wi-Fi + USB
are independent). On Linux, `--wifi PASSWORD` auto-joins via `nmcli` and restores
your previous network afterward. Build the `.ota` files first:
`cd firmware && ./build_firmwares.sh`.

```bash
# Forward + backward OTA; each image must self-confirm (Layer 4)
python tests/hil_roundtrip.py --wifi subaru92
python tests/hil_roundtrip.py --skip-usb            # baseline already running

# Rollback proof: OTA an unhealthy build, expect auto-revert to baseline (Layer 5)
python tests/hil_roundtrip.py --fault-injection --skip-usb

# Reversibility across real releases: flash baseline, then OTA each older image
python tests/hil_roundtrip.py --downgrade-matrix ~/releases/*/*.ota --skip-usb
```

Needs `requests` + `esptool` + `platformio` on PATH.

## CI

`.github/workflows/ci.yml` runs Layer 0 (pytest) and Layer 1 (`pio test -e
native`) on every push/PR. Hardware layers (3–5) are manual/local for now.

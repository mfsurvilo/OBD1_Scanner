"""Built images must fit their partitions — an app that overflows the OTA slot
or a filesystem that overflows spiffs can't be flashed at all.
"""
import pytest

from conftest import FW

APP_SLOT = 0x330000   # ota_0 / ota_1 size in default_8MB.csv
FS_SLOT = 0x180000    # spiffs size

ENV = "obd1"


def test_app_fits_ota_slot():
    fw = FW / ".pio" / "build" / ENV / "firmware.bin"
    if not fw.exists():
        pytest.skip(f"{ENV} not built — run ./build_release.sh")
    size = fw.stat().st_size
    assert size < APP_SLOT, f"{ENV} app {size} >= slot {APP_SLOT}"


def test_fs_fits_spiffs():
    fs = FW / ".pio" / "build" / ENV / "littlefs.bin"
    if not fs.exists():
        pytest.skip(f"{ENV} filesystem not built — run ./build_release.sh")
    size = fs.stat().st_size
    assert size <= FS_SLOT, f"{ENV} fs {size} > spiffs {FS_SLOT}"

"""Built images must fit their partitions — an app that overflows the OTA slot
or a filesystem that overflows spiffs can't be flashed at all.
"""
import pytest

from conftest import FW

APP_SLOT = 0x330000   # ota_0 / ota_1 size in default_8MB.csv
FS_SLOT = 0x180000    # spiffs size

ENVS = ["blink_red", "blink_green"]


@pytest.mark.parametrize("env", ENVS)
def test_app_fits_ota_slot(env):
    fw = FW / ".pio" / "build" / env / "firmware.bin"
    if not fw.exists():
        pytest.skip(f"{env} not built — run ./build_firmwares.sh")
    size = fw.stat().st_size
    assert size < APP_SLOT, f"{env} app {size} >= slot {APP_SLOT}"


@pytest.mark.parametrize("env", ENVS)
def test_fs_fits_spiffs(env):
    fs = FW / ".pio" / "build" / env / "littlefs.bin"
    if not fs.exists():
        pytest.skip(f"{env} filesystem not built — run ./build_firmwares.sh")
    size = fs.stat().st_size
    assert size <= FS_SLOT, f"{env} fs {size} > spiffs {FS_SLOT}"

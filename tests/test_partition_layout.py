"""Partition layout is the deepest backward-compat contract: if app slot
offsets/sizes change, a device running an old release can't OTA a new one (and
downgrade breaks too). Freeze it here.
"""
import os

import pytest

from conftest import FW, GOLDEN, parse_partitions

LIVE = os.path.expanduser(
    "~/.platformio/packages/framework-arduinoespressif32/"
    "tools/partitions/default_8MB.csv"
)


def test_platformio_pins_ota_partition_table():
    ini = (FW / "platformio.ini").read_text()
    assert "board_build.partitions = default_8MB.csv" in ini, (
        "must keep the two-slot OTA partition table"
    )


def test_golden_layout_is_ota_capable():
    g = parse_partitions((GOLDEN / "default_8MB.csv").read_text())
    # Two app slots for OTA + a filesystem slot, at fixed offsets.
    assert g["app0"] == ("app", "ota_0", "0x10000", "0x330000")
    assert g["app1"] == ("app", "ota_1", "0x340000", "0x330000")
    assert g["spiffs"][:2] == ("data", "spiffs")
    assert g["spiffs"][2] == "0x670000" and g["spiffs"][3] == "0x180000"
    assert g["otadata"][:2] == ("data", "ota")


def test_live_table_matches_golden():
    if not os.path.isfile(LIVE):
        pytest.skip("framework partition file not found on this machine")
    live = parse_partitions(open(LIVE).read())
    golden = parse_partitions((GOLDEN / "default_8MB.csv").read_text())
    assert live == golden, (
        "the installed Arduino core changed default_8MB.csv — OTA/downgrade "
        "compatibility may be broken. Review before releasing."
    )

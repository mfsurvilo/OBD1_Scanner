"""The .ota container format is a release contract: the on-device parser and the
build-time packer must agree forever, or old tools can't flash new firmware
(and vice-versa). These tests freeze the format and cross-check both sides.
"""
import struct

from conftest import FW, load_packer


def test_ota_header_roundtrip(tmp_path):
    pack = load_packer()
    build = tmp_path / "build"
    build.mkdir()
    (build / "firmware.bin").write_bytes(b"\xAB" * 1000)
    (build / "littlefs.bin").write_bytes(b"\xCD" * 2000)

    out = build / "x.ota"
    pack.make_ota(str(build), str(out))
    data = out.read_bytes()

    magic, ver, flags, resv, fw, fs = struct.unpack("<4sBBHII", data[:16])
    assert magic == b"OB1U"
    assert ver == 1
    assert (flags, resv) == (0, 0)
    assert fw == 1000 and fs == 2000
    assert len(data) == 16 + 1000 + 2000
    assert data[16:16 + 1000] == b"\xAB" * 1000            # firmware first
    assert data[16 + 1000:] == b"\xCD" * 2000              # filesystem second


def test_packer_uses_frozen_magic_and_version():
    pack = load_packer()
    assert pack.OTA_MAGIC == b"OB1U"
    assert pack.OTA_VERSION == 1


def test_firmware_parser_matches_spec():
    """The pure C++ parser (ota_container.h, shared by firmware + native tests)
    must use the same magic, header size and field offsets as the packer."""
    parser = (FW / "include" / "ota_container.h").read_text()
    cpp = (FW / "src" / "web_server.cpp").read_text()

    assert "'O', 'B', '1', 'U'" in parser        # magic
    assert "HEADER_SIZE     = 16" in parser       # 16-byte header
    assert "buf + 8" in parser                    # fw_len at offset 8
    assert "buf + 12" in parser                   # fs_len at offset 12
    assert "/update/combined" in cpp              # endpoint still wired up
    assert "ota::next" in cpp                     # firmware drives the pure parser

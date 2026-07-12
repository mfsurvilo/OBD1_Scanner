#!/usr/bin/env python3
"""
Pack build artifacts into distributable images for one PlatformIO env.

Produces, next to firmware.bin in the build dir:
  <name>.ota           combined OTA container  (firmware + filesystem, streamable)
  <name>-factory.bin   flat full-flash image   (USB: esptool write_flash 0x0 ...)

where <name> is the env with underscores turned into dashes.

  python scripts/pack_images.py <env> <build_dir>

The .ota header matches WebServerClass::handleCombinedUpload():
  magic[4]="OB1U", u8 version, u8 flags, u16 reserved, u32 fw_len, u32 fs_len (LE)
"""
import os
import struct
import sys

OTA_MAGIC = b"OB1U"
OTA_VERSION = 1

# Flat-flash layout — offsets must match board_build.partitions (default_8MB.csv).
BOOT_APP0 = os.path.expanduser(
    "~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
)
FLASH_MAP = [
    ("0x0",      "bootloader.bin"),
    ("0x8000",   "partitions.bin"),
    ("0xe000",   BOOT_APP0),
    ("0x10000",  "firmware.bin"),
    ("0x670000", "littlefs.bin"),   # spiffs partition offset in default_8MB.csv
]


def _read(path):
    with open(path, "rb") as f:
        return f.read()


def make_ota(build_dir, out_path):
    fw = _read(os.path.join(build_dir, "firmware.bin"))
    fs = _read(os.path.join(build_dir, "littlefs.bin"))
    header = struct.pack("<4sBBHII", OTA_MAGIC, OTA_VERSION, 0, 0, len(fw), len(fs))
    with open(out_path, "wb") as f:
        f.write(header)
        f.write(fw)
        f.write(fs)
    print(f"  .ota      {out_path}  (fw {len(fw)} + fs {len(fs)} + 16)")


def make_factory(build_dir, out_path):
    import esptool  # provided by the venv / platformio tool

    # 'keep' inherits the flash mode/freq from the (qio) bootloader image so the
    # merged factory image matches the board — a mismatched mode can fail to boot.
    args = ["--chip", "esp32s3", "merge_bin", "-o", out_path,
            "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "8MB"]
    for offset, name in FLASH_MAP:
        path = name if os.path.isabs(name) else os.path.join(build_dir, name)
        args += [offset, path]
    esptool.main(args)
    print(f"  factory   {out_path}")


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    env, build_dir = sys.argv[1], sys.argv[2]
    name = env.replace("_", "-")
    make_ota(build_dir, os.path.join(build_dir, f"{name}.ota"))
    make_factory(build_dir, os.path.join(build_dir, f"{name}-factory.bin"))


if __name__ == "__main__":
    main()

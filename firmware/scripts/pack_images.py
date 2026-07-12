#!/usr/bin/env python3
"""
Pack build artifacts into the full-flash USB recovery image for one build dir.

Produces, next to firmware.bin in the build dir:
  factory.bin   flat full-flash image  (USB: esptool write_flash 0x0 ...)

  python scripts/pack_images.py <build_dir>

The app (firmware.bin) and filesystem (littlefs.bin) images are shipped as-is
for over-the-air updates; only the USB recovery path needs this merged image.
"""
import os
import sys

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
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    build_dir = sys.argv[1]
    make_factory(build_dir, os.path.join(build_dir, "factory.bin"))


if __name__ == "__main__":
    main()

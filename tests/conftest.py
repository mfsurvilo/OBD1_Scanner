"""Shared paths and helpers for the regression tests."""
import importlib.util
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
FW = ROOT / "firmware"
GOLDEN = pathlib.Path(__file__).resolve().parent / "golden"


def load_packer():
    """Import firmware/scripts/pack_images.py (esptool is imported lazily, so
    this works without esptool installed)."""
    spec = importlib.util.spec_from_file_location(
        "pack_images", FW / "scripts" / "pack_images.py"
    )
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def parse_partitions(text):
    """Parse a partition CSV into {name: (type, subtype, offset, size)}."""
    rows = {}
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        cols = [c.strip() for c in line.split(",")]
        if len(cols) < 5:
            continue
        name, ptype, subtype, offset, size = cols[:5]
        rows[name] = (ptype, subtype, offset.lower(), size.lower())
    return rows

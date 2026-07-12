"""The HTTP surface (OTA endpoints + /status keys) is what the PWA and the
flashing tools depend on. Removing or renaming any of it breaks clients running
against other firmware versions, so lock the contract.
"""
from conftest import FW

CPP = (FW / "src" / "web_server.cpp")
VERSION_H = (FW / "include" / "version.h")


def test_ota_endpoints_present():
    src = CPP.read_text()
    for route in ("/status", "/update/firmware", "/update/filesystem",
                  "/update/combined"):
        assert route in src, f"missing endpoint {route}"


def test_status_keys_present():
    src = CPP.read_text()
    # Keys that tooling / regression round-trip rely on.
    for key in ("name", "variant", "version", "commit",
                "uptime_ms", "partition", "ota_confirmed"):
        assert f'doc["{key}"]' in src, f"missing /status key {key}"


def test_version_identifiers_present():
    h = VERSION_H.read_text()
    assert "FW_VERSION" in h
    assert "FW_VARIANT" in h


def test_rollback_wired_in():
    """Health-confirm / rollback must stay in the main loop, else a bad OTA
    can't auto-revert."""
    main = (FW / "src" / "main.cpp").read_text()
    assert "confirmHealthIfPending" in main
    assert "esp_ota_mark_app_valid_cancel_rollback" in main
    assert "esp_ota_mark_app_invalid_rollback_and_reboot" in main

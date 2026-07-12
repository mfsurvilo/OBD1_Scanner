"""The HTTP surface (OTA endpoints + /status keys) is what the PWA and the
flashing tools depend on. Removing or renaming any of it breaks clients running
against other firmware versions, so lock the contract.
"""
import re

from conftest import FW, ROOT

CPP = (FW / "src" / "web_server.cpp")
VERSION_H = (FW / "include" / "version.h")
APP_JS = (ROOT / "pwa_app" / "js" / "app.js")
MAIN_CPP = (FW / "src" / "main.cpp")


def test_ota_endpoints_present():
    src = CPP.read_text()
    for route in ("/status", "/update/firmware", "/update/filesystem",
                  "/update/check", "/update/pull"):
        assert route in src, f"missing endpoint {route}"


def test_status_keys_present():
    src = CPP.read_text()
    # Keys that tooling / regression round-trip rely on.
    for key in ("name", "version", "commit",
                "uptime_ms", "partition", "ota_confirmed"):
        assert f'doc["{key}"]' in src, f"missing /status key {key}"


def test_version_identifiers_present():
    h = VERSION_H.read_text()
    assert "FW_VERSION" in h


def test_rollback_wired_in():
    """Health-confirm / rollback must stay in the main loop, else a bad OTA
    can't auto-revert."""
    main = MAIN_CPP.read_text()
    assert "confirmHealthIfPending" in main
    assert "esp_ota_mark_app_valid_cancel_rollback" in main
    assert "esp_ota_mark_app_invalid_rollback_and_reboot" in main


def test_forced_rollback_reboot_is_test_only():
    """The self-reboot-into-rollback call must live ONLY inside the
    FORCE_UNHEALTHY (fault-injection) branch. In a normal build the other slot
    may be empty, so a self-reboot could boot-loop a freshly-flashed device."""
    main = MAIN_CPP.read_text()
    call = "esp_ota_mark_app_invalid_rollback_and_reboot"
    assert call in main, "rollback self-reboot missing entirely"
    m = re.search(r"#ifdef FORCE_UNHEALTHY(.*?)#else(.*?)#endif", main, re.S)
    assert m, "FORCE_UNHEALTHY guard not found around the rollback logic"
    fault_branch, normal_branch = m.group(1), m.group(2)
    assert call in fault_branch, f"{call} must be in the FORCE_UNHEALTHY branch"
    assert call not in normal_branch, f"{call} must NOT be in the normal-build branch"


def test_pwa_only_calls_endpoints_that_exist():
    """Every API path the PWA calls must be a route the firmware registers,
    otherwise the console silently breaks against a renamed endpoint."""
    used = set(re.findall(r"\$\{API\}(/[\w/-]+)", APP_JS.read_text()))
    assert used, "no API paths found in app.js — regex drift?"
    registered = set(re.findall(r'_http\.on\("(/[\w/-]+)"', CPP.read_text()))
    missing = used - registered
    assert not missing, f"PWA calls endpoints not registered in firmware: {missing}"

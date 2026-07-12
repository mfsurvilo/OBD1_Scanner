#!/usr/bin/env python3
"""
Hardware-in-the-loop regression for OTA update & rollback.

Modes (pick one; default is --round-trip):
  --round-trip        USB-flash the baseline, then OTA the app + filesystem
                      images (upload) and verify the device reboots into the
                      other OTA slot and self-confirms healthy — twice, so it
                      lands back on the original slot. Proves the OTA path and
                      A/B slot alternation both directions.
  --fault-injection   OTA a deliberately-unhealthy app image and prove the
                      device auto-rolls-back to the baseline slot (Layer 5).
                      This is the only test that actually exercises rollback.
  --downgrade-matrix APP.bin [APP.bin ...]
                      Flash the baseline, then OTA each listed app image in
                      order, verifying each still boots + confirms. Proves
                      reversibility across real releases.

Networking: join the device's "OBD1_Scanner" AP and keep USB plugged in (Wi-Fi
and USB are independent). On Linux, pass --wifi PASSWORD to auto-join via nmcli
and restore your previous network afterward.

  python tests/hil_roundtrip.py --wifi subaru92
  python tests/hil_roundtrip.py --fault-injection --skip-usb
  python tests/hil_roundtrip.py --downgrade-matrix ~/rel/v0.3/firmware.bin --skip-usb

Needs: requests, esptool (for USB flash), platformio on PATH.
"""
import argparse
import glob
import os
import subprocess
import sys
import time

import requests

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW_DIR = os.path.join(ROOT, "firmware")


# --- device I/O -------------------------------------------------------------
def get_status(host, timeout=3):
    r = requests.get(f"http://{host}/status", timeout=timeout)
    r.raise_for_status()
    return r.json()


def wait_for_status(host, deadline_s=60):
    end = time.time() + deadline_s
    last = None
    while time.time() < end:
        try:
            return get_status(host)
        except Exception as e:
            last = str(e)
        time.sleep(2)
    raise TimeoutError(f"device never came back; last={last}")


def wait_confirmed(host, deadline_s=20):
    end = time.time() + deadline_s
    while time.time() < end:
        try:
            if get_status(host).get("ota_confirmed") is True:
                return True
        except Exception:
            pass
        time.sleep(2)
    return False


def upload(host, path, endpoint):
    with open(path, "rb") as f:
        files = {"file": (os.path.basename(path), f, "application/octet-stream")}
        try:
            r = requests.post(f"http://{host}{endpoint}", files=files, timeout=120)
            print(f"    {endpoint} HTTP {r.status_code}: {r.text[:80]}")
        except requests.exceptions.RequestException as e:
            print(f"    {endpoint} socket closed (device rebooting): {e}")


def ota_app_fs(host, app, fs):
    """Upload the app image, then (if given) the filesystem image."""
    upload(host, app, "/update/firmware")
    if fs:
        # The app upload triggers a reboot on success; wait before the fs push.
        time.sleep(8)
        wait_for_status(host)
        upload(host, fs, "/update/filesystem")


def usb_flash(env):
    print(f"[usb] flashing {env} ...")
    subprocess.run(["pio", "run", "-e", env, "-t", "upload"], cwd=FW_DIR, check=True)


def build_env(env):
    """Build app + filesystem for an env. Returns (firmware.bin, littlefs.bin)."""
    build = os.path.join(FW_DIR, ".pio", "build", env)
    print(f"[build] {env} (app + fs) ...")
    subprocess.run(["pio", "run", "-e", env], cwd=FW_DIR, check=True)
    subprocess.run(["pio", "run", "-e", env, "-t", "buildfs"], cwd=FW_DIR, check=True)
    return (os.path.join(build, "firmware.bin"), os.path.join(build, "littlefs.bin"))


# --- Wi-Fi (Linux / nmcli) --------------------------------------------------
def wifi_current():
    out = subprocess.check_output(
        ["nmcli", "-t", "-f", "active,ssid", "dev", "wifi"]).decode()
    for line in out.splitlines():
        if line.startswith("yes:"):
            return line.split(":", 1)[1]
    return None


def wifi_join(ssid, password):
    print(f"[wifi] joining {ssid} ...")
    subprocess.run(["nmcli", "dev", "wifi", "connect", ssid, "password", password],
                   check=True)
    time.sleep(4)


def wifi_restore(prev):
    if prev:
        print(f"[wifi] restoring {prev} ...")
        subprocess.run(["nmcli", "con", "up", prev], check=False)


# --- test steps -------------------------------------------------------------
def ota_step(host, app, fs, prev_partition, label):
    print(f"[{label}] OTA -> {os.path.basename(app)}"
          + (f" + {os.path.basename(fs)}" if fs else ""))
    ota_app_fs(host, app, fs)
    print("    waiting for reboot ...")
    time.sleep(8)
    st = wait_for_status(host)
    confirmed = wait_confirmed(host)
    part = st.get("partition")
    print(f"    OK: version={st.get('version')} partition={part} confirmed={confirmed}")
    if not confirmed:
        sys.exit(f"    FAIL: image did not self-confirm")
    if prev_partition and part == prev_partition:
        sys.exit(f"    FAIL: expected the OTA slot to flip from {prev_partition}")
    return st


def run_round_trip(host, baseline, skip_usb):
    if not skip_usb:
        usb_flash(baseline)
        time.sleep(8)
    print("[1] verifying baseline ...")
    st = wait_for_status(host)
    p0 = st.get("partition")
    print(f"    baseline: version={st.get('version')} partition={p0}")
    app, fs = build_env(baseline)
    st = ota_step(host, app, fs, p0, "2 OTA forward")
    ota_step(host, app, fs, st.get("partition"), "3 OTA back")
    print("\nPASS: OTA app+fs applied twice; slot alternated and both confirmed.")


def run_fault_injection(host, baseline, skip_usb):
    if not skip_usb:
        usb_flash(baseline)
        time.sleep(8)
    base = wait_for_status(host)
    p0 = base.get("partition")
    print(f"[1] baseline healthy: version={base.get('version')} partition={p0}")

    bad_app, _ = build_env("badhealth")
    print("[2] OTA the unhealthy app image (expect auto-rollback) ...")
    upload(host, bad_app, "/update/firmware")

    # It boots badhealth, fails to self-confirm, then the bootloader reverts to
    # the baseline slot. Wait past the grace + reboot window and re-check.
    print("    waiting for rollback (~30s) ...")
    time.sleep(30)
    st = wait_for_status(host, deadline_s=60)
    confirmed = wait_confirmed(host)
    part = st.get("partition")
    print(f"    recovered: version={st.get('version')} partition={part} confirmed={confirmed}")
    if part != p0:
        sys.exit(f"    FAIL: expected rollback to {p0}, still on {part}")
    if not confirmed:
        sys.exit("    FAIL: baseline did not re-confirm after rollback")
    print("\nPASS: unhealthy image rolled back to the previous slot.")


def run_downgrade_matrix(host, baseline, apps, skip_usb):
    if not skip_usb:
        usb_flash(baseline)
        time.sleep(8)
    wait_for_status(host)
    print(f"[1] baseline up; flashing across {len(apps)} app image(s)")
    for i, app in enumerate(apps, 1):
        print(f"[{i}] OTA -> {os.path.basename(app)}")
        upload(host, app, "/update/firmware")
        time.sleep(8)
        st = wait_for_status(host)
        if not wait_confirmed(host):
            sys.exit(f"    FAIL: {os.path.basename(app)} did not boot/confirm")
        print(f"    booted: version={st.get('version')} partition={st.get('partition')}")
    print("\nPASS: device booted every image in the downgrade matrix.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.4.1")
    ap.add_argument("--baseline", default="obd1")
    ap.add_argument("--skip-usb", action="store_true")
    ap.add_argument("--wifi", metavar="PASSWORD",
                    help="auto-join OBD1_Scanner via nmcli, restore network after")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--round-trip", action="store_true")
    mode.add_argument("--fault-injection", action="store_true")
    mode.add_argument("--downgrade-matrix", nargs="+", metavar="APP")
    args = ap.parse_args()

    prev_wifi = None
    if args.wifi:
        prev_wifi = wifi_current()
        wifi_join("OBD1_Scanner", args.wifi)
    try:
        if args.fault_injection:
            run_fault_injection(args.host, args.baseline, args.skip_usb)
        elif args.downgrade_matrix:
            apps = [p for g in args.downgrade_matrix for p in glob.glob(g)] \
                or args.downgrade_matrix
            run_downgrade_matrix(args.host, args.baseline, apps, args.skip_usb)
        else:
            run_round_trip(args.host, args.baseline, args.skip_usb)
    finally:
        if args.wifi:
            wifi_restore(prev_wifi)


if __name__ == "__main__":
    main()

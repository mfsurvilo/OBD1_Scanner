#!/usr/bin/env python3
"""
Hardware-in-the-loop regression for firmware reversibility & rollback.

Modes (pick one; default is --round-trip):
  --round-trip        USB-flash baseline -> OTA forward -> OTA back; verify
                      /status.variant and ota_confirmed at each step.
  --fault-injection   OTA a deliberately-unhealthy build and prove the device
                      auto-rolls-back to the baseline (Layer 5). This is the
                      only test that actually exercises rollback.
  --downgrade-matrix A.ota B.ota ...
                      Flash the baseline, then OTA to each listed image in
                      order (newest -> oldest), verifying each still boots.
                      Proves reversibility across real releases.

Networking: join the device's "OBD1_Scanner" AP and keep USB plugged in (Wi-Fi
and USB are independent). On Linux, pass --wifi PASSWORD to auto-join via nmcli
and restore your previous network afterward.

  python tests/hil_roundtrip.py --wifi subaru92
  python tests/hil_roundtrip.py --fault-injection --skip-usb
  python tests/hil_roundtrip.py --downgrade-matrix ~/rel/v0.3/*.ota --skip-usb

Needs: requests, esptool (for USB flash), platformio on PATH.
"""
import argparse
import glob
import importlib.util
import os
import subprocess
import sys
import time

import requests

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW_DIR = os.path.join(ROOT, "firmware")
IMG_DIR = os.path.join(ROOT, "firmware_server", "firmware")


def _load_packer():
    spec = importlib.util.spec_from_file_location(
        "pack_images", os.path.join(FW_DIR, "scripts", "pack_images.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# --- device I/O -------------------------------------------------------------
def variant_of(env):
    return env.replace("_", "-")


def get_status(host, timeout=3):
    r = requests.get(f"http://{host}/status", timeout=timeout)
    r.raise_for_status()
    return r.json()


def wait_for_status(host, want_variant=None, deadline_s=60):
    end = time.time() + deadline_s
    last = None
    while time.time() < end:
        try:
            st = get_status(host)
            last = st
            if want_variant is None or st.get("variant") == want_variant:
                return st
        except Exception as e:
            last = str(e)
        time.sleep(2)
    raise TimeoutError(f"waiting for {want_variant or 'device'}; last={last}")


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


def ota_upload(host, ota_path):
    with open(ota_path, "rb") as f:
        files = {"file": (os.path.basename(ota_path), f, "application/octet-stream")}
        try:
            r = requests.post(f"http://{host}/update/combined", files=files, timeout=90)
            print(f"    upload HTTP {r.status_code}: {r.text[:80]}")
        except requests.exceptions.RequestException as e:
            print(f"    upload socket closed (device rebooting): {e}")


def usb_flash(env):
    print(f"[usb] flashing {env} ...")
    subprocess.run(["pio", "run", "-e", env, "-t", "upload"], cwd=FW_DIR, check=True)


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


# --- image helpers ----------------------------------------------------------
def ota_for(env_or_variant):
    """Path to a prebuilt .ota in firmware_server/firmware for a variant."""
    name = variant_of(env_or_variant)
    path = os.path.join(IMG_DIR, f"{name}.ota")
    if not os.path.isfile(path):
        sys.exit(f"missing {path} — run: cd firmware && ./build_firmwares.sh")
    return path


def build_ota(env):
    """Build + package an .ota for an env not covered by build_firmwares.sh
    (e.g. blink_badhealth). Returns the .ota path."""
    build = os.path.join(FW_DIR, ".pio", "build", env)
    print(f"[build] {env} (app + fs) ...")
    subprocess.run(["pio", "run", "-e", env], cwd=FW_DIR, check=True)
    subprocess.run(["pio", "run", "-e", env, "-t", "buildfs"], cwd=FW_DIR, check=True)
    out = os.path.join(build, f"{variant_of(env)}.ota")
    _load_packer().make_ota(build, out)
    return out


# --- test steps -------------------------------------------------------------
def ota_step(host, ota_path, expect_variant, label):
    print(f"[{label}] OTA -> {os.path.basename(ota_path)}")
    ota_upload(host, ota_path)
    print("    waiting for reboot ...")
    time.sleep(8)
    st = wait_for_status(host, want_variant=expect_variant)
    confirmed = wait_confirmed(host)
    print(f"    OK: variant={st['variant']} version={st.get('version')} "
          f"partition={st.get('partition')} confirmed={confirmed}")
    if not confirmed:
        sys.exit(f"    FAIL: {expect_variant} did not self-confirm")
    return st


def run_round_trip(host, baseline, other, skip_usb):
    if not skip_usb:
        usb_flash(baseline)
        time.sleep(8)
    print("[1] verifying baseline ...")
    st = wait_for_status(host, want_variant=variant_of(baseline))
    print(f"    baseline: variant={st['variant']} version={st.get('version')}")
    ota_step(host, ota_for(other), variant_of(other), "2 forward")
    ota_step(host, ota_for(baseline), variant_of(baseline), "3 backward")
    print("\nPASS: forward + backward OTA verified; both images self-confirmed.")


def run_fault_injection(host, baseline, skip_usb):
    if not skip_usb:
        usb_flash(baseline)
        time.sleep(8)
    base = wait_for_status(host, want_variant=variant_of(baseline))
    base_variant = base["variant"]
    print(f"[1] baseline healthy: {base_variant} (partition {base.get('partition')})")

    bad = build_ota("blink_badhealth")
    print("[2] OTA the unhealthy image (expect auto-rollback) ...")
    ota_upload(host, bad)

    # It boots badhealth, fails to self-confirm, then the bootloader reverts to
    # the baseline slot. Wait past the grace + reboot window and re-check.
    print("    waiting for rollback (~30s) ...")
    time.sleep(30)
    st = wait_for_status(host, want_variant=base_variant, deadline_s=60)
    confirmed = wait_confirmed(host)
    print(f"    recovered: variant={st['variant']} partition={st.get('partition')} "
          f"confirmed={confirmed}")
    if st["variant"] != base_variant:
        sys.exit(f"    FAIL: expected rollback to {base_variant}, got {st['variant']}")
    print("\nPASS: unhealthy image rolled back to the previous firmware.")


def run_downgrade_matrix(host, baseline, images, skip_usb):
    if not skip_usb:
        usb_flash(baseline)
        time.sleep(8)
    wait_for_status(host, want_variant=variant_of(baseline))
    print(f"[1] baseline {variant_of(baseline)} up; downgrading across "
          f"{len(images)} image(s)")
    for i, img in enumerate(images, 1):
        print(f"[{i}] OTA -> {os.path.basename(img)}")
        ota_upload(host, img)
        time.sleep(8)
        st = wait_for_status(host)                 # any variant, just must boot
        if not wait_confirmed(host):
            sys.exit(f"    FAIL: {os.path.basename(img)} did not boot/confirm")
        print(f"    booted: variant={st.get('variant')} version={st.get('version')}")
    print("\nPASS: device booted every image in the downgrade matrix.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.4.1")
    ap.add_argument("--baseline", default="blink_red")
    ap.add_argument("--other", default="blink_green")
    ap.add_argument("--skip-usb", action="store_true")
    ap.add_argument("--wifi", metavar="PASSWORD",
                    help="auto-join OBD1_Scanner via nmcli, restore network after")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--round-trip", action="store_true")
    mode.add_argument("--fault-injection", action="store_true")
    mode.add_argument("--downgrade-matrix", nargs="+", metavar="OTA")
    args = ap.parse_args()

    prev_wifi = None
    if args.wifi:
        prev_wifi = wifi_current()
        wifi_join("OBD1_Scanner", args.wifi)
    try:
        if args.fault_injection:
            run_fault_injection(args.host, args.baseline, args.skip_usb)
        elif args.downgrade_matrix:
            imgs = [p for g in args.downgrade_matrix for p in glob.glob(g)] \
                or args.downgrade_matrix
            run_downgrade_matrix(args.host, args.baseline, imgs, args.skip_usb)
        else:
            run_round_trip(args.host, args.baseline, args.other, args.skip_usb)
    finally:
        if args.wifi:
            wifi_restore(prev_wifi)


if __name__ == "__main__":
    main()

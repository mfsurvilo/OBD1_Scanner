#!/usr/bin/env python3
"""
Automated USB updater: download the latest factory image from GitHub Releases
and flash it over USB. Fully unattended (no browser) — the counterpart to the
one-click ESP Web Tools page for CI / power users.

  python cli_update.py --variant blink-red
  python cli_update.py --repo owner/name --tag v0.2.0 --variant blink-green
  python cli_update.py --variant blink-red --port /dev/ttyACM0

Needs: esptool  (pip install esptool). Repo is auto-detected from `git remote
origin` if --repo is omitted. Set GITHUB_TOKEN to avoid API rate limits.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request


def default_repo():
    try:
        url = subprocess.check_output(
            ["git", "remote", "get-url", "origin"], stderr=subprocess.DEVNULL
        ).decode().strip()
        if url.endswith(".git"):
            url = url[:-4]
        if url.startswith("git@"):          # git@github.com:owner/name
            return url.split(":", 1)[1]
        return "/".join(url.split("/")[-2:])  # https://github.com/owner/name
    except Exception:
        return None


def _get_json(url):
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "obd1-updater"}
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req) as r:
        return json.load(r)


def find_asset(repo, tag, variant):
    base = f"https://api.github.com/repos/{repo}/releases"
    rel = _get_json(base + "/latest") if tag in (None, "latest") \
        else _get_json(f"{base}/tags/{tag}")
    want = f"{variant}-factory.bin"
    for asset in rel.get("assets", []):
        if asset["name"] == want:
            return rel["tag_name"], asset["browser_download_url"]
    have = ", ".join(a["name"] for a in rel.get("assets", [])) or "(none)"
    sys.exit(f"no asset '{want}' in release {rel.get('tag_name')}. Assets: {have}")


def download(url, dest):
    req = urllib.request.Request(url, headers={"User-Agent": "obd1-updater"})
    with urllib.request.urlopen(req) as r, open(dest, "wb") as f:
        shutil.copyfileobj(r, f)


def flash(path, port):
    try:
        import esptool
    except ImportError:
        sys.exit("esptool not installed — run: pip install esptool")
    args = ["--chip", "esp32s3"]
    if port:
        args += ["--port", port]
    args += ["write_flash", "0x0", path]     # full-flash image at offset 0
    print(f"esptool {' '.join(args)}")
    esptool.main(args)


def main():
    ap = argparse.ArgumentParser(description="Flash the latest release over USB")
    ap.add_argument("--repo", default=default_repo(), help="owner/name (default: git origin)")
    ap.add_argument("--tag", default="latest", help="release tag or 'latest'")
    ap.add_argument("--variant", default="blink-red", help="e.g. blink-red / blink-green")
    ap.add_argument("--port", default=None, help="serial port (default: auto-detect)")
    args = ap.parse_args()

    if not args.repo:
        sys.exit("no repo — pass --repo owner/name (no git origin configured)")

    print(f"== updating from {args.repo} ({args.tag}), variant {args.variant} ==")
    tag, url = find_asset(args.repo, args.tag, args.variant)
    with tempfile.TemporaryDirectory() as tmp:
        dest = os.path.join(tmp, f"{args.variant}-factory.bin")
        print(f"downloading {tag} -> {os.path.basename(dest)}")
        download(url, dest)
        print(f"downloaded {os.path.getsize(dest)} bytes; flashing over USB ...")
        flash(dest, args.port)
    print("done — device reflashed and rebooting.")


if __name__ == "__main__":
    main()

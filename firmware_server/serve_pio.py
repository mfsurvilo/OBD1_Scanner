#!/usr/bin/env python3
"""
Tiny server to practice downloading firmware images onto your phone.

Scans firmware/.pio/build/<env>/ and serves the distributable images (built by
firmware/build_firmwares.sh) — never the bootloader/partition bins:
    <env>.bin  app image  |  <name>.ota  firmware+app  |  <name>-factory.bin  USB

Run:
    python firmware_server/serve_pio.py           # http://0.0.0.0:8000
    python firmware_server/serve_pio.py --port 5000

From the phone (same WiFi as this PC): http://<PC-LAN-IP>:8000
"""
import argparse
import glob
import os

from flask import Flask, abort, render_template_string, send_file

# firmware/.pio/build/  (relative to this file's parent project)
PIO_BUILD = os.path.join(
    os.path.dirname(__file__), "..", "firmware", ".pio", "build"
)
# ESP Web Tools static site (index + manifests + factory bins).
FLASHER_DIR = os.path.join(os.path.dirname(__file__), "flasher")

app = Flask(__name__)

PAGE = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Firmware downloads</title>
  <style>
    :root { color-scheme: dark; }
    * { box-sizing: border-box; }
    body { margin:0; font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;
           background:#0d1117; color:#e6edf3; }
    header { padding:20px; border-bottom:1px solid #21262d; }
    h1 { font-size:18px; margin:0; }
    main { max-width:640px; margin:0 auto; padding:16px; display:flex; flex-direction:column; gap:14px; }
    .fw { background:#161b22; border:1px solid #21262d; border-radius:12px; padding:16px;
          display:flex; align-items:center; gap:14px; }
    .meta { flex:1; min-width:0; }
    .name { font-weight:600; }
    .sub { color:#8b949e; font-size:13px; margin-top:2px; word-break:break-all; }
    .dl { text-decoration:none; background:#2f81f7; color:#fff; font-weight:600;
          padding:10px 16px; border-radius:8px; white-space:nowrap; }
    .empty { color:#8b949e; font-size:14px; }
    code { background:#161b22; padding:1px 5px; border-radius:4px; }
  </style>
</head>
<body>
  <header><h1>Firmware downloads (.pio)</h1></header>
  <main>
    <div class="fw">
      <div class="meta">
        <div class="name">USB flasher (recovery)</div>
        <div class="sub">flash over USB from the browser — desktop Chrome/Edge</div>
      </div>
      <a class="dl" href="/flash/">Open</a>
    </div>
    {% if files %}
      {% for f in files %}
      <div class="fw">
        <div class="meta">
          <div class="name">{{ f.download }}</div>
          <div class="sub">{{ f.kind }} · env {{ f.env }} · {{ f.size }}</div>
        </div>
        <a class="dl" href="/dl/{{ f.env }}/{{ f.disk }}" download="{{ f.download }}">Download</a>
      </div>
      {% endfor %}
    {% else %}
      <p class="empty">No images found. Build first:
        <code>cd firmware &amp;&amp; ./build_firmwares.sh</code></p>
    {% endif %}
  </main>
</body>
</html>
"""


def _human(n):
    return f"{n/1024:.1f} KB" if n < 1024 * 1024 else f"{n/1024/1024:.2f} MB"


def _artifacts():
    """One entry per downloadable image across all env build dirs."""
    files = []
    for env_dir in sorted(glob.glob(os.path.join(PIO_BUILD, "*"))):
        if not os.path.isdir(env_dir):
            continue
        env = os.path.basename(env_dir)
        name = env.replace("_", "-")
        # (on-disk filename, download name, human label)
        candidates = [
            ("firmware.bin",        f"{name}.bin",         "app image (OTA)"),
            (f"{name}.ota",         f"{name}.ota",         "firmware + app (OTA)"),
            (f"{name}-factory.bin", f"{name}-factory.bin", "full flash (USB)"),
        ]
        for disk, download, kind in candidates:
            path = os.path.join(env_dir, disk)
            if os.path.isfile(path):
                files.append({
                    "env": env, "disk": disk, "download": download,
                    "kind": kind, "size": _human(os.path.getsize(path)),
                })
    return files


@app.route("/")
def index():
    return render_template_string(PAGE, files=_artifacts())


@app.route("/flash/")
def flasher_index():
    # Web Serial needs a secure context: this works at http://localhost:PORT
    # (localhost is treated as secure). For remote users host flasher/ over
    # HTTPS (e.g. GitHub Pages).
    return send_file(os.path.join(FLASHER_DIR, "index.html"))


@app.route("/flash/<path:name>")
def flasher_asset(name):
    base = os.path.realpath(FLASHER_DIR)
    path = os.path.realpath(os.path.join(base, name))
    if not path.startswith(base + os.sep) or not os.path.isfile(path):
        abort(404)
    return send_file(path)


@app.route("/dl/<env>/<path:disk>")
def download(env, disk):
    # Confine to the env's build dir; only serve real distributable images.
    env_dir = os.path.realpath(os.path.join(PIO_BUILD, env))
    path = os.path.realpath(os.path.join(env_dir, disk))
    if not path.startswith(env_dir + os.sep) or not os.path.isfile(path):
        abort(404)
    name = env.replace("_", "-")
    allowed = {"firmware.bin": f"{name}.bin", f"{name}.ota": f"{name}.ota",
               f"{name}-factory.bin": f"{name}-factory.bin"}
    if disk not in allowed:
        abort(404)
    return send_file(path, as_attachment=True, download_name=allowed[disk])


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="0.0.0.0")
    p.add_argument("--port", type=int, default=8000)
    args = p.parse_args()
    print("Images found:", [f["download"] for f in _artifacts()])
    app.run(host=args.host, port=args.port)

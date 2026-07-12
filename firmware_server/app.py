#!/usr/bin/env python3
"""
Tiny firmware download server.

Serves the sample firmware .bin files (built by
firmware/build_firmwares.sh) over a phone-friendly page. The flow:

    1. Phone (on normal WiFi/cellular) opens this site, downloads a .bin.
    2. Phone joins the ESP32's "OBD1_Scanner" WiFi AP.
    3. In the scanner PWA, upload the downloaded .bin (OTA).

Run:
    pip install flask          # (or: pip install -r ../requirements.txt)
    python firmware_server/app.py          # http://0.0.0.0:8000
    python firmware_server/app.py --port 5000

To reach it from your phone, use the machine's LAN IP, e.g. http://192.168.1.20:8000
"""
import argparse
import os

from flask import Flask, abort, render_template_string, send_from_directory

FIRMWARE_DIR = os.path.join(os.path.dirname(__file__), "firmware")

app = Flask(__name__)

PAGE = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>OBD1 Scanner Firmware</title>
  <style>
    :root { color-scheme: dark; }
    * { box-sizing: border-box; }
    body { margin:0; font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;
           background:#0d1117; color:#e6edf3; }
    header { padding:20px; border-bottom:1px solid #21262d; }
    h1 { font-size:18px; margin:0; }
    main { max-width:640px; margin:0 auto; padding:16px;
           display:flex; flex-direction:column; gap:14px; }
    .fw { background:#161b22; border:1px solid #21262d; border-radius:12px;
          padding:16px; display:flex; align-items:center; gap:14px; }
    .fw .meta { flex:1; min-width:0; }
    .fw .name { font-weight:600; }
    .fw .sub { color:#8b949e; font-size:13px; margin-top:2px; word-break:break-all; }
    .dl { text-decoration:none; background:#2f81f7; color:#fff; font-weight:600;
          padding:10px 16px; border-radius:8px; white-space:nowrap; }
    .empty { color:#8b949e; font-size:14px; }
    .hint { color:#8b949e; font-size:13px; padding:0 4px; }
    code { background:#161b22; padding:1px 5px; border-radius:4px; }
  </style>
</head>
<body>
  <header><h1>OBD1 Scanner — Firmware</h1></header>
  <main>
    <p class="hint">Download a firmware image, then join the
      <code>OBD1_Scanner</code> WiFi and upload it in the scanner app.</p>
    {% if files %}
      {% for f in files %}
      <div class="fw">
        <div class="meta">
          <div class="name">{{ f.label }}</div>
          <div class="sub">{{ f.name }} · {{ f.size }}</div>
        </div>
        <a class="dl" href="/firmware/{{ f.name }}" download>Download</a>
      </div>
      {% endfor %}
    {% else %}
      <p class="empty">No firmware found. Build it first:
        <code>cd firmware &amp;&amp; ./build_firmwares.sh</code></p>
    {% endif %}
  </main>
</body>
</html>
"""


def _human_size(n):
    for unit in ("B", "KB", "MB"):
        if n < 1024 or unit == "MB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n/1024:.1f} {unit}"
        n /= 1024


def _list_firmware():
    if not os.path.isdir(FIRMWARE_DIR):
        return []
    files = []
    for name in sorted(os.listdir(FIRMWARE_DIR)):
        if not name.endswith(".bin"):
            continue
        path = os.path.join(FIRMWARE_DIR, name)
        label = name[:-4].replace("-", " ").replace("_", " ").title()
        files.append({
            "name": name,
            "label": label,
            "size": _human_size(os.path.getsize(path)),
        })
    return files


@app.route("/")
def index():
    return render_template_string(PAGE, files=_list_firmware())


@app.route("/firmware/<path:name>")
def firmware(name):
    if not name.endswith(".bin"):
        abort(404)
    return send_from_directory(FIRMWARE_DIR, name, as_attachment=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="OBD1 Scanner firmware download server")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()
    print(f"Serving firmware from {FIRMWARE_DIR}")
    app.run(host=args.host, port=args.port)

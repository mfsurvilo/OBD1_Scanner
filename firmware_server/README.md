# firmware_server

Everything for getting firmware onto a device, across the three flashing paths.

## Layout

```
flasher/            ESP Web Tools static site (deployed to GitHub Pages)
  index.html          one-click USB flasher (desktop Chrome/Edge)
  manifest-*.json     generated per variant  (git-ignored)
  *-factory.bin       full-flash images      (git-ignored)
firmware/           generated download images (git-ignored)
  <name>.bin          app image      (OTA: "Firmware update")
  <name>.ota          firmware + app (OTA: "Combined update")
  <name>-factory.bin  full flash     (USB)
serve_pio.py        local dev server: browse/download images + /flash/ page
cli_update.py       automated USB updater (pulls latest Release, flashes)
app.py              (legacy ECU monitor — unrelated)
```

Build the artifacts first: `cd ../firmware && ./build_firmwares.sh`.

## The three flashing methods

1. **Developer USB** — `cd ../firmware && ./upload.sh` (one combined pio command).
2. **End-user USB (recovery)** — the `flasher/` page. Hosted on Pages over HTTPS;
   locally test at `http://localhost:8000/flash/` (Web Serial needs https or localhost).
3. **OTA (Wi-Fi)** — the device PWA's "Combined update (.ota)" form → `/update/combined`.

Automated USB (CI / power users), no browser:

```bash
python cli_update.py --variant blink-red          # latest Release, auto port
python cli_update.py --repo owner/name --tag v0.2.0 --variant blink-green
```

## Local dev server

```bash
python serve_pio.py            # http://0.0.0.0:8000  (downloads + /flash/)
```

## CI / hosting (see ../.github/workflows)

- `ci.yml` — host regression tests on every push/PR.
- `release.yml` — on a `v*` tag: build + package, publish binaries to a GitHub
  Release, deploy `flasher/` to GitHub Pages (Settings > Pages > Source =
  "GitHub Actions" once).

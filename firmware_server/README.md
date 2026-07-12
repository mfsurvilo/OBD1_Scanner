# firmware_server

The end-user USB recovery flasher: a static page deployed to GitHub Pages.
No Python, no backend — just static files.

## Layout

```
flasher/            static site (deployed to GitHub Pages)
  index.html          USB flasher (desktop Chrome/Edge) with a version dropdown
  versions.json       the list of available firmware versions (edit per release)
firmware/           generated release images (git-ignored, staged for Releases)
  firmware.bin        app image      (OTA: pulled by the "Update now" button)
  filesystem.bin      LittleFS / PWA (OTA: pulled by the "Update now" button)
  factory.bin         full flash     (USB recovery)
```

`index.html` reads `versions.json` and, for the selected version, flashes that
version's `factory.bin` (ESP Web Tools). GitHub release assets aren't served
with CORS headers, so `pages.yml` mirrors each listed version's `factory.bin`
onto the Pages origin (`fw/<tag>-factory.bin`) at deploy time. The `firmware/`
images are **generated** by `../firmware/build_release.sh` and published to the
Release by CI — never committed.

## Publishing a new version

1. `cd ../firmware && ./build_release.sh` (optional local check).
2. Tag + push: `git tag v0.5 && git push origin v0.5` → `release.yml` builds and
   publishes `firmware.bin` / `filesystem.bin` / `factory.bin` to the Release.
3. Add the version to `flasher/versions.json` (newest first, `latest:true` on it)
   and push to `master` → `pages.yml` redeploys the flasher.

## The three flashing methods

1. **Developer USB** — `cd ../firmware && ./upload.sh` (one combined pio command).
2. **End-user USB (recovery)** — the `flasher/` page. Hosted on Pages over HTTPS
   (Web Serial needs https or localhost).
3. **OTA (Wi-Fi)** — the device PWA's "Update now" button → `/update/pull`. The
   firmware lists the repo's releases, picks the highest version number (not
   GitHub's date-ordered `/releases/latest`), and pulls that release's
   `firmware.bin` + `filesystem.bin` by explicit tag.

## Local preview

The flasher is static, so any static file server works (Web Serial requires
https **or** localhost):

```bash
cd flasher && python -m http.server 8000   # http://localhost:8000
```

## CI / hosting (see ../.github/workflows)

- `ci.yml` — host regression tests on every push/PR.
- `pages.yml` — on every `master` push: deploy `flasher/` to GitHub Pages
  (Settings > Pages > Source = "GitHub Actions" once).
- `release.yml` — on a `v*` tag: build + package, publish the release images to
  a GitHub Release.

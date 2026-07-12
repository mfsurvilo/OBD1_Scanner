"""Release-system contracts.

The flasher, the firmware OTA, the build script, and the release workflow all
have to agree on two things: the list of published versions (versions.json,
hand-edited every release) and the asset filenames. A mismatch silently breaks
flashing or OTA — exactly the class of bug that's invisible until a user hits
"Update", so lock it here where CI catches it.
"""
import json
import re

from conftest import ROOT, FW

FLASHER = ROOT / "firmware_server" / "flasher"
VERSIONS = FLASHER / "versions.json"
INDEX_HTML = FLASHER / "index.html"
BUILD_SH = FW / "build_release.sh"
WEB_CPP = FW / "src" / "web_server.cpp"
RELEASE_YML = ROOT / ".github" / "workflows" / "release.yml"
PAGES_YML = ROOT / ".github" / "workflows" / "pages.yml"

# Assets the build script stages / the release publishes, and the subset the
# firmware pulls over OTA.
RELEASE_ASSETS = {"firmware.bin", "filesystem.bin", "factory.bin"}
OTA_ASSETS = {"firmware.bin", "filesystem.bin"}


# --- versions.json: the hand-edited list the flasher reads -------------------
def test_versions_json_is_sane():
    data = json.loads(VERSIONS.read_text())
    assert re.fullmatch(r"[\w.-]+/[\w.-]+", data["repo"]), "repo must be owner/name"

    versions = data["versions"]
    assert isinstance(versions, list) and versions, "versions must be a non-empty list"

    tags = [v["tag"] for v in versions]
    assert len(tags) == len(set(tags)), f"duplicate tags: {tags}"

    for v in versions:
        # Convention: the git tag is 'v' + the displayed version.
        assert v["tag"] == "v" + v["version"], f"tag/version mismatch: {v}"

    latest = [v for v in versions if v.get("latest")]
    assert len(latest) == 1, "exactly one version must have latest: true"


# --- asset-name contract across firmware / build / release -------------------
def test_build_script_stages_the_release_assets():
    staged = set(re.findall(r'\$OUT/([\w.-]+\.bin)"', BUILD_SH.read_text()))
    assert staged == RELEASE_ASSETS, f"build_release.sh stages {staged}"


def test_release_workflow_publishes_the_release_assets():
    published = set(re.findall(r"firmware_server/firmware/([\w.-]+\.bin)",
                               RELEASE_YML.read_text()))
    assert published == RELEASE_ASSETS, f"release.yml publishes {published}"


def test_firmware_pulls_only_published_assets():
    pulled = set(re.findall(r'base \+ "([\w.-]+\.bin)"', WEB_CPP.read_text()))
    assert pulled == OTA_ASSETS, f"firmware OTA pulls {pulled}"
    assert pulled <= RELEASE_ASSETS, "firmware pulls an asset the release doesn't publish"


# --- factory.bin path: flasher must fetch what pages.yml mirrors -------------
def test_flasher_and_pages_agree_on_factory_path():
    assert "fw/${tag}-factory.bin" in INDEX_HTML.read_text(), \
        "index.html doesn't fetch fw/<tag>-factory.bin"
    assert "site/fw/${tag}-factory.bin" in PAGES_YML.read_text(), \
        "pages.yml doesn't mirror to fw/<tag>-factory.bin"

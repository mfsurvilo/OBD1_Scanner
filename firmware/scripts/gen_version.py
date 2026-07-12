#
# Stamp the firmware version from git at build time.
#
# Injects FW_VERSION (git describe) and FW_GIT_COMMIT (short SHA) as compile
# defines, consumed by include/version.h. Runs as a PlatformIO `pre:` script
# (see platformio.ini). If the tree isn't a git checkout, the fallbacks in
# version.h apply instead.
#
Import("env")  # noqa: F821  (provided by PlatformIO)
import subprocess


def _git(args, default):
    try:
        return (
            subprocess.check_output(["git"] + args, stderr=subprocess.DEVNULL)
            .decode()
            .strip()
        )
    except Exception:
        return default


version = _git(["describe", "--tags", "--always", "--dirty"], "v0.0.0-dev")
commit = _git(["rev-parse", "--short", "HEAD"], "unknown")

env.Append(CPPDEFINES=[
    ("FW_VERSION", env.StringifyMacro(version)),
    ("FW_GIT_COMMIT", env.StringifyMacro(commit)),
])

print("== firmware version: %s (%s) ==" % (version, commit))

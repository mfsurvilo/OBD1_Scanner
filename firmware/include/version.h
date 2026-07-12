#ifndef VERSION_H
#define VERSION_H

//=============================================================================
// Firmware identity
//
// FW_VERSION and FW_GIT_COMMIT are injected at build time by
// scripts/gen_version.py (from `git describe`). The fallbacks below apply only
// when building outside a git checkout. Bump releases by tagging git, e.g.:
//     git tag v0.2.0 && ./upload.sh
//=============================================================================

#ifndef FW_VERSION
#define FW_VERSION "v0.0.0-dev"
#endif

#ifndef FW_GIT_COMMIT
#define FW_GIT_COMMIT "unknown"
#endif

#define FW_NAME       "OBD1_Scanner"
#define FW_BUILD_DATE (__DATE__ " " __TIME__)

#endif // VERSION_H

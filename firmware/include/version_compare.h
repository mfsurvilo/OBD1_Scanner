#ifndef VERSION_COMPARE_H
#define VERSION_COMPARE_H

#include <cstdio>

//=============================================================================
// Pure version-ordering helper (no Arduino deps, unit-tested natively).
//
// Packs a "vMAJOR.MINOR.PATCH" release tag into a monotonically comparable
// integer so the firmware can pick the highest release version itself instead
// of trusting GitHub's date-ordered /releases/latest. Missing components are 0,
// and a leading 'v'/'V' is optional.
//
//   version_key("v0.4")  > version_key("v0.3")
//   version_key("v0.10") > version_key("v0.9")     // double-digit minor
//   version_key("v1.0")  > version_key("v0.99")
//=============================================================================
inline long version_key(const char* tag) {
  if (!tag) return -1;
  if (*tag == 'v' || *tag == 'V') tag++;
  int major = 0, minor = 0, patch = 0;
  std::sscanf(tag, "%d.%d.%d", &major, &minor, &patch);
  return (long)major * 1000000L + (long)minor * 1000L + patch;
}

#endif // VERSION_COMPARE_H

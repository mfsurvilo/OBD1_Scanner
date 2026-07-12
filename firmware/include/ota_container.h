#ifndef OTA_CONTAINER_H
#define OTA_CONTAINER_H

//=============================================================================
// .ota combined-image container — PURE parser (no Arduino / ESP / I/O deps).
//
// This is the single source of truth for the .ota format, shared by:
//   - the firmware (src/web_server.cpp drives real Update calls off the steps)
//   - the native unit tests (test/test_ota_container, run on the host via
//     `pio test -e native`)
// so the tests exercise the exact byte-accounting the device runs.
//
// Wire format (little-endian), 16-byte header then two raw payloads:
//   magic[4]="OB1U", u8 version, u8 flags, u16 reserved, u32 fw_len, u32 fs_len
//   <fw_len bytes: app image> <fs_len bytes: filesystem image>
//
// The parser is a pull-based streaming state machine: feed arbitrary-sized
// chunks (HTTP upload boundaries are unpredictable) and pull Steps describing
// what to do — begin/write/end for the firmware slot then the filesystem slot.
//=============================================================================
#include <stdint.h>
#include <string.h>

namespace ota {

static const uint8_t  MAGIC[4]        = {'O', 'B', '1', 'U'};
static const uint32_t HEADER_SIZE     = 16;
static const uint8_t  FORMAT_VERSION  = 1;

struct Header {
  uint8_t  version;
  uint32_t fw_len;
  uint32_t fs_len;
};

// Parse a 16-byte header. `buf` must hold at least HEADER_SIZE bytes.
inline bool parse_header(const uint8_t* buf, Header* out) {
  if (memcmp(buf, MAGIC, 4) != 0) return false;
  out->version = buf[4];
  memcpy(&out->fw_len, buf + 8, 4);    // LE; xtensa and x86 are both LE
  memcpy(&out->fs_len, buf + 12, 4);
  return true;
}

enum Action {
  NONE,
  BEGIN_FW, WRITE_FW, END_FW,   // firmware (app) slot
  BEGIN_FS, WRITE_FS, END_FS,   // filesystem slot
};

// One unit of work emitted by next(). For WRITE_* the (data,len) points into
// the caller's chunk; for BEGIN_* `size` is the declared payload length.
struct Step {
  Action         action = NONE;
  const uint8_t* data   = nullptr;
  uint32_t       len    = 0;
  uint32_t       size   = 0;
};

enum Phase {
  P_HEADER,
  P_FW_BODY, P_FW_END,
  P_FS_BODY, P_FS_END,
  P_DONE, P_ERROR,
};

struct Parser {
  Phase    phase   = P_HEADER;
  uint8_t  hdr[HEADER_SIZE];
  uint32_t hdrGot  = 0;
  Header   header  = {0, 0, 0};
  uint32_t remaining = 0;   // bytes left in the current payload phase
};

inline bool is_done(const Parser& s)  { return s.phase == P_DONE; }
inline bool is_error(const Parser& s) { return s.phase == P_ERROR; }

// Advance the parser, consuming from (*pp,*pn) and emitting at most one Step.
// Returns true while it made progress (call again); false when the current
// chunk is drained (need more bytes) or the stream is done/errored.
inline bool next(Parser& s, const uint8_t** pp, uint32_t* pn, Step* out) {
  const uint8_t* p = *pp;
  uint32_t n = *pn;
  *out = Step{};

  switch (s.phase) {
    case P_HEADER: {
      uint32_t need = HEADER_SIZE - s.hdrGot;
      uint32_t take = need < n ? need : n;
      memcpy(s.hdr + s.hdrGot, p, take);
      s.hdrGot += take;
      *pp = p + take;
      *pn = n - take;
      if (s.hdrGot < HEADER_SIZE) return false;          // header spans chunks
      if (!parse_header(s.hdr, &s.header)) { s.phase = P_ERROR; return false; }
      if (s.header.fw_len > 0) {
        s.phase = P_FW_BODY; s.remaining = s.header.fw_len;
        out->action = BEGIN_FW; out->size = s.header.fw_len; return true;
      }
      if (s.header.fs_len > 0) {
        s.phase = P_FS_BODY; s.remaining = s.header.fs_len;
        out->action = BEGIN_FS; out->size = s.header.fs_len; return true;
      }
      s.phase = P_DONE; return false;
    }
    case P_FW_BODY: {
      if (s.remaining == 0) { s.phase = P_FW_END; out->action = END_FW; return true; }
      if (n == 0) return false;
      uint32_t take = s.remaining < n ? s.remaining : n;
      out->action = WRITE_FW; out->data = p; out->len = take;
      s.remaining -= take; *pp = p + take; *pn = n - take;
      return true;
    }
    case P_FW_END: {
      if (s.header.fs_len > 0) {
        s.phase = P_FS_BODY; s.remaining = s.header.fs_len;
        out->action = BEGIN_FS; out->size = s.header.fs_len; return true;
      }
      s.phase = P_DONE; return false;
    }
    case P_FS_BODY: {
      if (s.remaining == 0) { s.phase = P_FS_END; out->action = END_FS; return true; }
      if (n == 0) return false;
      uint32_t take = s.remaining < n ? s.remaining : n;
      out->action = WRITE_FS; out->data = p; out->len = take;
      s.remaining -= take; *pp = p + take; *pn = n - take;
      return true;
    }
    case P_FS_END:
      s.phase = P_DONE;
      return false;
    default:
      return false;   // P_DONE / P_ERROR
  }
}

}  // namespace ota

#endif // OTA_CONTAINER_H

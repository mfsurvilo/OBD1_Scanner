//=============================================================================
// Native unit tests for the pure .ota parser (include/ota_container.h).
// Exercises the exact byte-accounting the firmware runs, with adversarial
// chunk boundaries. Run with: pio test -e native
//=============================================================================
#include <unity.h>
#include <ota_container.h>

#include <cstdint>
#include <cstring>
#include <vector>

void setUp() {}
void tearDown() {}

// Build a valid .ota image: header + fw payload + fs payload.
static std::vector<uint8_t> make_ota(const std::vector<uint8_t>& fw,
                                     const std::vector<uint8_t>& fs,
                                     const uint8_t magic[4] = ota::MAGIC) {
  std::vector<uint8_t> out(ota::HEADER_SIZE, 0);
  memcpy(out.data(), magic, 4);
  out[4] = ota::FORMAT_VERSION;
  uint32_t fwn = (uint32_t)fw.size(), fsn = (uint32_t)fs.size();
  memcpy(out.data() + 8, &fwn, 4);
  memcpy(out.data() + 12, &fsn, 4);
  out.insert(out.end(), fw.begin(), fw.end());
  out.insert(out.end(), fs.begin(), fs.end());
  return out;
}

// Drive the parser over `img` fed in `chunk`-sized pieces, collecting the bytes
// routed to the firmware slot and the filesystem slot, and the action sequence.
struct Result {
  std::vector<uint8_t> fw, fs;
  std::vector<ota::Action> actions;
  ota::Parser parser;
};

static Result drive(const std::vector<uint8_t>& img, size_t chunk) {
  Result r;
  size_t off = 0;
  while (off < img.size()) {
    size_t clen = std::min(chunk, img.size() - off);
    const uint8_t* p = img.data() + off;
    uint32_t n = (uint32_t)clen;
    ota::Step step;
    while (ota::next(r.parser, &p, &n, &step)) {
      r.actions.push_back(step.action);
      if (step.action == ota::WRITE_FW)
        r.fw.insert(r.fw.end(), step.data, step.data + step.len);
      else if (step.action == ota::WRITE_FS)
        r.fs.insert(r.fs.end(), step.data, step.data + step.len);
    }
    off += clen;
  }
  return r;
}

static std::vector<uint8_t> ramp(size_t n, uint8_t start) {
  std::vector<uint8_t> v(n);
  for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(start + i);
  return v;
}

// --- header ----------------------------------------------------------------
void test_parse_header_valid() {
  auto img = make_ota(ramp(10, 0), ramp(20, 100));
  ota::Header h;
  TEST_ASSERT_TRUE(ota::parse_header(img.data(), &h));
  TEST_ASSERT_EQUAL_UINT8(ota::FORMAT_VERSION, h.version);
  TEST_ASSERT_EQUAL_UINT32(10, h.fw_len);
  TEST_ASSERT_EQUAL_UINT32(20, h.fs_len);
}

void test_parse_header_bad_magic() {
  uint8_t bad[4] = {'X', 'X', 'X', 'X'};
  auto img = make_ota(ramp(4, 0), ramp(4, 0), bad);
  ota::Header h;
  TEST_ASSERT_FALSE(ota::parse_header(img.data(), &h));
}

// --- happy path across many chunk sizes ------------------------------------
void test_roundtrip_various_chunk_sizes() {
  auto fw = ramp(500, 1);
  auto fs = ramp(750, 200);
  auto img = make_ota(fw, fs);
  size_t chunks[] = {1, 2, 3, 5, 13, 16, 17, 64, 500, 4096};
  for (size_t c : chunks) {
    Result r = drive(img, c);
    TEST_ASSERT_TRUE_MESSAGE(ota::is_done(r.parser), "parser not done");
    TEST_ASSERT_EQUAL_UINT32(fw.size(), r.fw.size());
    TEST_ASSERT_EQUAL_UINT32(fs.size(), r.fs.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fw.data(), r.fw.data(), fw.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fs.data(), r.fs.data(), fs.size());
  }
}

// --- action ordering: BEGIN/WRITE.../END for FW then FS --------------------
void test_action_sequence() {
  auto img = make_ota(ramp(30, 0), ramp(40, 0));
  Result r = drive(img, 7);
  TEST_ASSERT_EQUAL(ota::BEGIN_FW, r.actions.front());
  // last three meaningful actions must be END_FW-ish ... BEGIN_FS ... END_FS
  TEST_ASSERT_EQUAL(ota::END_FS, r.actions.back());
  bool saw_end_fw = false, saw_begin_fs = false;
  for (auto a : r.actions) {
    if (a == ota::END_FW) saw_end_fw = true;
    if (a == ota::BEGIN_FS) {
      TEST_ASSERT_TRUE_MESSAGE(saw_end_fw, "BEGIN_FS before END_FW");
      saw_begin_fs = true;
    }
  }
  TEST_ASSERT_TRUE(saw_begin_fs);
}

// --- edge cases ------------------------------------------------------------
void test_bad_magic_errors() {
  uint8_t bad[4] = {'N', 'O', 'P', 'E'};
  auto img = make_ota(ramp(8, 0), ramp(8, 0), bad);
  Result r = drive(img, 4);
  TEST_ASSERT_TRUE(ota::is_error(r.parser));
  TEST_ASSERT_FALSE(ota::is_done(r.parser));
}

void test_truncated_not_done() {
  auto img = make_ota(ramp(100, 0), ramp(100, 0));
  img.resize(16 + 100 + 40);   // drop 60 bytes of the fs payload
  Result r = drive(img, 8);
  TEST_ASSERT_FALSE(ota::is_done(r.parser));
  TEST_ASSERT_FALSE(ota::is_error(r.parser));
}

void test_fw_only() {
  auto fw = ramp(64, 5);
  auto img = make_ota(fw, {});
  Result r = drive(img, 9);
  TEST_ASSERT_TRUE(ota::is_done(r.parser));
  TEST_ASSERT_EQUAL_UINT32(64, r.fw.size());
  TEST_ASSERT_EQUAL_UINT32(0, r.fs.size());
}

void test_fs_only_skips_fw() {
  auto fs = ramp(64, 5);
  auto img = make_ota({}, fs);
  Result r = drive(img, 9);
  TEST_ASSERT_TRUE(ota::is_done(r.parser));
  TEST_ASSERT_EQUAL_UINT32(0, r.fw.size());
  TEST_ASSERT_EQUAL_UINT32(64, r.fs.size());
  TEST_ASSERT_EQUAL(ota::BEGIN_FS, r.actions.front());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_header_valid);
  RUN_TEST(test_parse_header_bad_magic);
  RUN_TEST(test_roundtrip_various_chunk_sizes);
  RUN_TEST(test_action_sequence);
  RUN_TEST(test_bad_magic_errors);
  RUN_TEST(test_truncated_not_done);
  RUN_TEST(test_fw_only);
  RUN_TEST(test_fs_only_skips_fw);
  return UNITY_END();
}

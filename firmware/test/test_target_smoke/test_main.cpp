//=============================================================================
// On-target smoke tests (Layer 3). Flashed to the board and run on-device via
// `pio test -e test_esp32`, results reported back over USB-CDC serial.
//
// Verifies the runtime prerequisites for OTA + rollback actually hold on real
// hardware: the filesystem mounts, the two-slot OTA partition table is present
// and correctly sized, there's room to flash the next image, and the AP starts.
//=============================================================================
#include <Arduino.h>
#include <unity.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

void setUp() {}
void tearDown() {}

void test_littlefs_mounts() {
  // begin(true) formats on a failed mount, so success means the FS is usable.
  TEST_ASSERT_TRUE(LittleFS.begin(true));
}

void test_running_on_ota_slot_with_a_spare() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  TEST_ASSERT_NOT_NULL(running);
  TEST_ASSERT_EQUAL(ESP_PARTITION_TYPE_APP, running->type);

  // There must be a *different* OTA slot to write the next image into.
  const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
  TEST_ASSERT_NOT_NULL(next);
  TEST_ASSERT_NOT_EQUAL(running->address, next->address);

  // Both app slots are the size the tooling assumes (default_8MB.csv).
  TEST_ASSERT_EQUAL_HEX32(0x330000, running->size);
  TEST_ASSERT_EQUAL_HEX32(0x330000, next->size);
}

void test_spiffs_partition_present_and_sized() {
  const esp_partition_t* fs = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
  TEST_ASSERT_NOT_NULL(fs);
  TEST_ASSERT_EQUAL_HEX32(0x180000, fs->size);   // LittleFS lives here
}

void test_room_to_flash_next_image() {
  // Free sketch space == the inactive OTA slot; must comfortably hold our app.
  TEST_ASSERT_GREATER_THAN_UINT32(0x100000, ESP.getFreeSketchSpace());
}

void test_access_point_starts() {
  WiFi.mode(WIFI_AP);
  TEST_ASSERT_TRUE(WiFi.softAP("obd1-smoke-test", "test1234"));
  WiFi.softAPdisconnect(true);
}

void setup() {
  delay(2000);  // give USB-CDC time to enumerate so results reach the host
  UNITY_BEGIN();
  RUN_TEST(test_littlefs_mounts);
  RUN_TEST(test_running_on_ota_slot_with_a_spare);
  RUN_TEST(test_spiffs_partition_present_and_sized);
  RUN_TEST(test_room_to_flash_next_image);
  RUN_TEST(test_access_point_starts);
  UNITY_END();
}

void loop() {}

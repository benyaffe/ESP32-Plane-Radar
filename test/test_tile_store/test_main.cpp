// Unity tests for data::tile::TileStore — the in-memory cache with
// flash-fallback that firmware renders read from.
//
// The tests use synthetic byte buffers (not real tiles) to keep the
// assertions about caching behavior clean; separate tests
// (test_fallback_tile, test_tile_reader) cover byte-format concerns.
//
// Run via `pio test -e native_test`.

#include <cstdint>
#include <cstring>
#include <unity.h>

#include "data/fallback_tile.h"
#include "data/tile_store.h"

#define NATIVE_STUBS_DEFINE
#include "../common/native_stubs.h"

void setUp(void) {}
void tearDown(void) {}

// Tiny sentinel buffers — the byte content only has to be distinct
// enough for the tests to notice which one was returned.
static const uint8_t kTileA[] = {0xAA, 0xAA, 0xAA, 0xAA};
static const uint8_t kTileB[] = {0xBB, 0xBB, 0xBB, 0xBB};
static const uint8_t kTileC[] = {0xCC, 0xCC, 0xCC, 0xCC};
static const uint8_t kTileD[] = {0xDD, 0xDD, 0xDD, 0xDD};
static const uint8_t kTileE[] = {0xEE, 0xEE, 0xEE, 0xEE};

void test_get_on_empty_cache_returns_fallback(void) {
  data::tile::TileStore s;
  auto b = s.get(7, 20, 37);
  TEST_ASSERT_TRUE(b.is_fallback);
  TEST_ASSERT_EQUAL_PTR(data::tile::kFallbackTile, b.data);
  TEST_ASSERT_EQUAL_UINT32(data::tile::kFallbackTileSize, b.size);
  TEST_ASSERT_EQUAL_UINT32(0, s.cachedCount());
}

void test_put_then_get_returns_cached_bytes(void) {
  data::tile::TileStore s;
  TEST_ASSERT_TRUE(s.put(7, 20, 37, kTileA, sizeof(kTileA)));
  auto b = s.get(7, 20, 37);
  TEST_ASSERT_FALSE(b.is_fallback);
  TEST_ASSERT_EQUAL_UINT32(sizeof(kTileA), b.size);
  TEST_ASSERT_EQUAL_MEMORY(kTileA, b.data, sizeof(kTileA));
  TEST_ASSERT_EQUAL_UINT32(1, s.cachedCount());
}

void test_put_makes_copy_so_caller_buffer_can_change(void) {
  uint8_t caller_buf[] = {0x11, 0x22, 0x33, 0x44};
  data::tile::TileStore s;
  s.put(7, 20, 37, caller_buf, sizeof(caller_buf));
  // Mutate caller's buffer AFTER put(): cached bytes must be unaffected.
  caller_buf[0] = 0x99;
  caller_buf[3] = 0x99;
  auto b = s.get(7, 20, 37);
  TEST_ASSERT_EQUAL_UINT8(0x11, b.data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x44, b.data[3]);
}

void test_put_same_key_twice_updates_in_place(void) {
  data::tile::TileStore s;
  s.put(7, 20, 37, kTileA, sizeof(kTileA));
  s.put(7, 20, 37, kTileB, sizeof(kTileB));
  TEST_ASSERT_EQUAL_UINT32(1, s.cachedCount());
  auto b = s.get(7, 20, 37);
  TEST_ASSERT_EQUAL_MEMORY(kTileB, b.data, sizeof(kTileB));
}

void test_get_wrong_key_falls_back(void) {
  data::tile::TileStore s;
  s.put(7, 20, 37, kTileA, sizeof(kTileA));
  auto b = s.get(7, 20, 99);
  TEST_ASSERT_TRUE(b.is_fallback);
}

void test_cache_evicts_lru_when_full(void) {
  data::tile::TileStore s;
  // Fill all 4 slots.
  s.put(7, 0, 0, kTileA, sizeof(kTileA));
  s.put(7, 0, 1, kTileB, sizeof(kTileB));
  s.put(7, 0, 2, kTileC, sizeof(kTileC));
  s.put(7, 0, 3, kTileD, sizeof(kTileD));
  TEST_ASSERT_EQUAL_UINT32(4, s.cachedCount());

  // Access A, B, C so they're MRU. D remains LRU.
  s.get(7, 0, 0);
  s.get(7, 0, 1);
  s.get(7, 0, 2);

  // Insert E — should evict D.
  s.put(7, 0, 4, kTileE, sizeof(kTileE));
  TEST_ASSERT_EQUAL_UINT32(4, s.cachedCount());

  TEST_ASSERT_FALSE(s.get(7, 0, 0).is_fallback);  // A still there
  TEST_ASSERT_FALSE(s.get(7, 0, 1).is_fallback);
  TEST_ASSERT_FALSE(s.get(7, 0, 2).is_fallback);
  TEST_ASSERT_TRUE(s.get(7, 0, 3).is_fallback);   // D evicted
  TEST_ASSERT_FALSE(s.get(7, 0, 4).is_fallback);  // E cached
}

void test_clear_drops_all_cached_entries(void) {
  data::tile::TileStore s;
  s.put(7, 0, 0, kTileA, sizeof(kTileA));
  s.put(7, 0, 1, kTileB, sizeof(kTileB));
  TEST_ASSERT_EQUAL_UINT32(2, s.cachedCount());
  s.clear();
  TEST_ASSERT_EQUAL_UINT32(0, s.cachedCount());
  TEST_ASSERT_TRUE(s.get(7, 0, 0).is_fallback);
}

void test_put_reject_null_or_zero(void) {
  data::tile::TileStore s;
  TEST_ASSERT_FALSE(s.put(0, 0, 0, nullptr, 4));
  TEST_ASSERT_FALSE(s.put(0, 0, 0, kTileA, 0));
  TEST_ASSERT_EQUAL_UINT32(0, s.cachedCount());
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_get_on_empty_cache_returns_fallback);
  RUN_TEST(test_put_then_get_returns_cached_bytes);
  RUN_TEST(test_put_makes_copy_so_caller_buffer_can_change);
  RUN_TEST(test_put_same_key_twice_updates_in_place);
  RUN_TEST(test_get_wrong_key_falls_back);
  RUN_TEST(test_cache_evicts_lru_when_full);
  RUN_TEST(test_clear_drops_all_cached_entries);
  RUN_TEST(test_put_reject_null_or_zero);
  return UNITY_END();
}

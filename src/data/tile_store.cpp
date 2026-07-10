#include "data/tile_store.h"

#include <cstdlib>
#include <cstring>

#include "data/fallback_tile.h"

namespace data::tile {

TileStore::TileStore() {
  for (auto& e : entries_) {
    e.used = false;
    e.buffer = nullptr;
    e.size = 0;
  }
}

TileStore::~TileStore() {
  clear();
}

int TileStore::findEntry(uint8_t z, uint16_t x, uint16_t y) const {
  for (size_t i = 0; i < kTileCacheCapacity; ++i) {
    const Entry& e = entries_[i];
    if (e.used && e.z == z && e.x == x && e.y == y) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int TileStore::findLruSlot() const {
  // Prefer an unused slot; otherwise the oldest last_used_tick.
  int lru = 0;
  uint32_t oldest = 0xFFFFFFFFu;
  for (size_t i = 0; i < kTileCacheCapacity; ++i) {
    const Entry& e = entries_[i];
    if (!e.used) {
      return static_cast<int>(i);
    }
    if (e.last_used_tick < oldest) {
      oldest = e.last_used_tick;
      lru = static_cast<int>(i);
    }
  }
  return lru;
}

TileBytes TileStore::get(uint8_t z, uint16_t x, uint16_t y) {
  const int idx = findEntry(z, x, y);
  if (idx >= 0) {
    entries_[idx].last_used_tick = ++tick_;
    return TileBytes{entries_[idx].buffer, entries_[idx].size, false};
  }
  return TileBytes{kFallbackTile, kFallbackTileSize, true};
}

bool TileStore::put(uint8_t z, uint16_t x, uint16_t y,
                     const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) return false;

  // Update-in-place if this key already exists — otherwise a repeated
  // put() for the same tile would evict a *different* cache entry
  // instead of refreshing itself.
  int slot = findEntry(z, x, y);
  if (slot < 0) {
    slot = findLruSlot();
    Entry& victim = entries_[slot];
    if (victim.used && victim.buffer != nullptr) {
      std::free(victim.buffer);
      victim.buffer = nullptr;
    }
  } else {
    // Same key: reuse the buffer if it fits, otherwise realloc.
    if (entries_[slot].size != size) {
      std::free(entries_[slot].buffer);
      entries_[slot].buffer = nullptr;
    }
  }

  Entry& e = entries_[slot];
  if (e.buffer == nullptr) {
    e.buffer = static_cast<uint8_t*>(std::malloc(size));
    if (e.buffer == nullptr) {
      e.used = false;
      e.size = 0;
      return false;
    }
  }
  std::memcpy(e.buffer, data, size);
  e.used = true;
  e.z = z;
  e.x = x;
  e.y = y;
  e.size = size;
  e.last_used_tick = ++tick_;
  return true;
}

size_t TileStore::cachedCount() const {
  size_t n = 0;
  for (const auto& e : entries_) {
    if (e.used) ++n;
  }
  return n;
}

void TileStore::clear() {
  for (auto& e : entries_) {
    if (e.buffer != nullptr) {
      std::free(e.buffer);
      e.buffer = nullptr;
    }
    e.used = false;
    e.size = 0;
  }
  tick_ = 0;
}

}  // namespace data::tile

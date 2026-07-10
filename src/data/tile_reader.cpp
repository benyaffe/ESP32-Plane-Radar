#include "data/tile_reader.h"

namespace data::tile {

namespace {

// Header layout (12 bytes) — see scripts/tile_format.py.
constexpr size_t kHeaderSize = 12;
constexpr size_t kIndexEntrySize = 12;

// Read little-endian scalars via memcpy — safe against misaligned
// access on RISC-V / Cortex-M targets where the tile bytes come from
// a SPIFFS or HTTP buffer with no alignment guarantees.
uint16_t readU16(const uint8_t* p) {
  uint16_t v;
  std::memcpy(&v, p, 2);
  return v;
}

uint32_t readU32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

int32_t readI32(const uint8_t* p) {
  int32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

}  // namespace

bool TileReader::init(const uint8_t* data, size_t size) {
  data_ = nullptr;
  size_ = 0;
  section_count_ = 0;
  if (data == nullptr || size < kHeaderSize) {
    return false;
  }
  if (readU32(data) != kMagic) {
    return false;
  }
  const uint8_t version = data[4];
  if (version != kVersion) {
    return false;
  }
  const uint8_t z = data[5];
  const uint16_t x = readU16(data + 6);
  const uint16_t y = readU16(data + 8);
  const uint8_t sec_count = data[10];
  if (sec_count > kMaxSections) {
    return false;
  }
  const size_t index_size = static_cast<size_t>(sec_count) * kIndexEntrySize;
  if (size < kHeaderSize + index_size) {
    return false;
  }
  for (uint8_t i = 0; i < sec_count; ++i) {
    const uint8_t* entry = data + kHeaderSize + i * kIndexEntrySize;
    const uint8_t kind_byte = entry[0];
    // Bytes 1..3 reserved. Payload offset + length follow.
    const uint32_t offset = readU32(entry + 4);
    const uint32_t length = readU32(entry + 8);
    if (offset < kHeaderSize + index_size) {
      return false;  // section would overlap header/index
    }
    if (offset > size || length > size - offset) {
      return false;  // section extends past end of buffer
    }
    sections_[i].kind = static_cast<Section>(kind_byte);
    sections_[i].offset = offset;
    sections_[i].length = length;
  }
  data_ = data;
  size_ = size;
  z_ = z;
  x_ = x;
  y_ = y;
  section_count_ = sec_count;
  return true;
}

const uint8_t* TileReader::sectionBegin(Section kind, uint32_t* length_out) const {
  for (uint8_t i = 0; i < section_count_; ++i) {
    if (sections_[i].kind == kind) {
      if (length_out) *length_out = sections_[i].length;
      return data_ + sections_[i].offset;
    }
  }
  if (length_out) *length_out = 0;
  return nullptr;
}

bool TileReader::readSectionCount(const uint8_t** cursor, const uint8_t* end,
                                    uint16_t* count_out) {
  if (*cursor + 2 > end) return false;
  *count_out = readU16(*cursor);
  *cursor += 2;
  return true;
}

bool TileReader::readPolyline(const uint8_t** cursor, const uint8_t* end,
                                PolylineView* out) {
  // 2 (count) + 4*4 (bbox) = 18-byte prefix.
  if (*cursor + 18 > end) return false;
  const uint16_t pc = readU16(*cursor);
  const int32_t mn_lat = readI32(*cursor + 2);
  const int32_t mn_lon = readI32(*cursor + 6);
  const int32_t mx_lat = readI32(*cursor + 10);
  const int32_t mx_lon = readI32(*cursor + 14);
  const uint8_t* points = *cursor + 18;
  const size_t points_bytes = static_cast<size_t>(pc) * 8;
  if (points + points_bytes > end) return false;
  out->point_count = pc;
  out->bbox_min_lat_e7 = mn_lat;
  out->bbox_min_lon_e7 = mn_lon;
  out->bbox_max_lat_e7 = mx_lat;
  out->bbox_max_lon_e7 = mx_lon;
  out->points_begin = points;
  *cursor = points + points_bytes;
  return true;
}

bool TileReader::readAirport(const uint8_t** cursor, const uint8_t* end,
                               AirportView* out) {
  // 4 (lat) + 4 (lon) + 1 (flags) + 8 (ident) + 1 (runway_count) = 18-byte prefix.
  if (*cursor + 18 > end) return false;
  out->lat_e7 = readI32(*cursor + 0);
  out->lon_e7 = readI32(*cursor + 4);
  out->flags = *(*cursor + 8);
  std::memcpy(out->ident, *cursor + 9, 8);
  out->ident[8] = '\0';
  // Also null-terminate at the first embedded 0 so callers get a
  // proper C-string even for short idents (e.g. "X1\0\0\0\0\0\0").
  for (int i = 0; i < 8; ++i) {
    if (out->ident[i] == '\0') break;
  }
  out->runway_count = *(*cursor + 17);
  const uint8_t* runways = *cursor + 18;
  const size_t runways_bytes = static_cast<size_t>(out->runway_count) * 16;
  if (runways + runways_bytes > end) return false;
  out->runways_begin = runways;
  *cursor = runways + runways_bytes;
  return true;
}

}  // namespace data::tile

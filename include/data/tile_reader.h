// C++ reader for the binary tile format produced by scripts/tile_format.py.
//
// Zero-copy: init() takes a pointer + length to an existing byte buffer
// (SPIFFS mmap, embedded flash constant, network buffer) and never
// allocates. Iteration returns views that borrow pointers into that
// buffer — callers must not use them after the buffer goes away.
//
// Alignment: the on-wire format packs int32s directly with no padding,
// so section offsets are not guaranteed to align to 4 bytes. All scalar
// reads go through memcpy to stay safe on architectures (including
// ESP32-C3 RISC-V) where misaligned int32 loads are slow or trap.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace data::tile {

// Wire-format constants — MUST stay in sync with scripts/tile_format.py.
// Regression test: tests/test_tile_reader_fixture.py.
constexpr uint32_t kMagic = 0x31545250u;  // "PRT1" little-endian
constexpr uint8_t kVersion = 1;
constexpr uint8_t kMaxSections = 4;

enum class Section : uint8_t {
  Coast = 0,
  Land = 1,
  Water = 2,
  Airports = 3,
};

// One polyline within a section. `points_begin` is the first byte of
// the packed (int32 lat, int32 lon) pair sequence, `point_count` pairs
// long.
struct PolylineView {
  uint16_t point_count;
  int32_t bbox_min_lat_e7;
  int32_t bbox_min_lon_e7;
  int32_t bbox_max_lat_e7;
  int32_t bbox_max_lon_e7;
  const uint8_t* points_begin;

  void getPoint(size_t i, int32_t* lat_e7, int32_t* lon_e7) const {
    std::memcpy(lat_e7, points_begin + i * 8, 4);
    std::memcpy(lon_e7, points_begin + i * 8 + 4, 4);
  }
};

// One airport within the airports section. `runways_begin` points at
// packed (int32 lat1, int32 lon1, int32 lat2, int32 lon2) 4-tuples,
// `runway_count` of them.
struct AirportView {
  int32_t lat_e7;
  int32_t lon_e7;
  uint8_t flags;
  char ident[9];  // null-terminated (ident up to 8 chars + '\0')
  uint8_t runway_count;
  const uint8_t* runways_begin;

  uint8_t tier() const { return flags & 0x03; }
  bool instrumentApproach() const { return (flags & 0x04) != 0; }

  void getRunway(size_t i, int32_t* la1_e7, int32_t* lo1_e7,
                 int32_t* la2_e7, int32_t* lo2_e7) const {
    const uint8_t* p = runways_begin + i * 16;
    std::memcpy(la1_e7, p + 0, 4);
    std::memcpy(lo1_e7, p + 4, 4);
    std::memcpy(la2_e7, p + 8, 4);
    std::memcpy(lo2_e7, p + 12, 4);
  }
};

class TileReader {
 public:
  // Parse the header + section index. Returns false on any format
  // violation (bad magic, wrong version, truncated header, section
  // offset past end of buffer). Safe to call on any bytes.
  bool init(const uint8_t* data, size_t size);

  uint8_t z() const { return z_; }
  uint16_t x() const { return x_; }
  uint16_t y() const { return y_; }
  uint8_t sectionCount() const { return section_count_; }

  // Returns the first byte of the given section's payload, or nullptr
  // if the section isn't present. `length_out` receives the payload
  // length (0 if absent).
  const uint8_t* sectionBegin(Section kind, uint32_t* length_out) const;

  // Reads the uint16 polyline_count / airport_count that precedes a
  // section's per-entry data. Advances *cursor by 2 bytes. Returns
  // false if not enough bytes remain.
  static bool readSectionCount(const uint8_t** cursor, const uint8_t* end,
                                uint16_t* count_out);

  // Reads one polyline from *cursor. Advances the cursor past its
  // point data. Returns false on truncated / malformed input.
  static bool readPolyline(const uint8_t** cursor, const uint8_t* end,
                            PolylineView* out);

  // Reads one airport from *cursor. Advances the cursor past its
  // runway data. Returns false on truncated / malformed input.
  static bool readAirport(const uint8_t** cursor, const uint8_t* end,
                           AirportView* out);

 private:
  struct SectionEntry {
    Section kind;
    uint32_t offset;
    uint32_t length;
  };

  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  uint8_t z_ = 0;
  uint16_t x_ = 0;
  uint16_t y_ = 0;
  uint8_t section_count_ = 0;
  SectionEntry sections_[kMaxSections] = {};
};

}  // namespace data::tile

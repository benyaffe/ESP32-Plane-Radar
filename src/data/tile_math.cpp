#include "data/tile_math.h"

#include <cmath>

namespace data::tile {

uint32_t tilesPerSide(uint8_t z) {
  return 1u << z;
}

void tileOfLatLon(uint8_t z, double lat, double lon, uint16_t* x, uint16_t* y) {
  const uint32_t n = tilesPerSide(z);
  // Clamp to just inside the poles so ±90 maps to the edge row.
  if (lat > 89.999999) lat = 89.999999;
  if (lat < -89.999999) lat = -89.999999;
  // Wrap longitude into [-180, 180).
  lon = std::fmod(lon + 180.0, 360.0);
  if (lon < 0.0) lon += 360.0;
  lon -= 180.0;

  const double lon_span = 360.0 / n;
  const double lat_span = 180.0 / n;
  long xi = static_cast<long>(std::floor((lon + 180.0) / lon_span));
  long yi = static_cast<long>(std::floor((90.0 - lat) / lat_span));
  if (xi < 0) xi = 0;
  if (xi > static_cast<long>(n - 1)) xi = static_cast<long>(n - 1);
  if (yi < 0) yi = 0;
  if (yi > static_cast<long>(n - 1)) yi = static_cast<long>(n - 1);
  *x = static_cast<uint16_t>(xi);
  *y = static_cast<uint16_t>(yi);
}

}  // namespace data::tile

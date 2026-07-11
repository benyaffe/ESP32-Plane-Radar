"""Tile pyramid geometry for the global map data pipeline.

Equirectangular tiles: at zoom z, the world is divided into 2^z columns × 2^z
rows. y=0 is the north edge (TMS/slippy convention). Each tile covers:
    lon in [-180 + x * 360/2^z, -180 + (x+1) * 360/2^z]
    lat in [ 90 - (y+1) * 180/2^z,  90 - y * 180/2^z]

Kept alongside build_tiles.py rather than embedded in it so both the pipeline
and its tests import the same primitives.
"""
from __future__ import annotations

from dataclasses import dataclass
from math import cos, floor, radians

# The three baked zoom levels + the DP simplification tolerance at each.
# Coarser levels drop detail to keep tile size manageable at continent /
# world scales.
#
# Tile widths at each zoom (at the equator, degrees):
#   z=3 → 45.0°  ≈ 5000 km
#   z=5 → 11.25° ≈ 1250 km
#   z=7 →  2.8°  ≈  310 km  (comfortably covers the widest 25 nm radar view)
#
# z=7 tolerance was tightened from 0.002 → 0.0005 (≈222 m → 55 m) so
# tight river bends — Willamette through Corvallis, Sacramento River
# delta — stay recognizable at the 5-25 nm radar zoom. Tile size grows
# a few KB per z=7 tile with rivers folded in, still well under the
# 128 KB per-tile cap in services::tile_cache.
ZOOM_LEVELS: tuple[int, ...] = (3, 5, 7)
SIMPLIFY_TOL_DEG: dict[int, float] = {
    3: 0.02,
    5: 0.005,
    7: 0.0005,
}


@dataclass(frozen=True)
class TileBounds:
    """Geographic extent of a tile."""

    min_lat: float
    min_lon: float
    max_lat: float
    max_lon: float

    def contains(self, lat: float, lon: float) -> bool:
        return (
            self.min_lat <= lat <= self.max_lat
            and self.min_lon <= lon <= self.max_lon
        )

    def as_bbox(self) -> tuple[float, float, float, float]:
        """Return in the (min_lat, max_lat, min_lon, max_lon) order that
        the existing coastline/land helpers already expect."""
        return (self.min_lat, self.max_lat, self.min_lon, self.max_lon)


def tiles_per_side(z: int) -> int:
    if z < 0:
        raise ValueError(f"zoom must be >= 0, got {z}")
    return 1 << z


def tile_bounds(z: int, x: int, y: int) -> TileBounds:
    """Geographic extent of a single tile."""
    n = tiles_per_side(z)
    if not (0 <= x < n and 0 <= y < n):
        raise ValueError(f"tile ({z}, {x}, {y}) out of range for 2^{z}={n}")
    lon_span = 360.0 / n
    lat_span = 180.0 / n
    min_lon = -180.0 + x * lon_span
    max_lon = min_lon + lon_span
    max_lat = 90.0 - y * lat_span
    min_lat = max_lat - lat_span
    return TileBounds(min_lat, min_lon, max_lat, max_lon)


def tile_of(z: int, lat: float, lon: float) -> tuple[int, int]:
    """Which tile at zoom z contains the given point.

    Latitudes are clamped to (-90, 90) so that the exact poles map to the
    edge tiles rather than raising an off-by-one.
    """
    n = tiles_per_side(z)
    lat = max(-90.0 + 1e-9, min(90.0 - 1e-9, lat))
    lon = ((lon + 180.0) % 360.0) - 180.0
    x = int(floor((lon + 180.0) / (360.0 / n)))
    y = int(floor((90.0 - lat) / (180.0 / n)))
    x = max(0, min(n - 1, x))
    y = max(0, min(n - 1, y))
    return x, y


def tiles_covering(
    z: int,
    center_lat: float,
    center_lon: float,
    radius_km: float,
) -> list[tuple[int, int]]:
    """Every (x, y) tile at zoom z whose bounds touch the disc of the
    given radius around (center_lat, center_lon). Uses an equirectangular
    bbox around the disc (cheap, slightly generous — never misses tiles).

    Antimeridian crossings are unrolled into two east/west queries so the
    result is always a plain (x, y) list at the same zoom.
    """
    km_per_deg_lat = 111.0
    km_per_deg_lon = 111.0 * max(0.05, cos(radians(center_lat)))
    dlat = radius_km / km_per_deg_lat
    dlon = radius_km / km_per_deg_lon
    min_lat = max(-89.999, center_lat - dlat)
    max_lat = min(89.999, center_lat + dlat)
    lon_west = center_lon - dlon
    lon_east = center_lon + dlon

    ranges: list[tuple[float, float]] = []
    if lon_west < -180.0:
        ranges.append((lon_west + 360.0, 180.0))
        ranges.append((-180.0, lon_east))
    elif lon_east > 180.0:
        ranges.append((lon_west, 180.0))
        ranges.append((-180.0, lon_east - 360.0))
    else:
        ranges.append((lon_west, lon_east))

    # Nudge the eastern edge just inside 180° so tile_of()'s longitude
    # wrap (180 → -180) doesn't send us to the far-west column.
    def _tile_of_eastern_edge(lat: float, lon: float) -> tuple[int, int]:
        return tile_of(z, lat, min(lon, 180.0 - 1e-9))

    result: set[tuple[int, int]] = set()
    for lon_lo, lon_hi in ranges:
        x_lo, y_hi = tile_of(z, min_lat, lon_lo)
        x_hi, y_lo = _tile_of_eastern_edge(max_lat, lon_hi)
        for x in range(x_lo, x_hi + 1):
            for y in range(y_lo, y_hi + 1):
                result.add((x, y))
    return sorted(result)


def tile_relative_path(z: int, x: int, y: int) -> str:
    """The on-disk / on-URL path for a tile file. Kept in one place so
    the firmware, website, and generator can't drift."""
    return f"tiles/{z}/{x}/{y}.bin"

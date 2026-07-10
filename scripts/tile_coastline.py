"""Per-tile coastline builder.

Takes a Natural Earth 10m coastline GeoJSON (or any GeoJSON with
LineString / MultiLineString features) and distributes it into a
{(z, x, y): [Polyline, ...]} dict at the zoom levels declared in
tile_scheme.ZOOM_LEVELS.

For each polyline in the source:
  1. Compute its bbox once.
  2. Find every tile at the target zoom whose bounds overlap that bbox.
  3. Clip the polyline to each tile's bounds (splitting on exits).
  4. Simplify each surviving sub-polyline at the zoom's tolerance.

The final tile dict is what the caller feeds into tile_format.encode()
alongside the land/water/airport payloads.
"""
from __future__ import annotations

import math
from typing import Iterable

import tile_format as tf
import tile_geo as tg
import tile_scheme as ts

# GeoJSON is (lon, lat) — same as our tile_geo Point convention.
Coords = list[tuple[float, float]]


def _extract_linestrings(features: Iterable[dict]) -> list[Coords]:
    """Flatten LineString + MultiLineString features into a list of
    polylines. Silently skips other geometry types."""
    out: list[Coords] = []
    for feat in features:
        geom = feat.get("geometry") or {}
        gtype = geom.get("type")
        raw = geom.get("coordinates") or []
        if gtype == "LineString":
            out.append([(float(p[0]), float(p[1])) for p in raw])
        elif gtype == "MultiLineString":
            for chunk in raw:
                out.append([(float(p[0]), float(p[1])) for p in chunk])
    return out


def _tile_x_range(z: int, min_lon: float, max_lon: float) -> range:
    n = ts.tiles_per_side(z)
    lon_span = 360.0 / n
    x_lo = max(0, min(n - 1, int(math.floor((min_lon + 180.0) / lon_span))))
    # Nudge the eastern edge just inside 180° so the wrap in tile_of
    # doesn't underflow to the far-west column.
    edge = min(max_lon, 180.0 - 1e-9)
    x_hi = max(0, min(n - 1, int(math.floor((edge + 180.0) / lon_span))))
    return range(x_lo, x_hi + 1)


def _tile_y_range(z: int, min_lat: float, max_lat: float) -> range:
    n = ts.tiles_per_side(z)
    lat_span = 180.0 / n
    y_lo = max(0, min(n - 1, int(math.floor((90.0 - max_lat) / lat_span))))
    y_hi = max(0, min(n - 1, int(math.floor((90.0 - min_lat) / lat_span))))
    return range(y_lo, y_hi + 1)


def distribute_polyline_to_tiles(
    coords: Coords,
    z: int,
    tol_deg: float,
) -> dict[tuple[int, int], list[tf.Polyline]]:
    """One polyline → {(x, y): [Polyline, ...]} at zoom z.

    Clips to each candidate tile's bounds, DP-simplifies the survivor,
    and drops fragments with fewer than 2 points. Polylines that don't
    touch any tile — should be impossible for real-world data but easy
    to hit in tests — return an empty dict.
    """
    if len(coords) < 2:
        return {}
    min_lat, max_lat, min_lon, max_lon = tg.polyline_bbox(coords)

    result: dict[tuple[int, int], list[tf.Polyline]] = {}
    for x in _tile_x_range(z, min_lon, max_lon):
        for y in _tile_y_range(z, min_lat, max_lat):
            bounds = ts.tile_bounds(z, x, y)
            tile_bbox = bounds.as_bbox()
            for clipped in tg.clip_polyline_to_bbox(coords, tile_bbox):
                simplified = tg.dp_simplify(clipped, tol_deg)
                if len(simplified) >= 2:
                    result.setdefault((x, y), []).append(tf.Polyline(list(simplified)))
    return result


def build_coastline_tiles(
    features: Iterable[dict],
    zoom_levels: Iterable[int] = ts.ZOOM_LEVELS,
) -> dict[tuple[int, int, int], list[tf.Polyline]]:
    """Distribute all coastline features across the tile pyramid.

    Returns {(z, x, y): [Polyline, ...]} — tiles with no coastline are
    simply absent from the dict (they'll still be emitted, just without
    a coast section).
    """
    lines = _extract_linestrings(features)
    result: dict[tuple[int, int, int], list[tf.Polyline]] = {}
    for z in zoom_levels:
        tol = ts.SIMPLIFY_TOL_DEG[z]
        for line in lines:
            for (x, y), polys in distribute_polyline_to_tiles(line, z, tol).items():
                result.setdefault((z, x, y), []).extend(polys)
    return result

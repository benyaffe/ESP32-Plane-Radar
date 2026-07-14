"""Shapely-backed drop-in replacements for the naive tile clippers.

The Python-loop Sutherland-Hodgman clip in tile_polygons.py + the DP
simplify in tile_geo.py are fine for Natural Earth 10m (a few thousand
polygon vertices per continent) but grind to a halt on GSHHG full-res
(the North America polygon alone is ~880 k vertices, touches ~500 z=7
tiles, so the per-tile clip is 400M+ vertex operations).

Shapely delegates the clip + simplify to GEOS (C, SSE-vectorized), and
STRtree gives us an O(log N) tile→polygon lookup instead of scanning
all 179 k GSHHG polygons per tile. Total wall-clock at z=7 drops from
"gave up after 15 min" to "a couple of minutes".

Same public API as tile_polygons.build_polygon_tiles +
tile_coastline.build_coastline_tiles so build_tiles.py just imports
these when shapely is available.
"""
from __future__ import annotations

from typing import Iterable

try:
    from shapely import make_valid
    from shapely.geometry import (LineString, MultiLineString, MultiPolygon,
                                   Polygon, box, shape)
    from shapely.strtree import STRtree
except ImportError as e:
    raise ImportError(
        "tile_shapely requires the `shapely` package. Install with "
        "`pip install shapely`."
    ) from e

import tile_format as tf
import tile_scheme as ts


def _sanitize_polygon(poly: Polygon) -> list[Polygon]:
    """Some GSHHG rings self-intersect at fjord entrances and other
    long, thin features. Route every polygon through make_valid() at
    load time so downstream intersection() calls don't blow up with
    'side location conflict' TopologyExceptions mid-bake. Returns a
    list because make_valid can split an invalid polygon into a
    MultiPolygon."""
    if poly.is_valid:
        return [poly]
    fixed = make_valid(poly)
    if fixed.is_empty:
        return []
    if isinstance(fixed, Polygon):
        return [fixed]
    if isinstance(fixed, MultiPolygon):
        return list(fixed.geoms)
    # make_valid can return GeometryCollection with lower-dim parts —
    # keep only the Polygon members.
    if hasattr(fixed, "geoms"):
        return [g for g in fixed.geoms if isinstance(g, Polygon)]
    return []


def _iter_polygons_from_features(features: Iterable[dict]) -> list[Polygon]:
    """Extract every Polygon (and MultiPolygon subpart) as a shapely
    Polygon. Empty polygons are dropped; invalid polygons are healed
    via make_valid so GEOS intersection() doesn't crash mid-bake."""
    out: list[Polygon] = []
    for feat in features:
        geom = feat.get("geometry") or {}
        gtype = geom.get("type")
        if gtype not in ("Polygon", "MultiPolygon"):
            continue
        try:
            g = shape(geom)
        except Exception:
            continue
        if g.is_empty:
            continue
        if isinstance(g, Polygon):
            out.extend(_sanitize_polygon(g))
        elif isinstance(g, MultiPolygon):
            for part in g.geoms:
                out.extend(_sanitize_polygon(part))
    return out


def _iter_linestrings_from_features(features: Iterable[dict]) -> list[LineString]:
    """Extract LineString + MultiLineString feature geometries as
    shapely LineStrings."""
    out: list[LineString] = []
    for feat in features:
        geom = feat.get("geometry") or {}
        gtype = geom.get("type")
        if gtype not in ("LineString", "MultiLineString"):
            continue
        try:
            g = shape(geom)
        except Exception:
            continue
        if g.is_empty:
            continue
        if isinstance(g, LineString):
            out.append(g)
        elif isinstance(g, MultiLineString):
            out.extend(list(g.geoms))
    return out


def _tile_boxes(zoom: int) -> dict[tuple[int, int], Polygon]:
    """Every tile's bounding box at the given zoom as a shapely box.
    Skips tiles that would be empty for polygon clipping (the poles at
    the very top / bottom row have degenerate lat spans in practice)."""
    n = ts.tiles_per_side(zoom)
    out: dict[tuple[int, int], Polygon] = {}
    for x in range(n):
        for y in range(n):
            b = ts.tile_bounds(zoom, x, y)
            # box(minx, miny, maxx, maxy) — (lon, lat)
            out[(x, y)] = box(b.min_lon, b.min_lat, b.max_lon, b.max_lat)
    return out


def _polygon_to_tf_polylines(
    poly: Polygon, tol_deg: float
) -> list[tf.Polyline]:
    """Simplify a shapely Polygon and emit its outer ring(s) as
    tf.Polyline records. Returns a list because a clipped polygon can
    degenerate into multiple disjoint pieces (a peninsula bridge cut
    by the tile edge)."""
    simplified = poly.simplify(tol_deg, preserve_topology=True)
    if simplified.is_empty:
        return []
    if isinstance(simplified, MultiPolygon):
        parts = list(simplified.geoms)
    elif isinstance(simplified, Polygon):
        parts = [simplified]
    else:
        return []
    out: list[tf.Polyline] = []
    for part in parts:
        ring = list(part.exterior.coords)
        # tile_format polylines store OPEN sequences (the renderer
        # closes them for fill). shapely gives us a closed ring with
        # first==last; drop the dup.
        if len(ring) >= 4 and ring[0] == ring[-1]:
            ring = ring[:-1]
        if len(ring) < 3:
            continue
        out.append(tf.Polyline([(float(x), float(y)) for x, y in ring]))
    return out


def _linestring_to_tf_polylines(
    ls, tol_deg: float
) -> list[tf.Polyline]:
    """Simplify and convert a shapely LineString / MultiLineString into
    tf.Polyline records."""
    simplified = ls.simplify(tol_deg, preserve_topology=True)
    if simplified.is_empty:
        return []
    if isinstance(simplified, MultiLineString):
        parts = list(simplified.geoms)
    elif isinstance(simplified, LineString):
        parts = [simplified]
    else:
        return []
    out: list[tf.Polyline] = []
    for part in parts:
        pts = list(part.coords)
        if len(pts) < 2:
            continue
        out.append(tf.Polyline([(float(x), float(y)) for x, y in pts]))
    return out


def build_polygon_tiles(
    features: Iterable[dict],
    zoom_levels: Iterable[int] = ts.ZOOM_LEVELS,
) -> dict[tuple[int, int, int], list[tf.Polyline]]:
    """GSHHG-scale polygon → tile pyramid. Same output shape as
    tile_polygons.build_polygon_tiles."""
    polys = _iter_polygons_from_features(features)
    if not polys:
        return {}
    tree = STRtree(polys)
    result: dict[tuple[int, int, int], list[tf.Polyline]] = {}
    for z in zoom_levels:
        tol = ts.SIMPLIFY_TOL_DEG[z]
        for (x, y), tbox in _tile_boxes(z).items():
            # STRtree query returns indices in shapely 2.x.
            candidate_idxs = tree.query(tbox)
            for idx in candidate_idxs:
                poly = polys[int(idx)]
                if not poly.intersects(tbox):
                    continue
                try:
                    clipped = poly.intersection(tbox)
                except Exception:
                    # Extremely rare with sanitized polys, but silently
                    # skip individual failures rather than nuke the bake.
                    continue
                if clipped.is_empty:
                    continue
                if isinstance(clipped, MultiPolygon):
                    for part in clipped.geoms:
                        for pl in _polygon_to_tf_polylines(part, tol):
                            result.setdefault((z, x, y), []).append(pl)
                elif isinstance(clipped, Polygon):
                    for pl in _polygon_to_tf_polylines(clipped, tol):
                        result.setdefault((z, x, y), []).append(pl)
                # ignore lower-dimensional results (Line, Point) — they
                # can appear when a polygon exactly grazes a tile edge.
    return result


def build_coastline_tiles(
    features: Iterable[dict],
    zoom_levels: Iterable[int] = ts.ZOOM_LEVELS,
) -> dict[tuple[int, int, int], list[tf.Polyline]]:
    """GSHHG-scale LineString → tile pyramid. Same output shape as
    tile_coastline.build_coastline_tiles.

    When we feed derived polygon-boundary polylines through this, shapely's
    intersection cleanly returns just the segments that lie strictly
    inside each tile box — the tile-edge segments (which would otherwise
    render as fake coastlines) are automatically excluded because they
    coincide with the box edge and get dropped as boundary artifacts."""
    lines = _iter_linestrings_from_features(features)
    if not lines:
        return {}
    tree = STRtree(lines)
    result: dict[tuple[int, int, int], list[tf.Polyline]] = {}
    for z in zoom_levels:
        tol = ts.SIMPLIFY_TOL_DEG[z]
        for (x, y), tbox in _tile_boxes(z).items():
            candidate_idxs = tree.query(tbox)
            for idx in candidate_idxs:
                ls = lines[int(idx)]
                if not ls.intersects(tbox):
                    continue
                try:
                    clipped = ls.intersection(tbox)
                except Exception:
                    continue
                if clipped.is_empty:
                    continue
                for pl in _linestring_to_tf_polylines(clipped, tol):
                    result.setdefault((z, x, y), []).append(pl)
    return result


# ── size-cap post-processing ────────────────────────────────────────────────
# The firmware's largest contiguous heap block hovers around 30-40 KB after
# WiFi + 8bpp sprite + mbedTLS peak. Any tile that exceeds that dies at
# std::malloc in tile_fetch and drops the render back to the world fallback
# tile — no local coast/land at that focus. Rebuild dense-metro tiles
# (NYC, London, SF Bay) with looser tolerance / dropped small polygons
# until each fits under the cap.

# Hard cap — a bit under the firmware's practical heap-block ceiling so we
# leave room for HTTP overhead in the response.
DEFAULT_TILE_CAP_BYTES = 32 * 1024

# Sanity limit on how loose Douglas-Peucker tolerance can go before the
# result stops resembling the original coastline. ~0.05° ≈ 5.5 km. Beyond
# this we shift strategy to polygon-drop.
_MAX_SIMPLIFY_TOL_DEG = 0.05


def _resimplify_polyline(p: tf.Polyline, tol_deg: float, closed_ring: bool) -> tf.Polyline | None:
    """Re-simplify a Polyline at a looser tolerance. Returns None if the
    result collapses (fewer than 2 points for a line, 3 for a ring)."""
    pts = p.points
    if len(pts) < 2:
        return p
    try:
        if closed_ring:
            # Ensure closed for Polygon()
            ring = list(pts)
            if ring[0] != ring[-1]:
                ring = ring + [ring[0]]
            if len(ring) < 4:
                return None
            geom = Polygon(ring)
            simplified = geom.simplify(tol_deg, preserve_topology=True)
            if simplified.is_empty:
                return None
            if isinstance(simplified, Polygon):
                out_ring = list(simplified.exterior.coords)
            elif isinstance(simplified, MultiPolygon):
                # Pick the largest part
                biggest = max(simplified.geoms, key=lambda g: g.area)
                out_ring = list(biggest.exterior.coords)
            else:
                return None
            # Drop the closing dup — tile_format polylines are OPEN
            if len(out_ring) >= 4 and out_ring[0] == out_ring[-1]:
                out_ring = out_ring[:-1]
            if len(out_ring) < 3:
                return None
            return tf.Polyline([(float(x), float(y)) for x, y in out_ring])
        else:
            geom = LineString(pts)
            simplified = geom.simplify(tol_deg, preserve_topology=True)
            if simplified.is_empty:
                return None
            if isinstance(simplified, LineString):
                out_pts = list(simplified.coords)
            elif isinstance(simplified, MultiLineString):
                biggest = max(simplified.geoms, key=lambda g: g.length)
                out_pts = list(biggest.coords)
            else:
                return None
            if len(out_pts) < 2:
                return None
            return tf.Polyline([(float(x), float(y)) for x, y in out_pts])
    except Exception:
        return p  # if simplification blows up, keep original


def _resimplify_tile_at(tile: tf.Tile, tol_deg: float) -> tf.Tile:
    """Return a new Tile with land/water treated as closed rings and coast
    as open lines, all re-simplified at `tol_deg`."""
    new_coast = [q for q in
                 (_resimplify_polyline(p, tol_deg, closed_ring=False) for p in tile.coast)
                 if q is not None]
    new_land = [q for q in
                (_resimplify_polyline(p, tol_deg, closed_ring=True) for p in tile.land)
                if q is not None]
    new_water = [q for q in
                 (_resimplify_polyline(p, tol_deg, closed_ring=True) for p in tile.water)
                 if q is not None]
    return tf.Tile(z=tile.z, x=tile.x, y=tile.y,
                   coast=new_coast, land=new_land, water=new_water,
                   airports=tile.airports)


def _drop_smallest_until_fits(tile: tf.Tile, cap_bytes: int) -> tf.Tile:
    """Drop polylines from land → water → coast (in that order) starting
    with the fewest vertices until encoded size fits under cap. Airports
    are cheap so they're never dropped."""
    def bytes_for(t: tf.Tile) -> int:
        return len(tf.encode(t))

    # Sort each layer by vertex count ascending — smallest first (easiest to drop).
    coast = sorted(tile.coast, key=lambda p: len(p.points))
    land = sorted(tile.land, key=lambda p: len(p.points))
    water = sorted(tile.water, key=lambda p: len(p.points))
    # Drop order: water first (least visually critical), then coast, then land
    # (land is the biggest visual signal — save for last).
    drop_targets = [water, coast, land]
    for target in drop_targets:
        while target and bytes_for(tf.Tile(
                z=tile.z, x=tile.x, y=tile.y,
                coast=coast, land=land, water=water,
                airports=tile.airports)) > cap_bytes:
            target.pop(0)  # remove smallest
        if bytes_for(tf.Tile(
                z=tile.z, x=tile.x, y=tile.y,
                coast=coast, land=land, water=water,
                airports=tile.airports)) <= cap_bytes:
            break
    return tf.Tile(z=tile.z, x=tile.x, y=tile.y,
                   coast=coast, land=land, water=water,
                   airports=tile.airports)


def cap_tile_size(tile: tf.Tile, cap_bytes: int = DEFAULT_TILE_CAP_BYTES) -> tuple[tf.Tile, str]:
    """Return `(tile_or_smaller, reason)`. `reason` is 'ok' if already
    under cap, 'simplify:<tol>' if fixed by simplification at that
    tolerance, or 'drop' if polygon-dropping was needed. If nothing worked
    the tile is returned oversized — caller can log and move on."""
    if len(tf.encode(tile)) <= cap_bytes:
        return tile, "ok"

    # Start at zoom's baseline tolerance × 2 and keep doubling.
    tol = ts.SIMPLIFY_TOL_DEG.get(tile.z, 0.001) * 2
    while tol <= _MAX_SIMPLIFY_TOL_DEG:
        candidate = _resimplify_tile_at(tile, tol)
        if len(tf.encode(candidate)) <= cap_bytes:
            return candidate, f"simplify:{tol:.5f}"
        tile = candidate  # keep progressively coarser as base for next round
        tol *= 2

    # Even at max tolerance still oversized — drop smallest polygons.
    dropped = _drop_smallest_until_fits(tile, cap_bytes)
    if len(tf.encode(dropped)) <= cap_bytes:
        return dropped, "drop"
    return dropped, "oversized"

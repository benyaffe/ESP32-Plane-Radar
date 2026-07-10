"""Pure geometry helpers shared across the tile builder modules.

Kept separate from build_coastlines.py (which will be deleted in a
later milestone) so the new tile pipeline doesn't inherit a dependency
that's about to disappear.
"""
from __future__ import annotations

import math

Point = tuple[float, float]  # (lon, lat)


def polyline_bbox(coords: list[Point]) -> tuple[float, float, float, float]:
    """Min/max lat and lon of a polyline. (min_lat, max_lat, min_lon, max_lon)."""
    lats = [p[1] for p in coords]
    lons = [p[0] for p in coords]
    return min(lats), max(lats), min(lons), max(lons)


def bboxes_overlap(
    a: tuple[float, float, float, float],
    b: tuple[float, float, float, float],
) -> bool:
    """Both bboxes in (min_lat, max_lat, min_lon, max_lon) form."""
    return not (a[1] < b[0] or a[0] > b[1] or a[3] < b[2] or a[2] > b[3])


def clip_polyline_to_bbox(
    coords: list[Point], bbox: tuple[float, float, float, float]
) -> list[list[Point]]:
    """Split a polyline at bbox exits, keeping only sub-polylines with
    >=2 points inside the bbox. bbox = (min_lat, max_lat, min_lon, max_lon).

    This mirrors the current build_coastlines.py behavior exactly so
    the visual output of the new pipeline matches the baked Bay Area
    coastline it will replace.
    """
    min_lat, max_lat, min_lon, max_lon = bbox
    out: list[list[Point]] = []
    current: list[Point] = []
    for lon, lat in coords:
        if min_lat <= lat <= max_lat and min_lon <= lon <= max_lon:
            current.append((lon, lat))
        elif len(current) >= 2:
            out.append(current)
            current = []
        else:
            current = []
    if len(current) >= 2:
        out.append(current)
    return out


def _perp_dist(p: Point, a: Point, b: Point) -> float:
    ax, ay = a
    bx, by = b
    px, py = p
    den = math.hypot(by - ay, bx - ax)
    if den == 0:
        return math.hypot(px - ax, py - ay)
    num = abs((by - ay) * px - (bx - ax) * py + bx * ay - by * ax)
    return num / den


def dp_simplify(points: list[Point], tol: float) -> list[Point]:
    """Iterative Douglas-Peucker (avoids recursion-depth issues on long
    polylines). Preserves endpoints; drops interior points closer than
    `tol` to the line between their neighbors on either side.
    """
    if len(points) < 3:
        return list(points)
    keep = [False] * len(points)
    keep[0] = True
    keep[-1] = True
    stack: list[tuple[int, int]] = [(0, len(points) - 1)]
    while stack:
        lo, hi = stack.pop()
        if hi - lo < 2:
            continue
        dmax = 0.0
        idx = -1
        for i in range(lo + 1, hi):
            d = _perp_dist(points[i], points[lo], points[hi])
            if d > dmax:
                dmax = d
                idx = i
        if dmax > tol and idx >= 0:
            keep[idx] = True
            stack.append((lo, idx))
            stack.append((idx, hi))
    return [p for p, k in zip(points, keep) if k]

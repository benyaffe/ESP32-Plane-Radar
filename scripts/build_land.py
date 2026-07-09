#!/usr/bin/env python3
"""Build baked land triangles from Natural Earth 1:10m.

Pulls ne_10m_land + ne_10m_minor_islands (public domain), clips polygon
exterior rings to a bounding box around the radar center, Douglas-Peucker
simplifies, ear-clips into triangles, and emits src/data/land_data.cpp.

Runtime uses these triangles with LGFX fillTriangle to tint the landmass
so it's visually distinguishable from water. Same generator pattern as
build_coastlines.py — committed generator + committed output.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE_DIR = ROOT / ".local-data"
LAND_GEOJSON = CACHE_DIR / "ne_10m_land.geojson"
ISLANDS_GEOJSON = CACHE_DIR / "ne_10m_minor_islands.geojson"
OUT_H = ROOT / "include" / "data" / "land.h"
OUT_CPP = ROOT / "src" / "data" / "land_data.cpp"

LAND_URL = (
    "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/"
    "geojson/ne_10m_land.geojson"
)
ISLANDS_URL = (
    "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/"
    "geojson/ne_10m_minor_islands.geojson"
)

DEFAULT_CENTER_LAT = 37.7552
DEFAULT_CENTER_LON = -122.4528
DEFAULT_RADIUS_KM = 200.0
# Slightly coarser than coastlines: triangles don't need as much detail as
# the outline stroke, and each vertex costs 8 B in the baked table.
DEFAULT_SIMPLIFY_TOL_DEG = 0.004
KM_PER_DEG = 111.0


def download(url: Path, cache: Path) -> None:
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    if cache.exists():
        return
    print(f"Downloading {url} → {cache}", file=sys.stderr)
    urllib.request.urlretrieve(url, cache)


def clip_ring_to_bbox(ring, bbox):
    """Sutherland–Hodgman polygon clip against an axis-aligned bbox."""
    min_lat, max_lat, min_lon, max_lon = bbox

    def clip_edge(poly, keep):
        out = []
        n = len(poly)
        if n == 0:
            return out
        for i in range(n):
            a = poly[i - 1]
            b = poly[i]
            a_in = keep(a)
            b_in = keep(b)
            if b_in:
                if not a_in:
                    out.append(intersect(a, b, keep))
                out.append(b)
            elif a_in:
                out.append(intersect(a, b, keep))
        return out

    def intersect(a, b, keep):
        # binary search along a→b for the boundary crossing (edge is axis-
        # aligned, so we can solve directly)
        ax, ay = a
        bx, by = b
        # Determine which edge we're clipping against by which coordinate flips
        # Actually simpler: try each of 4 edges; we know only one boundary is
        # being crossed for this call.
        for lo, hi, dim in (
            (min_lon, max_lon, 0),
            (min_lat, max_lat, 1),
        ):
            a_out_lo = a[dim] < lo
            b_out_lo = b[dim] < lo
            a_out_hi = a[dim] > hi
            b_out_hi = b[dim] > hi
            if a_out_lo != b_out_lo:
                t = (lo - a[dim]) / (b[dim] - a[dim])
                return (ax + t * (bx - ax), ay + t * (by - ay))
            if a_out_hi != b_out_hi:
                t = (hi - a[dim]) / (b[dim] - a[dim])
                return (ax + t * (bx - ax), ay + t * (by - ay))
        # Fallback: shouldn't hit
        return b

    poly = list(ring)
    poly = clip_edge(poly, lambda p: p[0] >= min_lon)
    poly = clip_edge(poly, lambda p: p[0] <= max_lon)
    poly = clip_edge(poly, lambda p: p[1] >= min_lat)
    poly = clip_edge(poly, lambda p: p[1] <= max_lat)
    return poly


def _perp_dist(p, a, b):
    ax, ay = a
    bx, by = b
    px, py = p
    den = math.hypot(by - ay, bx - ax)
    if den == 0:
        return math.hypot(px - ax, py - ay)
    num = abs((by - ay) * px - (bx - ax) * py + bx * ay - by * ax)
    return num / den


def dp_simplify(points, tol):
    if len(points) < 4:
        return list(points)
    keep = [False] * len(points)
    keep[0] = True
    keep[-1] = True
    stack = [(0, len(points) - 1)]
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


# --- Ear-clipping triangulation for a simple polygon ------------------------
# Standard O(n²) ear-clip. Fine for our polygon sizes (typically < 500 verts
# after simplification for the Bay Area).

def _signed_area(poly):
    a = 0.0
    n = len(poly)
    for i in range(n):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % n]
        a += (x0 * y1) - (x1 * y0)
    return a * 0.5


def _point_in_triangle(p, a, b, c):
    # Barycentric with a small epsilon so points strictly inside count.
    ax, ay = a
    bx, by = b
    cx, cy = c
    px, py = p
    d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
    if d == 0:
        return False
    l1 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / d
    l2 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / d
    l3 = 1.0 - l1 - l2
    return l1 > 1e-9 and l2 > 1e-9 and l3 > 1e-9


def _is_convex(a, b, c, ccw):
    # Cross product of (b-a) × (c-b). >0 => left turn (CCW).
    cross = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])
    return (cross > 0) if ccw else (cross < 0)


def earclip(poly):
    """Ear-clip a simple polygon into triangles. Returns list of 3-tuples of
    vertex indices into `poly`."""
    n = len(poly)
    if n < 3:
        return []
    ccw = _signed_area(poly) > 0
    indices = list(range(n))
    triangles = []
    guard = 0
    max_guard = 4 * n * n  # safety net for pathological input
    while len(indices) > 3:
        guard += 1
        if guard > max_guard:
            break
        m = len(indices)
        ear_found = False
        for k in range(m):
            i_prev = indices[(k - 1) % m]
            i_curr = indices[k]
            i_next = indices[(k + 1) % m]
            a = poly[i_prev]
            b = poly[i_curr]
            c = poly[i_next]
            if not _is_convex(a, b, c, ccw):
                continue
            # Ear must not contain any other vertex
            has_inside = False
            for j in indices:
                if j in (i_prev, i_curr, i_next):
                    continue
                if _point_in_triangle(poly[j], a, b, c):
                    has_inside = True
                    break
            if has_inside:
                continue
            triangles.append((i_prev, i_curr, i_next))
            indices.pop(k)
            ear_found = True
            break
        if not ear_found:
            # Degenerate polygon (self-intersecting or duplicate vertices) —
            # give up on the remainder rather than infinite-looping.
            break
    if len(indices) == 3:
        triangles.append((indices[0], indices[1], indices[2]))
    return triangles


def extract_rings(features, bbox):
    """Yield exterior rings from a set of GeoJSON Polygon/MultiPolygon
    features, clipped to bbox. Interior rings (holes) are ignored — for the
    Bay Area at 200 km radius they're negligible."""
    for feat in features:
        geom = feat.get("geometry") or {}
        t = geom.get("type")
        coords = geom.get("coordinates") or []
        if t == "Polygon":
            polys = [coords]
        elif t == "MultiPolygon":
            polys = coords
        else:
            continue
        for poly in polys:
            if not poly:
                continue
            ext = poly[0]  # exterior ring
            clipped = clip_ring_to_bbox(ext, bbox)
            if len(clipped) >= 3:
                yield clipped


def build(center_lat, center_lon, radius_km, tol_deg):
    download(LAND_URL, LAND_GEOJSON)
    download(ISLANDS_URL, ISLANDS_GEOJSON)

    lat_margin = radius_km / KM_PER_DEG
    lon_margin = radius_km / (KM_PER_DEG * math.cos(math.radians(center_lat)))
    bbox = (
        center_lat - lat_margin,
        center_lat + lat_margin,
        center_lon - lon_margin,
        center_lon + lon_margin,
    )

    rings: list[list[tuple[float, float]]] = []
    for path in (LAND_GEOJSON, ISLANDS_GEOJSON):
        data = json.loads(path.read_text())
        for ring in extract_rings(data.get("features", []), bbox):
            simplified = dp_simplify(ring, tol_deg)
            if len(simplified) >= 3:
                rings.append(simplified)
    return rings, bbox


def emit(rings, bbox, center_lat, center_lon, radius_km, tol_deg):
    all_verts: list[tuple[int, int]] = []
    all_triangles: list[tuple[int, int, int]] = []
    dropped = 0
    for ring in rings:
        base = len(all_verts)
        for lon, lat in ring:
            all_verts.append((int(round(lat * 1e7)), int(round(lon * 1e7))))
        tris = earclip(ring)
        if not tris:
            dropped += 1
        for a, b, c in tris:
            all_triangles.append((base + a, base + b, base + c))

    if len(all_verts) > 0xFFFF:
        raise SystemExit(
            f"vertex count {len(all_verts)} exceeds uint16 range; tighten "
            f"bbox or tolerance"
        )

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text(
        "#pragma once\n"
        "\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "\n"
        "// Baked landmass triangles (Natural Earth 1:10m land + minor islands)\n"
        "// clipped and triangulated around the radar center.\n"
        "// Regenerate with scripts/build_land.py.\n"
        "\n"
        "namespace data::land {\n"
        "\n"
        "struct Vertex {\n"
        "  int32_t lat_e7;\n"
        "  int32_t lon_e7;\n"
        "};\n"
        "\n"
        "struct Triangle {\n"
        "  uint16_t v0;\n"
        "  uint16_t v1;\n"
        "  uint16_t v2;\n"
        "};\n"
        "\n"
        "extern const Vertex kVertices[];\n"
        "extern const Triangle kTriangles[];\n"
        "extern const size_t kVertexCount;\n"
        "extern const size_t kTriangleCount;\n"
        "\n"
        "}  // namespace data::land\n"
    )

    lines: list[str] = []
    lines.append("// Generated by scripts/build_land.py — do not edit.")
    lines.append("// Source: Natural Earth 1:10m land + minor islands (public domain).")
    lines.append(
        f"// Center: ({center_lat:.6f}, {center_lon:.6f})  radius {radius_km:.0f} km"
    )
    lines.append(
        f"// Bbox: lat [{bbox[0]:.4f} .. {bbox[1]:.4f}]  lon [{bbox[2]:.4f} .. {bbox[3]:.4f}]"
    )
    lines.append(
        f"// Simplify tol: {tol_deg}° (~{tol_deg * KM_PER_DEG * 1000:.0f} m).  Dropped rings: {dropped}"
    )
    lines.append(
        f"// Rings: {len(rings)}, vertices: {len(all_verts)}, triangles: {len(all_triangles)}"
    )
    lines.append("")
    lines.append('#include "data/land.h"')
    lines.append("")
    lines.append("namespace data::land {")
    lines.append("")
    lines.append("const Vertex kVertices[] = {")
    for i in range(0, len(all_verts), 4):
        chunk = all_verts[i : i + 4]
        pieces = " ".join(f"{{{lat}, {lon}}}," for (lat, lon) in chunk)
        lines.append(f"    {pieces}")
    lines.append("};")
    lines.append("")
    lines.append("const Triangle kTriangles[] = {")
    for i in range(0, len(all_triangles), 6):
        chunk = all_triangles[i : i + 6]
        pieces = " ".join(f"{{{a},{b},{c}}}," for (a, b, c) in chunk)
        lines.append(f"    {pieces}")
    lines.append("};")
    lines.append("")
    lines.append(f"const size_t kVertexCount = {len(all_verts)};")
    lines.append(f"const size_t kTriangleCount = {len(all_triangles)};")
    lines.append("")
    lines.append("}  // namespace data::land")
    OUT_CPP.parent.mkdir(parents=True, exist_ok=True)
    OUT_CPP.write_text("\n".join(lines) + "\n")

    print(f"Wrote {OUT_H}")
    print(
        f"Wrote {OUT_CPP}: {len(rings)} rings, {len(all_verts)} verts, "
        f"{len(all_triangles)} tris (dropped {dropped})"
    )


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--center", default=f"{DEFAULT_CENTER_LAT},{DEFAULT_CENTER_LON}")
    p.add_argument("--radius-km", type=float, default=DEFAULT_RADIUS_KM)
    p.add_argument("--tol-deg", type=float, default=DEFAULT_SIMPLIFY_TOL_DEG)
    args = p.parse_args()
    lat_str, lon_str = args.center.split(",")
    lat = float(lat_str)
    lon = float(lon_str)
    rings, bbox = build(lat, lon, args.radius_km, args.tol_deg)
    emit(rings, bbox, lat, lon, args.radius_km, args.tol_deg)


if __name__ == "__main__":
    main()

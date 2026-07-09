#!/usr/bin/env python3
"""Bake JSON data files for the web preview.

Reads the same public-domain sources as the firmware bakes (Natural
Earth 1:10m + OurAirports) and emits JSON that the browser can fetch()
without a proxy. Files:

  web/public/data/coastline.json   [[[lon, lat], ...], ...]
  web/public/data/land.json        {vertices: [[lon, lat], ...],
                                    triangles: [[i, j, k], ...]}
  web/public/data/roads.json       [{type, points: [[lon, lat], ...]}, ...]
  web/public/data/airports.json    {"KSFO": {name, lat, lon, city, tier,
                                             runways: [{le, he, lat1, lon1,
                                                        lat2, lon2}, ...]},
                                    ...}
  web/public/data/airport_index.json  compact typeahead payload for all
                                       recognizable US airports:
                                       [[icao, iata, city, name, lat, lon],
                                        ...]

Phase 1: geometric layers clipped to a 200 km bbox around a chosen
center (default: home). Phase 2 will replace those with CONUS-wide
data + client-side clipping — the JSON schemas above are already
compatible with that.

Usage:
  scripts/build_web_data.py                        # SF default
  scripts/build_web_data.py --center 40.6,-73.7    # NYC
"""
from __future__ import annotations

import argparse
import csv
import io
import json
import math
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE_DIR = ROOT / ".local-data"
OUT_DIR = ROOT / "web" / "public" / "data"

DEFAULT_CENTER_LAT = 37.759
DEFAULT_CENTER_LON = -122.409
DEFAULT_RADIUS_KM = 200.0
KM_PER_DEG = 111.0

COASTLINE_URL = (
    "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/"
    "geojson/ne_10m_coastline.geojson"
)
LAND_URL = (
    "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/"
    "geojson/ne_10m_land.geojson"
)
MINOR_ISLANDS_URL = (
    "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/"
    "geojson/ne_10m_minor_islands.geojson"
)
ROADS_URL = (
    "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/"
    "geojson/ne_10m_roads.geojson"
)
AIRPORTS_URL = (
    "https://raw.githubusercontent.com/davidmegginson/ourairports-data/main/"
    "airports.csv"
)
RUNWAYS_URL = (
    "https://raw.githubusercontent.com/davidmegginson/ourairports-data/main/"
    "runways.csv"
)

CACHE_MAP = {
    "coastline": (COASTLINE_URL, CACHE_DIR / "ne_10m_coastline.geojson"),
    "land": (LAND_URL, CACHE_DIR / "ne_10m_land.geojson"),
    "islands": (MINOR_ISLANDS_URL, CACHE_DIR / "ne_10m_minor_islands.geojson"),
    "roads": (ROADS_URL, CACHE_DIR / "ne_10m_roads.geojson"),
}


def cached(url: str, path: Path) -> Path:
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        print(f"downloading {url}", file=sys.stderr)
        urllib.request.urlretrieve(url, path)
    return path


def fetch_csv(url: str) -> list[dict[str, str]]:
    print(f"fetching {url}", file=sys.stderr)
    with urllib.request.urlopen(url, timeout=60) as resp:
        text = resp.read().decode("utf-8")
    return list(csv.DictReader(io.StringIO(text)))


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
    if len(points) < 3:
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


def bbox_from_center(lat: float, lon: float, radius_km: float):
    lat_margin = radius_km / KM_PER_DEG
    lon_margin = radius_km / (KM_PER_DEG * math.cos(math.radians(lat)))
    return (
        lat - lat_margin,
        lat + lat_margin,
        lon - lon_margin,
        lon + lon_margin,
    )


def in_bbox(lon: float, lat: float, bbox) -> bool:
    return bbox[0] <= lat <= bbox[1] and bbox[2] <= lon <= bbox[3]


def clip_polyline(coords, bbox):
    """Same as the firmware: emit sub-polylines when the line exits bbox."""
    out = []
    current = []
    for pt in coords:
        lon, lat = pt[0], pt[1]
        if in_bbox(lon, lat, bbox):
            current.append((lon, lat))
        elif len(current) >= 2:
            out.append(current)
            current = []
        else:
            current = []
    if len(current) >= 2:
        out.append(current)
    return out


def round_pt(p, decimals=5):
    return [round(p[0], decimals), round(p[1], decimals)]


# ---------------------------------------------------------------------------
# Coastline
# ---------------------------------------------------------------------------


def build_coastline(bbox, tol_deg=0.002):
    path = cached(*CACHE_MAP["coastline"])
    data = json.loads(path.read_text())
    out = []
    for feat in data.get("features", []):
        geom = feat.get("geometry") or {}
        gtype = geom.get("type")
        raw = geom.get("coordinates") or []
        if gtype == "LineString":
            chunks = [raw]
        elif gtype == "MultiLineString":
            chunks = raw
        else:
            continue
        for coords in chunks:
            for clipped in clip_polyline(coords, bbox):
                simplified = dp_simplify(clipped, tol_deg)
                if len(simplified) >= 2:
                    out.append([round_pt(p) for p in simplified])
    return out


# ---------------------------------------------------------------------------
# Land — polygons converted to a shared vertex+triangle buffer via ear-clip
# ---------------------------------------------------------------------------


def _sign(p1, p2, p3):
    return (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1])


def _in_tri(p, a, b, c):
    d1 = _sign(p, a, b)
    d2 = _sign(p, b, c)
    d3 = _sign(p, c, a)
    has_neg = (d1 < 0) or (d2 < 0) or (d3 < 0)
    has_pos = (d1 > 0) or (d2 > 0) or (d3 > 0)
    return not (has_neg and has_pos)


def _polygon_area(poly):
    n = len(poly)
    s = 0.0
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        s += (x2 - x1) * (y2 + y1)
    return s


def ear_clip(polygon):
    """Basic ear-clipping for a simple polygon (no holes). Returns list of
    (a, b, c) index triples into the input polygon list."""
    poly = list(polygon)
    if len(poly) < 3:
        return []
    # Ensure CCW (positive signed area = CW in screen coords; for our
    # (lon, lat) we want CCW = positive area under standard math orientation).
    if _polygon_area(poly) < 0:
        poly = list(reversed(poly))
        was_reversed = True
    else:
        was_reversed = False
    idx = list(range(len(poly)))
    triangles = []
    guard = 0
    while len(idx) > 3:
        guard += 1
        if guard > 20000:
            break  # runaway safety
        ear_found = False
        n = len(idx)
        for i in range(n):
            i_prev = idx[(i - 1) % n]
            i_curr = idx[i]
            i_next = idx[(i + 1) % n]
            a, b, c = poly[i_prev], poly[i_curr], poly[i_next]
            if _sign(a, b, c) <= 0:
                continue  # reflex
            contains = False
            for j in idx:
                if j in (i_prev, i_curr, i_next):
                    continue
                if _in_tri(poly[j], a, b, c):
                    contains = True
                    break
            if not contains:
                triangles.append((i_prev, i_curr, i_next))
                idx.pop(i)
                ear_found = True
                break
        if not ear_found:
            break
    if len(idx) == 3:
        triangles.append((idx[0], idx[1], idx[2]))
    if was_reversed:
        n = len(poly)
        triangles = [(n - 1 - a, n - 1 - c, n - 1 - b) for (a, b, c) in triangles]
    return triangles


def _polygon_bbox_overlap(poly, bbox):
    min_lat = min(p[1] for p in poly)
    max_lat = max(p[1] for p in poly)
    min_lon = min(p[0] for p in poly)
    max_lon = max(p[0] for p in poly)
    if max_lat < bbox[0] or min_lat > bbox[1]:
        return False
    if max_lon < bbox[2] or min_lon > bbox[3]:
        return False
    return True


def build_land(bbox, tol_deg=0.003):
    vertices = []  # global (lon, lat) list
    triangles = []  # (v0, v1, v2) indices
    for key in ("land", "islands"):
        path = cached(*CACHE_MAP[key])
        data = json.loads(path.read_text())
        for feat in data.get("features", []):
            geom = feat.get("geometry") or {}
            gtype = geom.get("type")
            raw = geom.get("coordinates") or []
            if gtype == "Polygon":
                polys = [raw]
            elif gtype == "MultiPolygon":
                polys = raw
            else:
                continue
            for poly in polys:
                if not poly:
                    continue
                outer = [(p[0], p[1]) for p in poly[0]]
                if not _polygon_bbox_overlap(outer, bbox):
                    continue
                simplified = dp_simplify(outer, tol_deg)
                if len(simplified) < 3:
                    continue
                # Dedupe closing vertex if present
                if simplified[0] == simplified[-1]:
                    simplified = simplified[:-1]
                base = len(vertices)
                for p in simplified:
                    vertices.append(round_pt(p))
                tris = ear_clip(simplified)
                for (a, b, c) in tris:
                    triangles.append([base + a, base + b, base + c])
    return {"vertices": vertices, "triangles": triangles}


# ---------------------------------------------------------------------------
# Roads — polylines with type
# ---------------------------------------------------------------------------


def build_roads(bbox, tol_deg=0.002, keep_types=("Major Highway", "Secondary Highway")):
    path = cached(*CACHE_MAP["roads"])
    data = json.loads(path.read_text())
    keep = set(keep_types)
    out = []
    for feat in data.get("features", []):
        props = feat.get("properties") or {}
        road_type = props.get("type")
        if road_type not in keep:
            continue
        geom = feat.get("geometry") or {}
        gtype = geom.get("type")
        raw = geom.get("coordinates") or []
        if gtype == "LineString":
            chunks = [raw]
        elif gtype == "MultiLineString":
            chunks = raw
        else:
            continue
        for coords in chunks:
            for clipped in clip_polyline(coords, bbox):
                simplified = dp_simplify(clipped, tol_deg)
                if len(simplified) >= 2:
                    out.append({
                        "type": road_type,
                        "points": [round_pt(p) for p in simplified],
                    })
    return out


# ---------------------------------------------------------------------------
# Airports — full US index for typeahead + detail (runways) for airports
# in the current region.
# ---------------------------------------------------------------------------


AIRPORT_TIER = {
    "large_airport": 3,
    "medium_airport": 2,
    "small_airport": 1,
}


def _is_h_designator(s: str) -> bool:
    if not s or s[0] != "H":
        return False
    rest = s[1:]
    return not rest or rest[0] in "-_" or rest.isdigit()


def _is_helipad(row) -> bool:
    le = (row.get("le_ident") or "").strip().upper()
    he = (row.get("he_ident") or "").strip().upper()
    if not _is_h_designator(le) and not _is_h_designator(he):
        return False
    try:
        length_ft = int(row.get("length_ft") or 0)
    except ValueError:
        length_ft = 0
    if _is_h_designator(le) and _is_h_designator(he):
        return True
    return length_ft < 2500


def build_airports(bbox):
    airports = fetch_csv(AIRPORTS_URL)
    runways = fetch_csv(RUNWAYS_URL)

    # Group runways by airport ICAO
    rw_by_apt = {}
    for r in runways:
        if _is_helipad(r):
            continue
        ident = (r.get("airport_ident") or "").strip()
        try:
            lat1 = float(r["le_latitude_deg"])
            lon1 = float(r["le_longitude_deg"])
            lat2 = float(r["he_latitude_deg"])
            lon2 = float(r["he_longitude_deg"])
        except (KeyError, ValueError, TypeError):
            continue
        rw_by_apt.setdefault(ident, []).append({
            "le": (r.get("le_ident") or "").strip(),
            "he": (r.get("he_ident") or "").strip(),
            "lat1": round(lat1, 5), "lon1": round(lon1, 5),
            "lat2": round(lat2, 5), "lon2": round(lon2, 5),
        })

    # Detailed airports in bbox (for radar-style rendering)
    detailed = {}
    for a in airports:
        atype = a.get("type", "")
        if atype not in AIRPORT_TIER:
            continue
        ident = (a.get("ident") or "").strip()
        if len(ident) != 4 or ident[0] != "K":
            continue  # phase 1: CONUS ICAO codes (K-prefixed)
        try:
            lat = float(a["latitude_deg"])
            lon = float(a["longitude_deg"])
        except (KeyError, ValueError, TypeError):
            continue
        if not in_bbox(lon, lat, bbox):
            continue
        detailed[ident] = {
            "name": a.get("name", ""),
            "city": a.get("municipality", ""),
            "lat": round(lat, 5),
            "lon": round(lon, 5),
            "tier": AIRPORT_TIER[atype],
            "runways": rw_by_apt.get(ident, []),
        }

    # Global US typeahead index: all recognizable airports
    index = []
    for a in airports:
        atype = a.get("type", "")
        tier = AIRPORT_TIER.get(atype, 0)
        # Keep large + medium; small only if scheduled service.
        keep = tier >= 2 or (tier == 1 and a.get("scheduled_service") == "yes")
        if not keep:
            continue
        ident = (a.get("ident") or "").strip()
        if len(ident) != 4 or ident[0] != "K":
            continue
        iata = (a.get("iata_code") or "").strip()
        try:
            lat = float(a["latitude_deg"])
            lon = float(a["longitude_deg"])
        except (KeyError, ValueError, TypeError):
            continue
        # Tuple form keeps the file compact.
        index.append([
            ident,
            iata,
            a.get("municipality", ""),
            a.get("name", ""),
            round(lat, 5),
            round(lon, 5),
        ])
    # Sort by tier desc then by ICAO, so most recognizable airports rank first.
    tier_lookup = {a["ident"]: AIRPORT_TIER.get(a["type"], 0) for a in airports}
    index.sort(key=lambda row: (-tier_lookup.get(row[0], 0), row[0]))

    return detailed, index


# ---------------------------------------------------------------------------
# Entry
# ---------------------------------------------------------------------------


def emit(path: Path, payload) -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, separators=(",", ":")))
    size = path.stat().st_size
    print(f"wrote {path.relative_to(ROOT)} ({size/1024:.1f} KB)", file=sys.stderr)


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--center", default=f"{DEFAULT_CENTER_LAT},{DEFAULT_CENTER_LON}")
    p.add_argument("--radius-km", type=float, default=DEFAULT_RADIUS_KM)
    args = p.parse_args()

    lat_str, lon_str = args.center.split(",")
    center_lat, center_lon = float(lat_str), float(lon_str)
    bbox = bbox_from_center(center_lat, center_lon, args.radius_km)

    coast = build_coastline(bbox)
    emit(OUT_DIR / "coastline.json", coast)

    land = build_land(bbox)
    emit(OUT_DIR / "land.json", land)

    roads = build_roads(bbox)
    emit(OUT_DIR / "roads.json", roads)

    detailed, index = build_airports(bbox)
    emit(OUT_DIR / "airports.json", detailed)
    emit(OUT_DIR / "airport_index.json", index)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Bake the global tile pyramid.

Downloads Natural Earth 10m coastline/land/minor_islands/lakes and
OurAirports airports.csv + runways.csv, reads a static list of
FAA-registered instrument-approach airports, runs each layer's
per-tile builder, merges them into per-tile binary files under
web/public/data/tiles/{z}/{x}/{y}.bin.

The heavy geometry lives in tile_{coastline,polygons,airports}.py.
This file is orchestration + I/O only.

Usage:
  scripts/build_tiles.py                 # full pyramid, all zoom levels
  scripts/build_tiles.py --zoom 7        # just the finest zoom
  scripts/build_tiles.py --skip-download # use cached sources under .local-data
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
import urllib.request
from pathlib import Path

import gshhg_loader as gshhg
import tile_airports as ta
import tile_coastline as tc
import tile_format as tf
import tile_polygons as tp
import tile_scheme as ts
import tile_shapely as tsh  # GEOS-backed clip; the pure-Python paths choke on GSHHG-scale polygons

ROOT = Path(__file__).resolve().parents[1]
CACHE_DIR = ROOT / ".local-data"
OUT_DIR = ROOT / "web" / "public" / "data"

# Land + lake polygons come from GSHHG full-resolution shapefiles
# (Wessel & Smith's Global Self-consistent Hierarchical High-resolution
# Geography). Coastline polylines are derived from GSHHG land polygon
# outer rings — GSHHG doesn't ship separate coastlines because polygon
# boundaries ARE the coast. Full-res means Manhattan renders with
# hundreds of vertices instead of Natural Earth 10m's ~8, and wide
# rivers like the Hudson trace correctly as the boundary between the
# continental land polygon and its Manhattan "island" cutout.
NE = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson"
SOURCES = {
    # Inland rivers stay on Natural Earth (GSHHG's WDBII river data is
    # coarser than ne_10m for named rivers).
    "rivers": f"{NE}/ne_10m_rivers_lake_centerlines.geojson",
}
OA = "https://raw.githubusercontent.com/davidmegginson/ourairports-data/main"
AIRPORTS_URL = f"{OA}/airports.csv"
RUNWAYS_URL = f"{OA}/runways.csv"
NAVAIDS_URL = f"{OA}/navaids.csv"


def download(url: str, dest: Path) -> Path:
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    if not dest.exists():
        print(f"downloading {url}", file=sys.stderr)
        urllib.request.urlretrieve(url, dest)
    return dest


def load_geojson_features(url: str) -> list[dict]:
    dest = CACHE_DIR / Path(url).name
    download(url, dest)
    data = json.loads(dest.read_text())
    return data.get("features", [])


def fetch_csv_rows(url: str) -> list[dict]:
    dest = CACHE_DIR / Path(url).name
    download(url, dest)
    with dest.open(newline="") as f:
        return list(csv.DictReader(f))


def build_all_tiles(
    coast_features: list[dict],
    land_features: list[dict],
    water_features: list[dict],
    airport_rows: list[dict],
    runway_rows: list[dict],
    iap_icaos: set[str],
    river_features: list[dict] | None = None,
    zoom_levels: tuple[int, ...] = ts.ZOOM_LEVELS,
) -> list[tf.Tile]:
    """Run every per-layer builder and merge results into complete tiles.

    Empty tiles (no coast / land / water / airports at any zoom) are
    dropped — otherwise we'd write ~16k+ empty files for the ocean at
    z=7.

    Returns a list of Tile objects in deterministic (z, x, y) order so
    the pipeline output is byte-identical for byte-identical inputs.
    """
    # Land + coastline come from GSHHG full-res; both are far too big
    # for the naive per-vertex Sutherland-Hodgman clip. Route through
    # the shapely (GEOS) path — orders of magnitude faster and the
    # intersection cleanly drops the tile-boundary segments that would
    # otherwise render as fake coastlines.
    coast = tsh.build_coastline_tiles(coast_features, zoom_levels)
    # Rivers piggyback on the Coast section: same LineString geometry,
    # same "draw as thin coastline-color polyline" render path on both
    # firmware (coastline_overlay.cpp) and web (drawCoastline in
    # renderer.ts). The pure-Python coastline builder is fine here —
    # Natural Earth 10m rivers are far below GSHHG scale.
    if river_features:
        river_tiles = tc.build_coastline_tiles(river_features, zoom_levels)
        for key, polys in river_tiles.items():
            coast.setdefault(key, []).extend(polys)
    land = tsh.build_polygon_tiles(land_features, zoom_levels)
    water = tsh.build_polygon_tiles(water_features, zoom_levels)
    airports = ta.build_airport_tiles(
        airport_rows, runway_rows, iap_icaos, zoom_levels
    )

    keys = set(coast) | set(land) | set(water) | set(airports)
    tiles: list[tf.Tile] = []
    for (z, x, y) in sorted(keys):
        tiles.append(
            tf.Tile(
                z=z,
                x=x,
                y=y,
                coast=coast.get((z, x, y), []),
                land=land.get((z, x, y), []),
                water=water.get((z, x, y), []),
                airports=airports.get((z, x, y), []),
            )
        )
    return tiles


def write_tiles(tiles: list[tf.Tile], out_dir: Path = OUT_DIR) -> tuple[int, int]:
    """Serialize each tile to `out_dir/tiles/{z}/{x}/{y}.bin`. Returns
    (tile_count, total_bytes).

    Each tile passes through `tsh.cap_tile_size` first — dense-metro tiles
    (KLGA, EGLL, etc.) that would otherwise blow past the firmware's
    ~32 KB per-tile heap budget get re-simplified at looser tolerance
    (and, in extreme cases, have their smallest polygons dropped) so the
    on-device fetch always succeeds. Logs per-tile action counts so
    accidental over-shrinking is visible in the build output.
    """
    total = 0
    action_counts: dict[str, int] = {}
    for tile in tiles:
        capped, reason = tsh.cap_tile_size(tile, tsh.DEFAULT_TILE_CAP_BYTES)
        action_counts[reason.split(":", 1)[0]] = action_counts.get(
            reason.split(":", 1)[0], 0) + 1
        if reason == "oversized":
            print(
                f"warn: tile {tile.z}/{tile.x}/{tile.y} still oversized "
                f"after simplify+drop ({len(tf.encode(capped))} bytes)",
                file=sys.stderr,
            )
        path = out_dir / ts.tile_relative_path(capped.z, capped.x, capped.y)
        path.parent.mkdir(parents=True, exist_ok=True)
        data = tf.encode(capped)
        path.write_bytes(data)
        total += len(data)
    summary = ", ".join(f"{k}={v}" for k, v in sorted(action_counts.items()))
    print(f"tile size cap: {summary}", file=sys.stderr)
    return len(tiles), total


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument(
        "--zoom",
        type=int,
        action="append",
        choices=list(ts.ZOOM_LEVELS),
        help="Only bake these zoom levels (default: all in the pyramid)",
    )
    p.add_argument(
        "--out-dir",
        type=Path,
        default=OUT_DIR,
        help=f"Where to write the tile pyramid (default: {OUT_DIR.relative_to(ROOT)})",
    )
    args = p.parse_args()

    zoom_levels = tuple(sorted(set(args.zoom))) if args.zoom else ts.ZOOM_LEVELS
    print(f"building tile pyramid at zoom levels: {zoom_levels}", file=sys.stderr)

    runway_rows = fetch_csv_rows(RUNWAYS_URL)
    navaid_rows = fetch_csv_rows(NAVAIDS_URL)
    iap_icaos = ta.iap_idents_from_openflight_data(runway_rows, navaid_rows)
    print(
        f"IAP-capable airports (lighted runway + on-field navaid): "
        f"{len(iap_icaos)}",
        file=sys.stderr,
    )
    # GSHHG full-res: land polygons at hundreds-to-thousands of vertices
    # per island. Coastline polylines are derived from the polygon outer
    # rings — GSHHG doesn't ship coastlines separately because the
    # polygon boundary IS the coast. Tile-boundary artifacts don't
    # appear because the polyline clip in tile_coastline.py splits at
    # tile edges and only keeps the interior (non-boundary) fragments.
    gshhg_dir = gshhg.ensure_gshhg_extracted(CACHE_DIR)
    # Filter GSHHG polygons < 0.5 km² — 88% of L1 records are
    # sub-1 km² rocks/sandbars that render as sub-pixel dots at radar
    # zoom (240 px covers ~46 km at 25 nm). Filtering keeps most z=7
    # tiles under the 128 KB firmware cap without changing the pipeline.
    print("loading GSHHG L1 (continental + island coastlines, area>=0.5 km²)…",
          file=sys.stderr)
    land_features = gshhg.load_polygon_features(
        gshhg_dir / "GSHHS_shp" / "f" / "GSHHS_f_L1.shp",
        min_area_km2=0.5,
    )
    print(f"  L1: {len(land_features)} land features (post-filter)", file=sys.stderr)
    print("loading GSHHG L2 (lakes, area>=0.5 km²)…", file=sys.stderr)
    water_features = gshhg.load_polygon_features(
        gshhg_dir / "GSHHS_shp" / "f" / "GSHHS_f_L2.shp",
        min_area_km2=0.5,
    )
    print(f"  L2: {len(water_features)} lake features (post-filter)", file=sys.stderr)
    coast_features = gshhg.polygon_features_to_coastline_features(land_features)
    tiles = build_all_tiles(
        coast_features=coast_features,
        land_features=land_features,
        water_features=water_features,
        river_features=load_geojson_features(SOURCES["rivers"]),
        airport_rows=fetch_csv_rows(AIRPORTS_URL),
        runway_rows=runway_rows,
        iap_icaos=iap_icaos,
        zoom_levels=zoom_levels,
    )
    count, total_bytes = write_tiles(tiles, args.out_dir)
    print(
        f"wrote {count} tiles under {args.out_dir}, "
        f"{total_bytes / 1024:.1f} KB total",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()

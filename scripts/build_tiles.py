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

import tile_airports as ta
import tile_coastline as tc
import tile_format as tf
import tile_polygons as tp
import tile_scheme as ts

ROOT = Path(__file__).resolve().parents[1]
CACHE_DIR = ROOT / ".local-data"
OUT_DIR = ROOT / "web" / "public" / "data"

NE = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson"
SOURCES = {
    "coastline": f"{NE}/ne_10m_coastline.geojson",
    "land": f"{NE}/ne_10m_land.geojson",
    "islands": f"{NE}/ne_10m_minor_islands.geojson",
    "lakes": f"{NE}/ne_10m_lakes.geojson",
    # Inland rivers — folded into the Coast section (both are polylines
    # rendered with the same coastline-color pass on both firmware and
    # web). Without this feed the tiles have no inland water at all —
    # Willamette River through Corvallis, Sacramento River delta, etc.
    # never render.
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
    island_features: list[dict],
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
    coast = tc.build_coastline_tiles(coast_features, zoom_levels)
    # Rivers piggyback on the Coast section: same LineString geometry,
    # same "draw as thin coastline-color polyline" render path on both
    # firmware (coastline_overlay.cpp) and web (drawCoastline in
    # renderer.ts). Zero new format/renderer surface — the tradeoff is
    # rivers render in the coastline color instead of a distinct blue.
    if river_features:
        river_tiles = tc.build_coastline_tiles(river_features, zoom_levels)
        for key, polys in river_tiles.items():
            coast.setdefault(key, []).extend(polys)
    land = tp.build_polygon_tiles(land_features, zoom_levels)
    for key, polys in tp.build_polygon_tiles(island_features, zoom_levels).items():
        land.setdefault(key, []).extend(polys)
    water = tp.build_polygon_tiles(water_features, zoom_levels)
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
    (tile_count, total_bytes)."""
    total = 0
    for tile in tiles:
        path = out_dir / ts.tile_relative_path(tile.z, tile.x, tile.y)
        path.parent.mkdir(parents=True, exist_ok=True)
        data = tf.encode(tile)
        path.write_bytes(data)
        total += len(data)
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
    tiles = build_all_tiles(
        coast_features=load_geojson_features(SOURCES["coastline"]),
        land_features=load_geojson_features(SOURCES["land"]),
        island_features=load_geojson_features(SOURCES["islands"]),
        water_features=load_geojson_features(SOURCES["lakes"]),
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

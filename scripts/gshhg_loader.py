"""GSHHG shapefile loader.

GSHHG (Global Self-consistent Hierarchical High-resolution Geography)
ships as shapefiles at five resolutions (crude / low / intermediate /
high / full). This module reads the full-resolution set and emits
GeoJSON-shaped feature dicts so the existing tile pipeline
(tile_polygons.py, tile_coastline.py) can consume them without
knowing about shapefiles.

Levels used:
  L1 — continental + island coastlines (polygons)
  L2 — lakes                                (polygons)

L3 (islands in lakes) and L4 (ponds in islands) are supported by
GSHHG but negligible at radar zoom; skipped to keep the pipeline
simple.

Polygons are emitted as GeoJSON Polygon dicts with a single outer
ring (no interior holes — matches the existing tile pipeline which
already drops holes in tile_polygons.py). GSHHG uses (lon, lat)
tuple ordering in shapefile points, same as GeoJSON.
"""
from __future__ import annotations

import io
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path
from typing import Iterable, Iterator

try:
    import shapefile   # type: ignore
except ImportError as e:
    raise ImportError(
        "gshhg_loader requires the `pyshp` package. Install with "
        "`pip install pyshp`."
    ) from e


# Full-resolution shapefile bundle. ~150 MB compressed, ~1 GB unpacked.
# Cached under .local-data so subsequent builds skip the download.
GSHHG_URL = "https://www.soest.hawaii.edu/pwessel/gshhg/gshhg-shp-2.3.7.zip"


def ensure_gshhg_extracted(cache_dir: Path) -> Path:
    """Download + unzip the GSHHG bundle to cache_dir/gshhg/. Returns
    the path to the unpacked directory. Idempotent — checks for the
    presence of the L1 full-res shapefile before doing any work."""
    cache_dir.mkdir(parents=True, exist_ok=True)
    unpack = cache_dir / "gshhg"
    marker = unpack / "GSHHS_shp" / "f" / "GSHHS_f_L1.shp"
    if marker.exists():
        return unpack
    zip_path = cache_dir / "gshhg-shp.zip"
    if not zip_path.exists():
        print(f"downloading {GSHHG_URL} (~150 MB, one-time)", file=sys.stderr)
        with urllib.request.urlopen(GSHHG_URL, timeout=600) as resp, \
                open(zip_path, "wb") as out:
            shutil.copyfileobj(resp, out)
    print(f"unpacking {zip_path.name} → {unpack.relative_to(cache_dir.parent)}",
          file=sys.stderr)
    with zipfile.ZipFile(zip_path) as zf:
        zf.extractall(unpack)
    return unpack


def _shape_to_polygon_features(shape) -> Iterator[dict]:
    """A pyshp Shape (POLYGON) may hold multiple parts (rings).
    GSHHG's convention is one polygon per record — but a record can
    still carry the polygon's outer ring plus a sibling / island
    ring in separate parts. We emit each part as a standalone Polygon
    feature; the existing pipeline's polygon builder handles the
    downstream tile assignment."""
    parts = list(shape.parts) + [len(shape.points)]
    for i in range(len(parts) - 1):
        ring = [(pt[0], pt[1]) for pt in shape.points[parts[i]:parts[i + 1]]]
        if len(ring) < 4:
            continue
        yield {
            "type": "Feature",
            "geometry": {"type": "Polygon", "coordinates": [ring]},
        }


def load_polygon_features(
    shp_path: Path, min_area_km2: float = 0.0
) -> list[dict]:
    """Read a GSHHG polygon shapefile (L1, L2, …) as a list of
    GeoJSON-shaped features. Each feature has a Polygon geometry
    with a single outer ring.

    88% of GSHHG L1 features are sub-1 km² rocks / sandbars invisible
    at radar zoom (~200 m per pixel at 25 nm range). Filtering them
    out via `min_area_km2` at load time keeps tile sizes under the
    firmware's 128 KB cap without changing the pipeline. The GSHHG
    dbf carries an `area` field per record (km²) — used here to skip
    before the expensive shapefile shape decode.
    """
    if not shp_path.exists():
        raise FileNotFoundError(f"GSHHG shapefile not found: {shp_path}")
    sf = shapefile.Reader(str(shp_path))
    out: list[dict] = []
    for shape_rec in sf.iterShapeRecords():
        area = float(shape_rec.record["area"])
        if area < min_area_km2:
            continue
        out.extend(_shape_to_polygon_features(shape_rec.shape))
    return out


def polygon_features_to_coastline_features(features: Iterable[dict]) -> list[dict]:
    """Extract each polygon's outer ring as a LineString feature so the
    coastline pipeline (which does the tile-boundary clip cleanly) can
    consume land polygons and emit only the internal edges as coast."""
    out: list[dict] = []
    for feat in features:
        geom = feat.get("geometry") or {}
        if geom.get("type") != "Polygon":
            continue
        rings = geom.get("coordinates") or []
        if not rings:
            continue
        outer = rings[0]
        if len(outer) < 2:
            continue
        out.append({
            "type": "Feature",
            "geometry": {"type": "LineString", "coordinates": outer},
        })
    return out

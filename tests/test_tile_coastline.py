"""Tests for scripts/tile_coastline.py — the coastline layer of the tile
pipeline.

These lock in tile-distribution semantics: a coastline that runs across
multiple tiles should appear (in clipped form) in every tile it touches,
and a coastline that doesn't touch a tile should never appear there.
"""
import pytest

import tile_coastline as tc
import tile_scheme as ts


def _feat(coords, gtype="LineString"):
    return {"type": "Feature", "geometry": {"type": gtype, "coordinates": coords}}


def test_extract_linestrings_flattens_multilinestring():
    features = [
        _feat([(-1.0, 51.0), (0.0, 51.5)], gtype="LineString"),
        _feat(
            [[(1.0, 52.0), (2.0, 52.5)], [(3.0, 53.0), (4.0, 53.5)]],
            gtype="MultiLineString",
        ),
    ]
    lines = tc._extract_linestrings(features)
    assert len(lines) == 3
    assert lines[0] == [(-1.0, 51.0), (0.0, 51.5)]
    assert lines[1] == [(1.0, 52.0), (2.0, 52.5)]
    assert lines[2] == [(3.0, 53.0), (4.0, 53.5)]


def test_extract_linestrings_skips_non_line_geometries():
    features = [
        _feat([[[0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [0.0, 0.0]]], gtype="Polygon"),
        _feat([(0.0, 0.0)], gtype="Point"),
    ]
    assert tc._extract_linestrings(features) == []


def test_short_polyline_yields_no_tiles():
    """A single point can't form a coastline segment."""
    assert tc.distribute_polyline_to_tiles([(0.0, 0.0)], z=7, tol_deg=0.002) == {}


def test_polyline_fully_inside_a_tile_lands_in_that_tile_only():
    # Small SF-area segment: entirely inside z=7 tile (20, 37).
    line = [(-122.4528, 37.7552), (-122.4400, 37.7600), (-122.4300, 37.7700)]
    got = tc.distribute_polyline_to_tiles(line, z=7, tol_deg=0.001)
    assert set(got.keys()) == {(20, 37)}
    polys = got[(20, 37)]
    assert len(polys) == 1
    assert len(polys[0].points) >= 2


def test_polyline_crossing_two_tiles_lands_in_both():
    """At z=3, tiles are 45° wide. A line from lon=44 to lon=46 crosses
    the boundary at lon=45 (tile 4 → tile 5)."""
    # Latitude 30 sits inside z=3 y-band 2 (lat 45 → 22.5, so 30 is in y=2).
    line = [(44.0, 30.0), (44.5, 30.0), (45.5, 30.0), (46.0, 30.0)]
    got = tc.distribute_polyline_to_tiles(line, z=3, tol_deg=0.001)
    xs = {x for x, _ in got.keys()}
    # Both x=4 and x=5 must appear.
    assert 4 in xs and 5 in xs
    # Every entry should be usable (>=2 points).
    for polys in got.values():
        for p in polys:
            assert len(p.points) >= 2


def test_polyline_wholly_outside_any_data_area_returns_empty():
    """A polyline in the middle of the ocean south of Africa can't
    itself be 'outside' the world — it always lands in exactly one tile.
    But an empty polyline should never populate a tile.
    """
    assert tc.distribute_polyline_to_tiles([], z=7, tol_deg=0.002) == {}


def test_build_coastline_tiles_emits_all_zoom_levels_for_touched_areas():
    """A single SF-area line should appear at all three zoom levels."""
    features = [_feat([(-122.5, 37.75), (-122.4, 37.80), (-122.3, 37.85)])]
    tiles = tc.build_coastline_tiles(features)
    zooms_seen = {z for (z, _, _) in tiles.keys()}
    assert zooms_seen == set(ts.ZOOM_LEVELS)


def test_build_coastline_tiles_coarser_zooms_have_fewer_points():
    """A wiggly line at higher tolerance should retain fewer points than
    the same line at the baseline tolerance — otherwise the coarser
    zoom levels are wasting bytes.
    """
    # Wiggly polyline in northern California — 100 zig-zag points.
    line = [(-122.5 + i * 0.005, 37.75 + 0.01 * ((-1) ** i)) for i in range(100)]
    features = [_feat(line)]
    tiles = tc.build_coastline_tiles(features)

    def total_points(z: int) -> int:
        return sum(
            len(p.points)
            for (zz, _, _), polys in tiles.items()
            if zz == z
            for p in polys
        )

    n7 = total_points(7)
    n5 = total_points(5)
    n3 = total_points(3)
    assert n7 > 0
    assert n5 <= n7
    assert n3 <= n5


def test_build_coastline_tiles_uses_baseline_tolerance_at_finest_zoom():
    """Regression contract: the finest zoom level must not simplify
    below the coastline quality baseline (0.002°). Locking this in
    prevents a silent 'let's ship less data' change from degrading the
    map."""
    assert ts.SIMPLIFY_TOL_DEG[max(ts.ZOOM_LEVELS)] == pytest.approx(0.002)

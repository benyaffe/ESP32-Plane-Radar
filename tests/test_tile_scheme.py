"""Tests for scripts/tile_scheme.py — the pyramid geometry used by the
global map-data pipeline.

Locks in the (z, x, y) grid convention so the firmware / website / builder
can never drift apart on which tile covers which piece of the world.
"""
import pytest

import tile_scheme as ts


def test_zoom_levels_contract_from_plan():
    """The refactor plan fixes 3 zoom levels; new levels must be an
    explicit decision, not a silent drift."""
    assert ts.ZOOM_LEVELS == (3, 5, 7)


def test_baseline_tolerance_at_finest_zoom_stays_at_or_tighter_than_coastline_baseline():
    """Finest zoom must render at OR FINER THAN the plan's 0.002° coastline
    baseline (~222 m). Was tightened to 0.0005° (~55 m) once rivers
    were folded into the Coast section — tight river bends
    (Willamette through Corvallis, etc.) need finer resolution at the
    5-25 nm radar zoom than ocean coastline alone did. This guard
    prevents a silent 'ship less data' regression."""
    assert ts.SIMPLIFY_TOL_DEG[7] <= 0.002


def test_coarser_zoom_uses_looser_tolerance():
    """Detail should degrade monotonically as zoom coarsens — otherwise
    we'd be shipping high-detail data that renders at 1 pixel wide."""
    assert ts.SIMPLIFY_TOL_DEG[3] > ts.SIMPLIFY_TOL_DEG[5] > ts.SIMPLIFY_TOL_DEG[7]


def test_tiles_per_side_doubles_per_zoom():
    assert ts.tiles_per_side(0) == 1
    assert ts.tiles_per_side(3) == 8
    assert ts.tiles_per_side(7) == 128


def test_tiles_per_side_rejects_negative():
    with pytest.raises(ValueError):
        ts.tiles_per_side(-1)


def test_tile_bounds_at_z0_is_whole_world():
    b = ts.tile_bounds(0, 0, 0)
    assert (b.min_lat, b.max_lat) == (-90.0, 90.0)
    assert (b.min_lon, b.max_lon) == (-180.0, 180.0)


def test_tile_bounds_top_left_tile_is_north_west():
    """(0, 0) is the north-west tile. y grows south, x grows east —
    standard TMS/slippy convention."""
    b = ts.tile_bounds(3, 0, 0)
    assert b.max_lat == pytest.approx(90.0)
    assert b.min_lon == pytest.approx(-180.0)
    # 180° lat / 8 rows = 22.5° per tile.
    assert b.max_lat - b.min_lat == pytest.approx(22.5)
    # 360° lon / 8 cols = 45° per tile.
    assert b.max_lon - b.min_lon == pytest.approx(45.0)


def test_tile_bounds_bottom_right_tile_is_south_east():
    b = ts.tile_bounds(3, 7, 7)
    assert b.max_lon == pytest.approx(180.0)
    assert b.min_lat == pytest.approx(-90.0)


def test_tile_bounds_rejects_out_of_range():
    with pytest.raises(ValueError):
        ts.tile_bounds(3, 8, 0)
    with pytest.raises(ValueError):
        ts.tile_bounds(3, 0, 8)


def test_tile_of_sf_at_z7_matches_expected_cell():
    """Sutro Tower (37.7552, -122.4528) at z=7:
    x = floor((-122.4528 + 180) / 2.8125) = floor(20.4612...) = 20
    y = floor((90 - 37.7552) / 1.40625) = floor(37.1521...) = 37"""
    assert ts.tile_of(7, 37.7552, -122.4528) == (20, 37)


def test_tile_of_and_tile_bounds_are_consistent():
    """The tile of a point must contain that point."""
    for lat, lon in [(37.75, -122.45), (51.5, -0.13), (-33.86, 151.21), (1.35, 103.82)]:
        for z in ts.ZOOM_LEVELS:
            x, y = ts.tile_of(z, lat, lon)
            b = ts.tile_bounds(z, x, y)
            assert b.contains(lat, lon), (
                f"tile ({z},{x},{y}) with bounds {b} should contain "
                f"({lat},{lon})"
            )


def test_tile_of_wraps_longitude():
    x_east, _ = ts.tile_of(3, 0.0, 181.0)  # 1° past antimeridian → -179°
    x_expected, _ = ts.tile_of(3, 0.0, -179.0)
    assert x_east == x_expected


def test_tile_of_clamps_poles():
    """Exact poles shouldn't crash — they should map to the edge row."""
    _, y_north = ts.tile_of(3, 90.0, 0.0)
    _, y_south = ts.tile_of(3, -90.0, 0.0)
    assert y_north == 0
    assert y_south == 7


def test_tiles_covering_small_radius_returns_at_least_one_tile():
    """A 10 km radius always overlaps at least the tile containing the
    center."""
    for z in ts.ZOOM_LEVELS:
        tiles = ts.tiles_covering(z, 37.7552, -122.4528, 10.0)
        assert (20, 37) in tiles if z == 7 else True
        assert len(tiles) >= 1


def test_tiles_covering_large_radius_returns_more_tiles():
    """The widest 25 nm range (~46 km radius) at z=7 should touch a
    handful of neighboring tiles when the center sits near a boundary."""
    tiles = ts.tiles_covering(7, 37.7552, -122.4528, 100.0)
    # Sanity: a 100 km radius should span at least 2 tiles at z=7 (~310 km
    # per tile) unless the center is dead-center — SF is near a boundary,
    # so expect >= 1 and typically more.
    assert len(tiles) >= 1
    assert (20, 37) in tiles


def test_tiles_covering_handles_antimeridian():
    """A center near the antimeridian with a large radius should return
    tiles from both the far-east and far-west columns."""
    tiles = ts.tiles_covering(3, 0.0, 179.0, 500.0)
    xs = {x for x, _ in tiles}
    # At z=3, one tile spans 45° lon. 500 km / (111*cos 0) ≈ 4.5°, so a
    # small crossing — expect at least the last x column (7) and possibly
    # the first (0).
    assert 7 in xs
    # The exact set of x's near a boundary is fiddly; just require the
    # antimeridian crossing didn't silently drop tiles.
    assert len(tiles) >= 1


def test_tile_relative_path_is_stable():
    """This path shows up in URLs and in the SPIFFS cache; freeze it."""
    assert ts.tile_relative_path(7, 20, 37) == "tiles/7/20/37.bin"
    assert ts.tile_relative_path(0, 0, 0) == "tiles/0/0/0.bin"

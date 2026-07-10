"""Tests for scripts/build_coastlines.py helper functions.

These lock in the geometry primitives used by the coastline data pipeline
so future refactors (including the global tile builder that replaces this
one) can be validated against known-good behavior.
"""
import math

import build_coastlines as bc


def test_clip_polyline_all_inside_returns_single_chunk():
    bbox = (35.0, 40.0, -125.0, -120.0)
    coords = [(-122.5, 37.5), (-122.4, 37.6), (-122.3, 37.7)]
    result = bc.clip_polyline(coords, bbox)
    assert len(result) == 1
    assert result[0] == coords


def test_clip_polyline_all_outside_returns_empty():
    bbox = (35.0, 40.0, -125.0, -120.0)
    coords = [(-100.0, 50.0), (-99.0, 51.0), (-98.0, 52.0)]
    assert bc.clip_polyline(coords, bbox) == []


def test_clip_polyline_splits_on_bbox_exit():
    bbox = (35.0, 40.0, -125.0, -120.0)
    coords = [
        (-122.5, 37.5),
        (-122.4, 37.6),
        (-100.0, 50.0),   # outside — should split here
        (-99.0, 51.0),
        (-122.3, 37.7),
        (-122.2, 37.8),
    ]
    result = bc.clip_polyline(coords, bbox)
    assert len(result) == 2
    assert result[0] == [(-122.5, 37.5), (-122.4, 37.6)]
    assert result[1] == [(-122.3, 37.7), (-122.2, 37.8)]


def test_clip_polyline_drops_isolated_single_point():
    bbox = (35.0, 40.0, -125.0, -120.0)
    coords = [
        (-100.0, 50.0),
        (-122.5, 37.5),   # single inside point — needs >=2 to survive
        (-100.0, 50.0),
    ]
    assert bc.clip_polyline(coords, bbox) == []


def test_perp_dist_point_on_line_is_zero():
    a = (0.0, 0.0)
    b = (10.0, 0.0)
    p = (5.0, 0.0)
    assert bc._perp_dist(p, a, b) == 0.0


def test_perp_dist_point_off_line_is_perpendicular_distance():
    a = (0.0, 0.0)
    b = (10.0, 0.0)
    p = (5.0, 3.0)
    assert bc._perp_dist(p, a, b) == 3.0


def test_perp_dist_degenerate_line_falls_back_to_point_distance():
    a = (2.0, 2.0)
    b = (2.0, 2.0)   # a == b
    p = (5.0, 6.0)
    # sqrt((5-2)^2 + (6-2)^2) = 5
    assert bc._perp_dist(p, a, b) == 5.0


def test_dp_simplify_short_polyline_returned_unchanged():
    pts = [(0.0, 0.0), (1.0, 1.0)]
    assert bc.dp_simplify(pts, 0.1) == pts


def test_dp_simplify_collinear_points_all_drop_middle():
    pts = [(0.0, 0.0), (1.0, 0.0), (2.0, 0.0), (3.0, 0.0)]
    result = bc.dp_simplify(pts, 0.001)
    # Middle two are on the line — DP drops them, keeps only endpoints.
    assert result == [(0.0, 0.0), (3.0, 0.0)]


def test_dp_simplify_keeps_peak_above_tolerance():
    pts = [(0.0, 0.0), (5.0, 10.0), (10.0, 0.0)]
    # Peak is 10 units from the baseline; any tol < 10 must keep it.
    assert bc.dp_simplify(pts, 1.0) == pts


def test_dp_simplify_drops_peak_below_tolerance():
    pts = [(0.0, 0.0), (5.0, 0.05), (10.0, 0.0)]
    # Peak is 0.05 from baseline; tol 0.1 drops the middle.
    assert bc.dp_simplify(pts, 0.1) == [(0.0, 0.0), (10.0, 0.0)]


def test_default_simplify_tolerance_is_the_quality_baseline():
    """Rendering-quality contract, do not change without a plan.

    The plan file /Users/ben.yaffe/.claude/plans/between-all-three-
    surfaces-replicated-hickey.md fixes 0.002° (~222 m) as the minimum
    detail level for coastlines. Any change to this default silently
    degrades every rendered coastline. If you're intentionally tightening
    the tolerance, update the plan first.
    """
    assert bc.DEFAULT_SIMPLIFY_TOL_DEG == 0.002


def test_dp_simplify_at_baseline_tolerance_preserves_realistic_detail():
    """Synthetic wiggly coastline — number of points retained at the baseline
    tolerance should stay above a minimum density (points per degree of
    length). This is the regression contract: if a future 'simplification'
    refactor accidentally over-simplifies, this test fails."""
    tol = bc.DEFAULT_SIMPLIFY_TOL_DEG
    n = 200
    # Wiggly polyline: 200 points spanning 1° of longitude with sinusoidal
    # perpendicular displacement ~0.01° amplitude (10× the tolerance).
    pts = []
    for i in range(n):
        x = i / (n - 1)  # 0..1 degrees
        y = 0.01 * math.sin(x * math.pi * 6)  # 3 full wiggles
        pts.append((x, y))
    simplified = bc.dp_simplify(pts, tol)
    # At tol=0.002 with a 0.01 amplitude, we must retain enough vertices
    # to describe the wiggles. Empirically ~15+ for this shape.
    assert len(simplified) >= 15, (
        f"Coastline detail regressed: only {len(simplified)} vertices "
        f"retained for a wiggly 1° polyline at tol={tol}° — expected >= 15"
    )

"""Tests for scripts/tile_airports.py — the airport layer of the tile
pipeline.

The filter rules are the load-bearing part: get them wrong and the map
either goes empty (over-strict filter) or bloats to include every
private grass strip (over-permissive filter). These tests pin down
the boundaries.
"""
import tile_airports as ta
import tile_scheme as ts


def _apt(ident="KSFO", atype="large_airport", lat="37.6188", lon="-122.375",
         scheduled="yes"):
    return {
        "ident": ident,
        "type": atype,
        "latitude_deg": lat,
        "longitude_deg": lon,
        "scheduled_service": scheduled,
    }


def _rwy(airport="KSFO", le_lat="37.61", le_lon="-122.38",
         he_lat="37.62", he_lon="-122.37", le="28L", he="10R",
         length="10000"):
    return {
        "airport_ident": airport,
        "le_latitude_deg": le_lat,
        "le_longitude_deg": le_lon,
        "he_latitude_deg": he_lat,
        "he_longitude_deg": he_lon,
        "le_ident": le,
        "he_ident": he,
        "length_ft": length,
    }


# ---------------------------------------------------------------------------
# build_airports filter matrix
# ---------------------------------------------------------------------------


def test_large_airport_always_kept():
    apts = ta.build_airports([_apt(atype="large_airport")], [])
    assert [a.ident for a in apts] == ["KSFO"]


def test_medium_airport_always_kept():
    apts = ta.build_airports(
        [_apt(ident="KOAK", atype="medium_airport", scheduled="no")], []
    )
    assert [a.ident for a in apts] == ["KOAK"]


def test_small_airport_with_scheduled_service_kept():
    apts = ta.build_airports(
        [_apt(ident="KAAA", atype="small_airport", scheduled="yes")], []
    )
    assert [a.ident for a in apts] == ["KAAA"]


def test_small_airport_without_scheduled_service_dropped():
    apts = ta.build_airports(
        [_apt(ident="KAAA", atype="small_airport", scheduled="no")], []
    )
    assert apts == []


def test_iap_flag_force_includes_otherwise_filtered_airport():
    """A tiny GA strip with no scheduled service but a published
    instrument approach must appear on the map — instrument-rated
    pilots plan to it."""
    apts = ta.build_airports(
        [_apt(ident="KHAF", atype="small_airport", scheduled="no")],
        [],
        iap_icaos=["KHAF"],
    )
    assert len(apts) == 1
    assert apts[0].instrument_approach is True


def test_iap_set_case_insensitive():
    apts = ta.build_airports(
        [_apt(ident="KHAF", atype="small_airport", scheduled="no")],
        [],
        iap_icaos=["khaf"],
    )
    assert len(apts) == 1


def test_non_iap_airport_has_iap_flag_false():
    apts = ta.build_airports(
        [_apt(atype="large_airport")], [], iap_icaos=[]
    )
    assert apts[0].instrument_approach is False


def test_non_icao_ident_is_dropped():
    """OurAirports uses local codes (e.g. '4Q7', 'US-1234') for airports
    without an ICAO grid entry. The device only knows how to label
    4-letter codes so we skip those upstream."""
    apts = ta.build_airports(
        [_apt(ident="4Q7", atype="small_airport", scheduled="yes")], []
    )
    assert apts == []


def test_short_ident_is_dropped():
    apts = ta.build_airports(
        [_apt(ident="KSF", atype="large_airport")], []
    )
    assert apts == []


def test_five_char_ident_is_dropped():
    apts = ta.build_airports(
        [_apt(ident="KSFO1", atype="large_airport")], []
    )
    assert apts == []


def test_missing_coords_row_is_dropped():
    apts = ta.build_airports(
        [_apt(lat="", lon="")], []
    )
    assert apts == []


# ---------------------------------------------------------------------------
# Runway attachment
# ---------------------------------------------------------------------------


def test_runway_attached_to_matching_airport():
    apts = ta.build_airports(
        [_apt(atype="large_airport", ident="KSFO")],
        [_rwy(airport="KSFO")],
    )
    assert len(apts) == 1
    assert len(apts[0].runways) == 1
    r = apts[0].runways[0]
    assert (r.lat1, r.lon1) == (37.61, -122.38)


def test_heliport_pad_row_ignored():
    """A runway row with H-designator idents + short length is a helipad,
    not a runway."""
    apts = ta.build_airports(
        [_apt(atype="large_airport", ident="KSFO")],
        [
            _rwy(airport="KSFO", le="H1", he="H2", length="50"),
            _rwy(airport="KSFO"),
        ],
    )
    assert len(apts[0].runways) == 1


def test_missing_runway_coord_dropped():
    apts = ta.build_airports(
        [_apt(atype="large_airport", ident="KSFO")],
        [_rwy(airport="KSFO", he_lat="")],
    )
    assert apts[0].runways == []


# ---------------------------------------------------------------------------
# Deterministic ordering
# ---------------------------------------------------------------------------


def test_output_ordering_is_deterministic():
    """Sorted by (-tier, ident) so the same input always produces the
    same tile bytes — otherwise every re-run of the pipeline would
    change the deploy hash even with no data changes."""
    rows = [
        _apt(ident="KAAA", atype="small_airport"),   # tier 1 + scheduled
        _apt(ident="KOAK", atype="medium_airport"),  # tier 2
        _apt(ident="KSFO", atype="large_airport"),   # tier 3
        _apt(ident="KBBB", atype="medium_airport"),  # tier 2
    ]
    apts = ta.build_airports(rows, [])
    assert [a.ident for a in apts] == ["KSFO", "KBBB", "KOAK", "KAAA"]


# ---------------------------------------------------------------------------
# Tile bucketing
# ---------------------------------------------------------------------------


def test_airport_lands_in_one_tile_per_zoom():
    """One airport = one tile at each zoom level."""
    tiles = ta.build_airport_tiles(
        [_apt(atype="large_airport", lat="37.7552", lon="-122.4528")], []
    )
    z_seen = {z for (z, _, _) in tiles.keys()}
    assert z_seen == set(ts.ZOOM_LEVELS)
    for (z, x, y), apts in tiles.items():
        assert len(apts) == 1
        # Cross-check the (x,y) matches tile_of.
        assert (x, y) == ts.tile_of(z, 37.7552, -122.4528)


def test_two_airports_in_same_tile_both_appear():
    tiles = ta.build_airport_tiles(
        [
            _apt(ident="KSFO", lat="37.6188", lon="-122.375"),
            _apt(ident="KOAK", atype="medium_airport", lat="37.7213", lon="-122.2208"),
        ],
        [],
    )
    # Both are within the same z=3 tile (western US, tile (2, 3)).
    # Confirm at least one tile at z=3 contains both.
    counts = {
        (x, y): len(apts)
        for (z, x, y), apts in tiles.items()
        if z == 3
    }
    assert max(counts.values()) == 2


def test_two_airports_in_different_tiles_split():
    """SF and NYC land in different tiles at every zoom level."""
    tiles = ta.build_airport_tiles(
        [
            _apt(ident="KSFO", lat="37.6188", lon="-122.375"),
            _apt(ident="KJFK", lat="40.6413", lon="-73.7781"),
        ],
        [],
    )
    for z in ts.ZOOM_LEVELS:
        tiles_at_z = [(x, y) for (zz, x, y), _ in tiles.items() if zz == z]
        assert len(set(tiles_at_z)) == 2

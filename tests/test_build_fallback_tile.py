"""Unit tests for scripts/build_fallback_tile.build_fallback_tile.

Mocks the network fetchers so the test runs offline. Focuses on the
filter / assembly logic — the encoded byte output is exercised
end-to-end elsewhere (test_fallback_tile.py).
"""
import build_fallback_tile as bft
import tile_format as tf


# Minimal GeoJSON land feature — a single triangle covering a chunk of
# the world so tp.distribute_polygon_to_tiles has something to emit.
LAND_FEATURES = [
    {
        "type": "Feature",
        "geometry": {
            "type": "Polygon",
            "coordinates": [[
                [-10.0, -10.0],
                [10.0, -10.0],
                [10.0, 10.0],
                [-10.0, -10.0],
            ]],
        },
    },
]


def _airport_row(ident, atype, lat=0.0, lon=0.0, scheduled="yes"):
    return {
        "ident": ident, "type": atype, "scheduled_service": scheduled,
        "latitude_deg": str(lat), "longitude_deg": str(lon),
        "iata_code": "", "municipality": "", "name": ident,
    }


AIRPORT_ROWS = [
    _airport_row("KSFO", "large_airport", 37.6, -122.4),
    _airport_row("KHAF", "medium_airport", 37.5, -122.5),
    _airport_row("KMRY", "small_airport", 36.6, -121.8),
    _airport_row("1CA9", "heliport", 37.7, -122.4),
]


def _install_mocks(monkeypatch):
    monkeypatch.setattr(bft, "load_geojson_features",
                        lambda _url: LAND_FEATURES)
    monkeypatch.setattr(bft, "fetch_csv_rows",
                        lambda _url: AIRPORT_ROWS)


def test_build_fallback_tile_returns_z0_world_tile(monkeypatch):
    _install_mocks(monkeypatch)
    tile = bft.build_fallback_tile()
    assert (tile.z, tile.x, tile.y) == (0, 0, 0)


def test_build_fallback_tile_omits_coastlines_and_water(monkeypatch):
    """The fallback intentionally leaves coasts + water out of the flash
    payload — see the comment in build_fallback_tile()."""
    _install_mocks(monkeypatch)
    tile = bft.build_fallback_tile()
    assert tile.coast == []
    assert tile.water == []


def test_build_fallback_tile_includes_land_polygons(monkeypatch):
    _install_mocks(monkeypatch)
    tile = bft.build_fallback_tile()
    assert len(tile.land) >= 1
    for poly in tile.land:
        assert isinstance(poly, tf.Polyline)


def test_build_fallback_tile_drops_small_and_heliport_airports(monkeypatch):
    """Only tier >= 3 (large) airports survive the fallback filter."""
    _install_mocks(monkeypatch)
    tile = bft.build_fallback_tile()
    idents = {a.ident for a in tile.airports}
    assert "KSFO" in idents          # large
    assert "KHAF" not in idents      # medium — dropped in fallback (tier<3)
    assert "KMRY" not in idents      # small
    assert "1CA9" not in idents      # heliport


def test_build_fallback_tile_strips_runway_detail(monkeypatch):
    """Runways are trimmed to save flash — 16 bytes per airport."""
    _install_mocks(monkeypatch)
    tile = bft.build_fallback_tile()
    for a in tile.airports:
        assert a.runways == []


def test_build_fallback_tile_respects_airport_cap(monkeypatch):
    """FALLBACK_MAX_AIRPORTS bounds the airport count regardless of input."""
    many = [_airport_row(f"K{n:03d}", "large_airport", 37.0 + n * 0.001, -122.0)
            for n in range(bft.FALLBACK_MAX_AIRPORTS + 50)]
    monkeypatch.setattr(bft, "load_geojson_features",
                        lambda _url: LAND_FEATURES)
    monkeypatch.setattr(bft, "fetch_csv_rows", lambda _url: many)
    tile = bft.build_fallback_tile()
    assert len(tile.airports) == bft.FALLBACK_MAX_AIRPORTS


def test_encoded_tile_roundtrips_through_the_decoder(monkeypatch):
    _install_mocks(monkeypatch)
    tile = bft.build_fallback_tile()
    data = tf.encode(tile)
    decoded = tf.decode(data)
    assert (decoded.z, decoded.x, decoded.y) == (0, 0, 0)
    assert len(decoded.airports) == len(tile.airports)

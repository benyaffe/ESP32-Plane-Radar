"""Unit tests for scripts/build_web_data.build_airport_index.

The script fetches OurAirports live; here we swap the network call for a
canned CSV so we can lock the filter + sort rules without waiting on the
upstream (or being blocked in CI environments without egress).
"""
import build_web_data as bwd


# One row per interesting filter case. Order is intentionally scrambled
# so the sort assertions have something to prove.
SAMPLE_ROWS = [
    # small with scheduled service — KEEP
    {"ident": "KMRY", "iata_code": "MRY", "type": "small_airport",
     "scheduled_service": "yes", "municipality": "Monterey", "name": "Monterey Peninsula",
     "latitude_deg": "36.5870", "longitude_deg": "-121.8430"},
    # large hub — KEEP, should sort first
    {"ident": "KSFO", "iata_code": "SFO", "type": "large_airport",
     "scheduled_service": "yes", "municipality": "San Francisco", "name": "SFO Intl",
     "latitude_deg": "37.6188", "longitude_deg": "-122.3750"},
    # medium — KEEP
    {"ident": "KHAF", "iata_code": "", "type": "medium_airport",
     "scheduled_service": "no", "municipality": "Half Moon Bay", "name": "Half Moon Bay",
     "latitude_deg": "37.5137", "longitude_deg": "-122.5011"},
    # small WITHOUT scheduled service — DROP
    {"ident": "KPAO", "iata_code": "PAO", "type": "small_airport",
     "scheduled_service": "no", "municipality": "Palo Alto", "name": "Palo Alto",
     "latitude_deg": "37.4611", "longitude_deg": "-122.1150"},
    # heliport / not in tier map — DROP
    {"ident": "1CA9", "iata_code": "", "type": "heliport",
     "scheduled_service": "no", "municipality": "SF", "name": "Some Helipad",
     "latitude_deg": "37.7", "longitude_deg": "-122.4"},
    # non-4-letter ident (numeric private strip) — DROP
    {"ident": "0Q9", "iata_code": "", "type": "small_airport",
     "scheduled_service": "yes", "municipality": "X", "name": "Private",
     "latitude_deg": "37.0", "longitude_deg": "-122.0"},
    # unparseable lat/lon — DROP
    {"ident": "KBAD", "iata_code": "", "type": "medium_airport",
     "scheduled_service": "no", "municipality": "?", "name": "Bad",
     "latitude_deg": "not-a-number", "longitude_deg": "-122.0"},
]


def test_index_drops_helipads_and_untierred_types(monkeypatch):
    monkeypatch.setattr(bwd, "fetch_csv", lambda _url: SAMPLE_ROWS)
    idx = bwd.build_airport_index()
    idents = [row[0] for row in idx]
    assert "1CA9" not in idents  # heliport
    assert "KPAO" not in idents  # small without service
    assert "0Q9" not in idents   # non-4-letter ident
    assert "KBAD" not in idents  # unparseable coords


def test_index_keeps_large_medium_and_small_with_service(monkeypatch):
    monkeypatch.setattr(bwd, "fetch_csv", lambda _url: SAMPLE_ROWS)
    idx = bwd.build_airport_index()
    idents = [row[0] for row in idx]
    assert set(idents) == {"KSFO", "KHAF", "KMRY"}


def test_index_sorts_large_hubs_first(monkeypatch):
    monkeypatch.setattr(bwd, "fetch_csv", lambda _url: SAMPLE_ROWS)
    idx = bwd.build_airport_index()
    # Tier order: KSFO (3) before KHAF (2) before KMRY (1-with-service).
    idents = [row[0] for row in idx]
    assert idents[0] == "KSFO"
    assert idents.index("KHAF") < idents.index("KMRY")


def test_index_row_layout(monkeypatch):
    # SAMPLE_ROWS is reused for airports+runways+navaids by the fetch_csv
    # mock; runway rows don't have `lighted`/`airport_ident`, and navaid
    # rows don't have `associated_airport`, so iap_idents_from_openflight_data
    # returns an empty set → every row's IAP flag is 0.
    monkeypatch.setattr(bwd, "fetch_csv", lambda _url: SAMPLE_ROWS)
    idx = bwd.build_airport_index()
    sfo = next(row for row in idx if row[0] == "KSFO")
    # [ident, iata, city, name, lat, lon, iap]
    assert sfo == ["KSFO", "SFO", "San Francisco", "SFO Intl", 37.6188, -122.375, 0]


def test_index_iap_flag_set_by_lighted_runway(monkeypatch):
    # Different fixtures per URL: airports = SAMPLE_ROWS, runways = one
    # lighted non-closed runway for KSFO. iap_idents_from_openflight_data
    # should tag KSFO; KHAF (no runway row) stays 0.
    runway_rows = [
        {"airport_ident": "KSFO", "lighted": "1", "closed": "0"},
    ]

    def fake_fetch(url):
        if "airports.csv" in url:
            return SAMPLE_ROWS
        if "runways.csv" in url:
            return runway_rows
        return []  # navaids

    monkeypatch.setattr(bwd, "fetch_csv", fake_fetch)
    idx = bwd.build_airport_index()
    by_ident = {row[0]: row for row in idx}
    assert by_ident["KSFO"][6] == 1
    assert by_ident["KHAF"][6] == 0


def test_lat_lon_rounded_to_5dp(monkeypatch):
    row = {**SAMPLE_ROWS[0], "latitude_deg": "37.7552345678", "longitude_deg": "-122.4528901234"}
    monkeypatch.setattr(bwd, "fetch_csv", lambda _url: [row])
    idx = bwd.build_airport_index()
    assert idx[0][4] == 37.75523
    assert idx[0][5] == -122.45289


def test_empty_input_returns_empty_list(monkeypatch):
    monkeypatch.setattr(bwd, "fetch_csv", lambda _url: [])
    assert bwd.build_airport_index() == []

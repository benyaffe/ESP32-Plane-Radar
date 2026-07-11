"""Per-tile airport pipeline.

Inputs: OurAirports CSVs (airports, runways). Emits
{(z, x, y): [Airport, ...]} for the tile pyramid.

Filtering:
  * Keep every airport worldwide with a 4-letter ICAO code that is
    either a large airport, a medium airport, or a small airport with
    scheduled service.
  * Force-include any airport with a lighted runway (proxy for
    "capable of IFR / night operations" — the closest thing to
    "has an instrument approach" that OurAirports actually exposes;
    the navaids.csv table only contains VOR/NDB/DME, not ILS).
  * Drop obvious heliports.

The IAP source is passed in as a pre-computed set of idents rather
than derived inline, so the pipeline module stays pure and testable;
the actual runways.csv fetch lives in the build_tiles.py entry point.
"""
from __future__ import annotations

from typing import Iterable

import tile_format as tf
import tile_scheme as ts

AIRPORT_TIER = {
    "large_airport": 3,
    "medium_airport": 2,
    "small_airport": 1,
}


def iap_idents_from_runways(runways: Iterable[dict]) -> set[str]:
    """From OurAirports' runways.csv rows, return the set of airport
    idents with at least one lighted, non-closed runway. Uppercase
    normalized to match airport idents.

    "Lighted runway" is a proxy for "field supports IFR / night ops."
    OurAirports doesn't publish ILS/LOC data (only VOR/NDB/DME in
    navaids.csv), and every meaningful ILS-approach airport lights
    the approach runway — so a lighted-runway signal catches every
    airport we'd have hand-listed as IAP-capable, plus many more,
    without depending on FAA-specific US-only feeds. False positives
    are limited to a few lighted rural strips with no published
    approach; those just get drawn on the map at high zoom, no harm."""
    out: set[str] = set()
    for r in runways:
        if (r.get("closed") or "").strip() == "1":
            continue
        if (r.get("lighted") or "").strip() != "1":
            continue
        ident = (r.get("airport_ident") or "").strip().upper()
        if ident:
            out.add(ident)
    return out


def _is_h_designator(s: str) -> bool:
    if not s or s[0] != "H":
        return False
    rest = s[1:]
    return not rest or rest[0] in "-_" or rest.isdigit()


def _is_helipad(row: dict) -> bool:
    """Same rule as scripts/build_large_airports.py — H-prefix idents +
    <2500 ft length. See tests/test_airport_builder.py for the fixture
    matrix that pins this down."""
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


def _parse_float(s: str | None) -> float | None:
    if s is None:
        return None
    s = s.strip()
    if not s:
        return None
    try:
        return float(s)
    except ValueError:
        return None


def _valid_icao(ident: str) -> bool:
    """OurAirports uses 4-letter ICAO identifiers for airports on the
    global ICAO grid. Airports without an ICAO grid entry get a longer
    'local code' — we skip those since the device only knows how to
    label 4-letter codes."""
    return len(ident) == 4 and ident.isalnum()


def _runways_by_airport(runways: Iterable[dict]) -> dict[str, list[tf.Runway]]:
    out: dict[str, list[tf.Runway]] = {}
    for r in runways:
        if _is_helipad(r):
            continue
        ident = (r.get("airport_ident") or "").strip()
        if not ident:
            continue
        lat1 = _parse_float(r.get("le_latitude_deg"))
        lon1 = _parse_float(r.get("le_longitude_deg"))
        lat2 = _parse_float(r.get("he_latitude_deg"))
        lon2 = _parse_float(r.get("he_longitude_deg"))
        if None in (lat1, lon1, lat2, lon2):
            continue
        out.setdefault(ident, []).append(
            tf.Runway(lat1=lat1, lon1=lon1, lat2=lat2, lon2=lon2)
        )
    return out


def build_airports(
    airports: Iterable[dict],
    runways: Iterable[dict],
    iap_icaos: Iterable[str] = (),
) -> list[tf.Airport]:
    """Filter and hydrate the OurAirports rows into tf.Airport records.
    Returns airports in stable order sorted by (-tier, ident) so a
    given input always produces the same output — important for
    reproducible tile builds.
    """
    iap_set = {code.strip().upper() for code in iap_icaos if code}
    rw_by_apt = _runways_by_airport(runways)

    result: list[tf.Airport] = []
    for a in airports:
        ident = (a.get("ident") or "").strip().upper()
        if not _valid_icao(ident):
            continue
        atype = a.get("type", "")
        tier = AIRPORT_TIER.get(atype, 0)
        has_iap = ident in iap_set
        scheduled = (a.get("scheduled_service") or "").strip().lower() == "yes"
        keep = has_iap or tier >= 2 or (tier == 1 and scheduled)
        if not keep:
            continue
        lat = _parse_float(a.get("latitude_deg"))
        lon = _parse_float(a.get("longitude_deg"))
        if lat is None or lon is None:
            continue
        result.append(
            tf.Airport(
                ident=ident,
                lat=lat,
                lon=lon,
                tier=tier,
                instrument_approach=has_iap,
                runways=rw_by_apt.get(ident, []),
            )
        )

    result.sort(key=lambda a: (-a.tier, a.ident))
    return result


def build_airport_tiles(
    airports: Iterable[dict],
    runways: Iterable[dict],
    iap_icaos: Iterable[str] = (),
    zoom_levels: Iterable[int] = ts.ZOOM_LEVELS,
) -> dict[tuple[int, int, int], list[tf.Airport]]:
    """One airport lands in exactly one tile at each zoom (the one
    containing its lat/lon)."""
    apts = build_airports(airports, runways, iap_icaos)
    result: dict[tuple[int, int, int], list[tf.Airport]] = {}
    for a in apts:
        for z in zoom_levels:
            x, y = ts.tile_of(z, a.lat, a.lon)
            result.setdefault((z, x, y), []).append(a)
    return result

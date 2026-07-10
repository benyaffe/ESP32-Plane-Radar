#!/usr/bin/env python3
"""Bake the airport typeahead index the web preview loads at boot.

Everything the website used to fetch here — coastlines, land, rivers,
lakes, water polygons, per-airport runways — now comes from the same
tile pyramid the firmware fetches (see scripts/build_tiles.py). This
script only produces the one file that isn't naturally per-tile:

  web/public/data/airport_index.json  compact typeahead payload for all
                                       recognizable US airports:
                                       [[icao, iata, city, name, lat, lon],
                                        ...]

Source is OurAirports (same CSV the tile bake uses for per-tile
airports); this script just widens the filter to "any recognizable
airport globally" so the Settings picker can find one anywhere.
"""
from __future__ import annotations

import csv
import io
import json
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "web" / "public" / "data"

AIRPORTS_URL = (
    "https://raw.githubusercontent.com/davidmegginson/ourairports-data/main/"
    "airports.csv"
)

# OurAirports type → visual tier. Higher = more prominent on the map.
AIRPORT_TIER = {
    "large_airport": 3,
    "medium_airport": 2,
    "small_airport": 1,
}


def fetch_csv(url: str) -> list[dict[str, str]]:
    print(f"fetching {url}", file=sys.stderr)
    with urllib.request.urlopen(url, timeout=60) as resp:
        text = resp.read().decode("utf-8")
    return list(csv.DictReader(io.StringIO(text)))


def build_airport_index() -> list[list[object]]:
    airports = fetch_csv(AIRPORTS_URL)
    index: list[list[object]] = []
    for a in airports:
        atype = a.get("type", "")
        tier = AIRPORT_TIER.get(atype, 0)
        # Keep large + medium + small-with-scheduled-service. This keeps
        # the payload compact (~65 KB gzipped) while still covering every
        # airport a spectator might actually recognize.
        keep = tier >= 2 or (tier == 1 and a.get("scheduled_service") == "yes")
        if not keep:
            continue
        ident = (a.get("ident") or "").strip()
        # 4-letter ICAO codes only — skip pseudo-idents like private
        # strips with numeric identifiers that would clutter the picker.
        if len(ident) != 4:
            continue
        try:
            lat = float(a["latitude_deg"])
            lon = float(a["longitude_deg"])
        except (KeyError, ValueError, TypeError):
            continue
        iata = (a.get("iata_code") or "").strip()
        index.append([
            ident,
            iata,
            a.get("municipality", ""),
            a.get("name", ""),
            round(lat, 5),
            round(lon, 5),
        ])
    # Sort by tier desc then ICAO so large hubs rank first in the picker.
    tier_lookup = {
        (a.get("ident") or "").strip(): AIRPORT_TIER.get(a.get("type", ""), 0)
        for a in airports
    }
    index.sort(key=lambda row: (-tier_lookup.get(str(row[0]), 0), str(row[0])))
    return index


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    index = build_airport_index()
    out_path = OUT_DIR / "airport_index.json"
    out_path.write_text(json.dumps(index, separators=(",", ":")))
    size = out_path.stat().st_size
    print(
        f"wrote {out_path.relative_to(ROOT)} ({len(index)} airports, "
        f"{size/1024:.1f} KB)",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()

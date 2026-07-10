"""Sanity checks on the flash-embedded fallback tile.

Two concerns tested:
  1. The generated C++ source is a valid tile (parses cleanly through
     the Python decoder), so the firmware side has something legit
     to point TileReader at.
  2. The tile is small enough that including it in flash doesn't
     steal a meaningful chunk of the 1.5 MB firmware slot.
"""
import re
from pathlib import Path

import tile_format as tf

ROOT = Path(__file__).resolve().parents[1]
FALLBACK_HEADER = ROOT / "include" / "data" / "fallback_tile.h"
FALLBACK_SOURCE = ROOT / "src" / "data" / "fallback_tile.cpp"

# Hard cap for the fallback bytes in flash. The 1.5 MB app slot has
# plenty of room, but if this ever balloons past 32 KB it's a sign
# the tolerance/airport-count knobs got loose.
MAX_FALLBACK_BYTES = 32_768


def _extract_fallback_bytes() -> bytes:
    """Parse the constexpr uint8_t kFallbackTile[] array out of the .cpp."""
    src = FALLBACK_SOURCE.read_text()
    m = re.search(
        r"const uint8_t kFallbackTile\[\] = \{([^}]*)\};",
        src,
        flags=re.DOTALL,
    )
    assert m, "kFallbackTile array not found in fallback_tile.cpp"
    hexes = re.findall(r"0x([0-9a-fA-F]{2})", m.group(1))
    return bytes(int(h, 16) for h in hexes)


def test_fallback_source_files_exist():
    """Regen script (`scripts/build_fallback_tile.py`) must have been
    run at least once — otherwise the firmware include of
    data/fallback_tile.h will fail to compile."""
    assert FALLBACK_HEADER.exists(), (
        "run scripts/build_fallback_tile.py to generate the fallback tile"
    )
    assert FALLBACK_SOURCE.exists(), (
        "run scripts/build_fallback_tile.py to generate the fallback tile"
    )


def test_fallback_tile_parses_cleanly():
    data = _extract_fallback_bytes()
    tile = tf.decode(data)
    assert (tile.z, tile.x, tile.y) == (0, 0, 0), (
        f"fallback should be the (0,0,0) world tile, got {tile.z, tile.x, tile.y}"
    )


def test_fallback_tile_has_land_and_airports():
    data = _extract_fallback_bytes()
    tile = tf.decode(data)
    assert len(tile.land) > 20, (
        f"fallback should include a full-world land layer, got {len(tile.land)}"
    )
    assert len(tile.airports) >= 100, (
        f"fallback should include the top world hubs, got {len(tile.airports)}"
    )


def test_fallback_tile_size_within_flash_budget():
    data = _extract_fallback_bytes()
    assert len(data) < MAX_FALLBACK_BYTES, (
        f"fallback tile {len(data)} B exceeds {MAX_FALLBACK_BYTES} B flash cap"
    )


def test_fallback_tile_header_size_matches_source():
    data = _extract_fallback_bytes()
    header = FALLBACK_HEADER.read_text()
    m = re.search(r"kFallbackTileSize = (\d+);", header)
    assert m, "kFallbackTileSize constant not found in fallback_tile.h"
    assert int(m.group(1)) == len(data), (
        "kFallbackTileSize in fallback_tile.h is stale — re-run "
        "scripts/build_fallback_tile.py"
    )

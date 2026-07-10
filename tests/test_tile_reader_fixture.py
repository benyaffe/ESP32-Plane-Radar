"""Guardrail: the hand-embedded fixture bytes in
test/test_tile_reader/test_main.cpp must match what tile_format.encode()
would emit today.

If this test fails, the wire format has drifted from the C++ parser's
expectations. Options:
  * If the C++ change is intentional: paste the fresh bytes (printed
    below on failure) into the kFixture array in the .cpp.
  * If the Python change is unintentional: revert tile_format.py.
"""
import re
from pathlib import Path

import tile_format as tf

ROOT = Path(__file__).resolve().parents[1]
FIXTURE_CPP = ROOT / "test" / "test_tile_reader" / "test_main.cpp"


def _make_reference_tile() -> tf.Tile:
    """Same recipe as documented in the .cpp comment above kFixture.
    Any change to this function forces the C++ fixture to be regenerated.
    """
    return tf.Tile(
        z=7,
        x=20,
        y=37,
        coast=[tf.Polyline([(-122.5, 37.75), (-122.4, 37.80), (-122.3, 37.85)])],
        airports=[
            tf.Airport(
                ident="KSFO",
                lat=37.6188,
                lon=-122.375,
                tier=3,
                instrument_approach=True,
                runways=[tf.Runway(37.61, -122.38, 37.62, -122.37)],
            ),
            tf.Airport(
                ident="KOAK",
                lat=37.7201,
                lon=-122.2212,
                tier=3,
                instrument_approach=True,
                runways=[],
            ),
        ],
    )


def _extract_fixture_bytes() -> bytes:
    """Parse the constexpr uint8_t kFixture[] = { 0xAB, ... } array out
    of the .cpp source. Returns the raw byte sequence."""
    src = FIXTURE_CPP.read_text()
    m = re.search(
        r"constexpr uint8_t kFixture\[\] = \{([^}]*)\};",
        src,
        flags=re.DOTALL,
    )
    assert m, "kFixture array not found in test_main.cpp"
    hexes = re.findall(r"0x([0-9a-fA-F]{2})", m.group(1))
    return bytes(int(h, 16) for h in hexes)


def test_kfixture_matches_python_encoder_output():
    expected = tf.encode(_make_reference_tile())
    actual = _extract_fixture_bytes()
    if actual != expected:
        pretty = ", ".join(f"0x{b:02x}" for b in expected)
        raise AssertionError(
            "kFixture in test/test_tile_reader/test_main.cpp is stale.\n"
            "Regenerate by pasting these bytes into the array:\n\n"
            f"{pretty}\n"
        )


def test_reference_tile_round_trips_through_python_decoder():
    """Sanity: the reference tile fixture must at minimum round-trip
    through the Python encoder+decoder, otherwise the C++ test is
    checking against garbage."""
    tile = _make_reference_tile()
    back = tf.decode(tf.encode(tile))
    assert back.z == 7 and back.x == 20 and back.y == 37
    assert len(back.coast) == 1
    assert len(back.airports) == 2
    assert back.airports[0].ident == "KSFO"
    assert back.airports[0].instrument_approach
    assert back.airports[1].ident == "KOAK"

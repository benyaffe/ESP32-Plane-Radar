"""Tests for scripts/build_large_airports.py helper functions.

These pin down the pure functions that decide what counts as an airport
vs a helipad, and the coordinate encoding used throughout the pipeline.
"""
import build_large_airports as ba


def test_coord_e7_none_and_empty_return_none():
    assert ba.coord_e7(None) is None
    assert ba.coord_e7("") is None
    assert ba.coord_e7("   ") is None


def test_coord_e7_positive_rounds_to_int32_microdegrees():
    assert ba.coord_e7("37.7552") == 377552000
    assert ba.coord_e7("0.0000001") == 1


def test_coord_e7_negative_preserves_sign():
    assert ba.coord_e7("-122.4528") == -1224528000


def test_coord_e7_rounds_at_boundary():
    # 0.00000005 → 0.5 e7 → round-to-even = 0
    assert ba.coord_e7("0.00000005") == 0
    # 0.00000015 → 1.5 e7 → round-to-even = 2
    assert ba.coord_e7("0.00000015") == 2


def test_is_h_designator_bare_h_and_h_with_digit():
    assert ba.is_h_designator("H") is True
    assert ba.is_h_designator("H1") is True
    assert ba.is_h_designator("H23") is True
    assert ba.is_h_designator("H-1") is True
    assert ba.is_h_designator("H_A") is True


def test_is_h_designator_rejects_runway_headings():
    assert ba.is_h_designator("28L") is False
    assert ba.is_h_designator("09R") is False
    assert ba.is_h_designator("") is False
    assert ba.is_h_designator("HXY") is False


def test_is_helipad_both_ends_H_designator():
    row = {"le_ident": "H1", "he_ident": "H2", "length_ft": "50"}
    assert ba.is_helipad(row) is True


def test_is_helipad_short_runway_with_H_designator():
    row = {"le_ident": "H", "he_ident": "28L", "length_ft": "1500"}
    # H at one end + <2500 ft → helipad
    assert ba.is_helipad(row) is True


def test_is_helipad_long_runway_with_H_designator_is_not_helipad():
    row = {"le_ident": "H", "he_ident": "28L", "length_ft": "5000"}
    # H at one end but long runway — real airport (unusual but possible).
    assert ba.is_helipad(row) is False


def test_is_helipad_no_H_designator_is_not_helipad():
    row = {"le_ident": "28L", "he_ident": "10R", "length_ft": "8000"}
    assert ba.is_helipad(row) is False


def test_is_helipad_missing_length_field_defaults_to_zero():
    row = {"le_ident": "H", "he_ident": "28L", "length_ft": ""}
    # Missing length → treated as 0 → helipad
    assert ba.is_helipad(row) is True


def test_is_helipad_junk_length_field_defaults_to_zero():
    row = {"le_ident": "H", "he_ident": "28L", "length_ft": "not-a-number"}
    assert ba.is_helipad(row) is True

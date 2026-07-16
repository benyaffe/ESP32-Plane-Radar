// Tests for services::adsb JSON parsing. Drives ingestPayloadForTest
// directly with canned adsb.fi bodies so no HTTP / TLS / heap check
// runs. Locks the field-picker helpers (pickAltitude, pickGroundSpeed,
// pickSquawk, on-ground filter) end-to-end.

#include <unity.h>
#include <cstring>

#include "services/adsb_client.h"

#define NATIVE_STUBS_DEFINE
#include "../common/native_stubs.h"

namespace adsb = services::adsb;

// A single-aircraft body is enough for most assertions. Wrap the array
// literal in a fresh call — the parser mutates module state that persists
// across tests, so each test starts by ingesting the body it needs.
static const char* singleAircraftBody =
  "{\"ac\":[{"
  "\"hex\":\"ABC123\",\"flight\":\"UAL1234 \","
  "\"lat\":37.62,\"lon\":-122.375,"
  "\"alt_baro\":18000,\"gs\":420,\"track\":90,\"true_heading\":93,"
  "\"baro_rate\":-1200,\"squawk\":\"1200\",\"t\":\"B738\""
  "}]}";

void setUp(void) {
  // Force the aircraft list to empty by ingesting an empty array.
  adsb::ingestPayloadForTest("{\"ac\":[]}", std::strlen("{\"ac\":[]}"));
}

void tearDown(void) {}

void test_parse_populates_aircraft_list(void) {
  TEST_ASSERT_TRUE(adsb::ingestPayloadForTest(
      singleAircraftBody, std::strlen(singleAircraftBody)));
  TEST_ASSERT_EQUAL_UINT(1, adsb::aircraftCount());
  const adsb::Aircraft& a = adsb::aircraftList()[0];
  TEST_ASSERT_EQUAL_FLOAT(37.62f, a.lat);
  TEST_ASSERT_EQUAL_FLOAT(-122.375f, a.lon);
  TEST_ASSERT_EQUAL_INT32(18000, a.alt_ft);
  TEST_ASSERT_EQUAL_FLOAT(420.0f, a.gs_knots);
  // track = 90 but true_heading = 93 → nose_deg picks true_heading.
  TEST_ASSERT_EQUAL_FLOAT(93.0f, a.nose_deg);
  TEST_ASSERT_EQUAL_FLOAT(90.0f, a.track_deg);
  TEST_ASSERT_EQUAL_UINT16(1200, a.squawk);
}

void test_callsign_prefers_flight_over_registration(void) {
  TEST_ASSERT_TRUE(adsb::ingestPayloadForTest(
      singleAircraftBody, std::strlen(singleAircraftBody)));
  // "flight" is "UAL1234 " (trailing space); the parser trims.
  TEST_ASSERT_EQUAL_STRING("UAL1234", adsb::aircraftList()[0].callsign);
}

void test_callsign_falls_back_to_registration_only(void) {
  const char* reg_only =
    "{\"ac\":[{\"hex\":\"AAA\",\"r\":\"N12345\",\"lat\":0,\"lon\":0,\"alt_baro\":10000,\"gs\":100}]}";
  TEST_ASSERT_TRUE(adsb::ingestPayloadForTest(reg_only, std::strlen(reg_only)));
  TEST_ASSERT_EQUAL_STRING("N12345", adsb::aircraftList()[0].callsign);
}

void test_hex_only_leaves_callsign_empty(void) {
  // Hex-only tracks come from TIS-B / MLAT / ADS-R feeds — the "hex" is
  // often a synthetic ~-prefixed id, useless as a callsign. Callsign
  // stays empty; the render layer will skip the tag but still draw the
  // triangle.
  const char* hex_only =
    "{\"ac\":[{\"hex\":\"BBBEEE\",\"lat\":0,\"lon\":0,\"alt_baro\":10000,\"gs\":100}]}";
  TEST_ASSERT_TRUE(adsb::ingestPayloadForTest(hex_only, std::strlen(hex_only)));
  TEST_ASSERT_EQUAL_STRING("", adsb::aircraftList()[0].callsign);
}

void test_tilde_hex_only_leaves_callsign_empty(void) {
  // adsb.fi prefixes non-ICAO addresses with ~. Same expectation.
  const char* tisb =
    "{\"ac\":[{\"hex\":\"~2bb34b\",\"lat\":0,\"lon\":0,\"alt_baro\":10000,\"gs\":100}]}";
  TEST_ASSERT_TRUE(adsb::ingestPayloadForTest(tisb, std::strlen(tisb)));
  TEST_ASSERT_EQUAL_STRING("", adsb::aircraftList()[0].callsign);
}

void test_ground_aircraft_are_filtered_when_flag_is_off(void) {
  // "alt_baro":"ground" → on-ground. kAdsbShowGroundAircraft defaults
  // to false so this should be filtered out.
  const char* ground =
    "{\"ac\":[{\"hex\":\"ABC\",\"lat\":37,\"lon\":-122,\"alt_baro\":\"ground\",\"gs\":0}]}";
  TEST_ASSERT_TRUE(adsb::ingestPayloadForTest(ground, std::strlen(ground)));
  TEST_ASSERT_EQUAL_UINT(0, adsb::aircraftCount());
}

void test_missing_lat_lon_aircraft_are_skipped(void) {
  const char* mixed =
    "{\"ac\":["
    "{\"hex\":\"A\",\"lat\":37,\"lon\":-122,\"alt_baro\":10000,\"gs\":100},"
    "{\"hex\":\"B\",\"alt_baro\":10000,\"gs\":100},"                // no lat/lon
    "{\"hex\":\"C\",\"lat\":38,\"lon\":-123,\"alt_baro\":10000,\"gs\":100}"
    "]}";
  TEST_ASSERT_TRUE(adsb::ingestPayloadForTest(mixed, std::strlen(mixed)));
  TEST_ASSERT_EQUAL_UINT(2, adsb::aircraftCount());
}

void test_malformed_json_returns_false_and_leaves_list_alone(void) {
  TEST_ASSERT_TRUE(adsb::ingestPayloadForTest(
      singleAircraftBody, std::strlen(singleAircraftBody)));
  const size_t before = adsb::aircraftCount();

  const char* junk = "this is not json";
  TEST_ASSERT_FALSE(adsb::ingestPayloadForTest(junk, std::strlen(junk)));
  TEST_ASSERT_EQUAL_UINT(before, adsb::aircraftCount());
}

void test_empty_ac_array_populates_zero_aircraft(void) {
  const char* empty = "{\"ac\":[]}";
  TEST_ASSERT_TRUE(adsb::ingestPayloadForTest(empty, std::strlen(empty)));
  TEST_ASSERT_EQUAL_UINT(0, adsb::aircraftCount());
}

void test_missing_ac_key_populates_zero_aircraft(void) {
  const char* no_ac = "{}";
  TEST_ASSERT_TRUE(adsb::ingestPayloadForTest(no_ac, std::strlen(no_ac)));
  TEST_ASSERT_EQUAL_UINT(0, adsb::aircraftCount());
}

void test_fetchCount_increments_on_successful_parse(void) {
  const unsigned long before = adsb::fetchCount();
  adsb::ingestPayloadForTest(singleAircraftBody,
                             std::strlen(singleAircraftBody));
  TEST_ASSERT_TRUE(adsb::fetchCount() > before);
}

// --- Ghost dedup ---------------------------------------------------------

// Helper: build a two-aircraft payload with the given per-aircraft
// field snippets. Keeps the tests readable.
static void ingestTwo(const char* a_fields, const char* b_fields) {
  char body[1024];
  std::snprintf(body, sizeof(body), "{\"ac\":[{%s},{%s}]}", a_fields, b_fields);
  TEST_ASSERT_TRUE(adsb::ingestPayloadForTest(body, std::strlen(body)));
}

void test_source_enum_recognizes_every_documented_value(void) {
  TEST_ASSERT_EQUAL_INT((int)adsb::AdsbSource::AdsbIcao,
                        (int)adsb::parseSource("adsb_icao"));
  TEST_ASSERT_EQUAL_INT((int)adsb::AdsbSource::AdsbIcaoNt,
                        (int)adsb::parseSource("adsb_icao_nt"));
  TEST_ASSERT_EQUAL_INT((int)adsb::AdsbSource::AdsrIcao,
                        (int)adsb::parseSource("adsr_icao"));
  TEST_ASSERT_EQUAL_INT((int)adsb::AdsbSource::TisbIcao,
                        (int)adsb::parseSource("tisb_icao"));
  TEST_ASSERT_EQUAL_INT((int)adsb::AdsbSource::Mlat,
                        (int)adsb::parseSource("mlat"));
  TEST_ASSERT_EQUAL_INT((int)adsb::AdsbSource::TisbTrackfile,
                        (int)adsb::parseSource("tisb_trackfile"));
  TEST_ASSERT_EQUAL_INT((int)adsb::AdsbSource::TisbOther,
                        (int)adsb::parseSource("tisb_other"));
  TEST_ASSERT_EQUAL_INT((int)adsb::AdsbSource::AdsbOther,
                        (int)adsb::parseSource("adsb_other"));
  TEST_ASSERT_EQUAL_INT((int)adsb::AdsbSource::ModeS,
                        (int)adsb::parseSource("mode_s"));
  TEST_ASSERT_EQUAL_INT((int)adsb::AdsbSource::Adsc,
                        (int)adsb::parseSource("adsc"));
  // Unknown / absent / new source strings all bucket to Unknown, tier 0.
  TEST_ASSERT_EQUAL_INT((int)adsb::AdsbSource::Unknown,
                        (int)adsb::parseSource("some_future_source_type"));
  TEST_ASSERT_EQUAL_INT((int)adsb::AdsbSource::Unknown, (int)adsb::parseSource(""));
  TEST_ASSERT_EQUAL_INT((int)adsb::AdsbSource::Unknown, (int)adsb::parseSource(nullptr));
}

void test_confidence_tiers_are_correct(void) {
  TEST_ASSERT_EQUAL_UINT8(3, adsb::confidence(adsb::AdsbSource::AdsbIcao));
  TEST_ASSERT_EQUAL_UINT8(3, adsb::confidence(adsb::AdsbSource::AdsbIcaoNt));
  TEST_ASSERT_EQUAL_UINT8(2, adsb::confidence(adsb::AdsbSource::AdsrIcao));
  TEST_ASSERT_EQUAL_UINT8(2, adsb::confidence(adsb::AdsbSource::TisbIcao));
  TEST_ASSERT_EQUAL_UINT8(1, adsb::confidence(adsb::AdsbSource::Mlat));
  TEST_ASSERT_EQUAL_UINT8(0, adsb::confidence(adsb::AdsbSource::TisbTrackfile));
  TEST_ASSERT_EQUAL_UINT8(0, adsb::confidence(adsb::AdsbSource::TisbOther));
  TEST_ASSERT_EQUAL_UINT8(0, adsb::confidence(adsb::AdsbSource::AdsbOther));
  TEST_ASSERT_EQUAL_UINT8(0, adsb::confidence(adsb::AdsbSource::ModeS));
  TEST_ASSERT_EQUAL_UINT8(0, adsb::confidence(adsb::AdsbSource::Adsc));
  TEST_ASSERT_EQUAL_UINT8(0, adsb::confidence(adsb::AdsbSource::Unknown));
}

void test_ghost_no_identity_dropped_by_higher_tier(void) {
  // Real ORD observation: adsb_icao with callsign vs tisb_other with no
  // flight, no reg, ~-prefixed hex. 100 m apart, same altitude → drop
  // the TIS-B track.
  ingestTwo(
    "\"hex\":\"a704c0\",\"type\":\"adsb_icao\",\"flight\":\"UAL1234\","
    "\"lat\":37.6200,\"lon\":-122.3750,\"alt_baro\":18000,\"gs\":420,\"track\":90",
    "\"hex\":\"~270c06\",\"type\":\"tisb_other\","
    "\"lat\":37.6210,\"lon\":-122.3750,\"alt_baro\":18000,\"gs\":420,\"track\":90");
  TEST_ASSERT_EQUAL_UINT(1, adsb::aircraftCount());
  TEST_ASSERT_EQUAL_STRING("UAL1234", adsb::aircraftList()[0].callsign);
}

void test_ghost_matching_callsign_dropped_by_higher_tier(void) {
  // LAX morning observation: adsr_icao carries full identity, mlat has
  // matching flight but no reg. Drop the MLAT track.
  ingestTwo(
    "\"hex\":\"ad138e\",\"type\":\"adsr_icao\",\"flight\":\"N9412S\",\"r\":\"N9412S\","
    "\"lat\":34.0000,\"lon\":-118.0000,\"alt_baro\":8000,\"gs\":180,\"track\":45",
    "\"hex\":\"100000\",\"type\":\"mlat\",\"flight\":\"N9412S\","
    "\"lat\":34.0002,\"lon\":-118.0002,\"alt_baro\":8000,\"gs\":180,\"track\":45");
  TEST_ASSERT_EQUAL_UINT(1, adsb::aircraftCount());
  const adsb::Aircraft& kept = adsb::aircraftList()[0];
  TEST_ASSERT_EQUAL_STRING("N9412S", kept.callsign);
  TEST_ASSERT_EQUAL_INT((int)adsb::AdsbSource::AdsrIcao, (int)kept.source);
}

void test_ghost_matching_registration_dropped(void) {
  // Registration branch of the identity guard: callsigns differ but the
  // tail number matches.
  ingestTwo(
    "\"hex\":\"AAAA01\",\"type\":\"adsb_icao\",\"flight\":\"UAL9\",\"r\":\"N123AB\","
    "\"lat\":37.0,\"lon\":-122.0,\"alt_baro\":10000,\"gs\":250,\"track\":100",
    "\"hex\":\"AAAA02\",\"type\":\"adsr_icao\",\"r\":\"N123AB\","
    "\"lat\":37.0001,\"lon\":-122.0001,\"alt_baro\":10000,\"gs\":250,\"track\":100");
  TEST_ASSERT_EQUAL_UINT(1, adsb::aircraftCount());
  TEST_ASSERT_EQUAL_STRING("UAL9", adsb::aircraftList()[0].callsign);
}

void test_different_callsigns_both_survive(void) {
  // LAX approach-queue case: two aircraft in close formation with
  // different identities. Identity guard must keep both.
  ingestTwo(
    "\"hex\":\"a55f3f\",\"type\":\"adsr_icao\",\"flight\":\"B08\",\"r\":\"N445XB\","
    "\"lat\":34.0,\"lon\":-118.0,\"alt_baro\":8000,\"gs\":180,\"track\":45",
    "\"hex\":\"ac058a\",\"type\":\"adsb_icao\",\"flight\":\"DAL449\",\"r\":\"N874DN\","
    "\"lat\":34.001,\"lon\":-118.001,\"alt_baro\":8000,\"gs\":180,\"track\":45");
  TEST_ASSERT_EQUAL_UINT(2, adsb::aircraftCount());
}

void test_same_tier_pair_always_survives(void) {
  // Formation flight of two adsb_icao tracks. Same-tier rule keeps
  // both even at 100 m separation.
  ingestTwo(
    "\"hex\":\"AAA1\",\"type\":\"adsb_icao\",\"flight\":\"FLT1\","
    "\"lat\":37.0,\"lon\":-122.0,\"alt_baro\":10000,\"gs\":250,\"track\":90",
    "\"hex\":\"AAA2\",\"type\":\"adsb_icao\",\"flight\":\"FLT2\","
    "\"lat\":37.0005,\"lon\":-122.0005,\"alt_baro\":10000,\"gs\":250,\"track\":90");
  TEST_ASSERT_EQUAL_UINT(2, adsb::aircraftCount());
}

void test_far_apart_pair_both_survive(void) {
  // Cross-tier + matching callsign, but 5 nm apart — geometry gate fails.
  ingestTwo(
    "\"hex\":\"AAA1\",\"type\":\"adsb_icao\",\"flight\":\"N9412S\","
    "\"lat\":37.0,\"lon\":-122.0,\"alt_baro\":10000,\"gs\":250,\"track\":90",
    "\"hex\":\"AAA2\",\"type\":\"mlat\",\"flight\":\"N9412S\","
    "\"lat\":37.1,\"lon\":-122.0,\"alt_baro\":10000,\"gs\":250,\"track\":90");
  TEST_ASSERT_EQUAL_UINT(2, adsb::aircraftCount());
}

void test_altitude_too_different_both_survive(void) {
  ingestTwo(
    "\"hex\":\"AAA1\",\"type\":\"adsb_icao\",\"flight\":\"N9412S\","
    "\"lat\":37.0,\"lon\":-122.0,\"alt_baro\":10000,\"gs\":250,\"track\":90",
    "\"hex\":\"AAA2\",\"type\":\"mlat\",\"flight\":\"N9412S\","
    "\"lat\":37.0001,\"lon\":-122.0001,\"alt_baro\":11500,\"gs\":250,\"track\":90");
  TEST_ASSERT_EQUAL_UINT(2, adsb::aircraftCount());
}

void test_tilde_prefix_normalized_in_identity_match(void) {
  // adsb.fi sometimes prefixes callsigns / hex with ~ for non-ICAO
  // addresses. Match must still succeed once the prefix is stripped.
  ingestTwo(
    "\"hex\":\"AAA1\",\"type\":\"adsb_icao\",\"flight\":\"N9412S\","
    "\"lat\":37.0,\"lon\":-122.0,\"alt_baro\":10000,\"gs\":250,\"track\":90",
    "\"hex\":\"100000\",\"type\":\"mlat\",\"flight\":\"~N9412S\","
    "\"lat\":37.0001,\"lon\":-122.0001,\"alt_baro\":10000,\"gs\":250,\"track\":90");
  TEST_ASSERT_EQUAL_UINT(1, adsb::aircraftCount());
}

void test_unknown_source_treated_as_tier_zero(void) {
  // A brand-new source string upstream must be safely droppable by an
  // adsb_icao neighbor — never crash, never survive at tier 3.
  ingestTwo(
    "\"hex\":\"AAA1\",\"type\":\"adsb_icao\",\"flight\":\"UAL1\","
    "\"lat\":37.0,\"lon\":-122.0,\"alt_baro\":10000,\"gs\":250,\"track\":90",
    "\"hex\":\"~2bb\",\"type\":\"some_future_source_type\","
    "\"lat\":37.0001,\"lon\":-122.0001,\"alt_baro\":10000,\"gs\":250,\"track\":90");
  TEST_ASSERT_EQUAL_UINT(1, adsb::aircraftCount());
  TEST_ASSERT_EQUAL_STRING("UAL1", adsb::aircraftList()[0].callsign);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_populates_aircraft_list);
  RUN_TEST(test_callsign_prefers_flight_over_registration);
  RUN_TEST(test_callsign_falls_back_to_registration_only);
  RUN_TEST(test_hex_only_leaves_callsign_empty);
  RUN_TEST(test_tilde_hex_only_leaves_callsign_empty);
  RUN_TEST(test_ground_aircraft_are_filtered_when_flag_is_off);
  RUN_TEST(test_missing_lat_lon_aircraft_are_skipped);
  RUN_TEST(test_malformed_json_returns_false_and_leaves_list_alone);
  RUN_TEST(test_empty_ac_array_populates_zero_aircraft);
  RUN_TEST(test_missing_ac_key_populates_zero_aircraft);
  RUN_TEST(test_fetchCount_increments_on_successful_parse);
  RUN_TEST(test_source_enum_recognizes_every_documented_value);
  RUN_TEST(test_confidence_tiers_are_correct);
  RUN_TEST(test_ghost_no_identity_dropped_by_higher_tier);
  RUN_TEST(test_ghost_matching_callsign_dropped_by_higher_tier);
  RUN_TEST(test_ghost_matching_registration_dropped);
  RUN_TEST(test_different_callsigns_both_survive);
  RUN_TEST(test_same_tier_pair_always_survives);
  RUN_TEST(test_far_apart_pair_both_survive);
  RUN_TEST(test_altitude_too_different_both_survive);
  RUN_TEST(test_tilde_prefix_normalized_in_identity_match);
  RUN_TEST(test_unknown_source_treated_as_tier_zero);
  return UNITY_END();
}

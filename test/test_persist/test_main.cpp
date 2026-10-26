#include <unity.h>
#include <string.h>
#include "alert_types.h"
#include "persisted_state.h"

void setUp() {}
void tearDown() {}

static void reseal(uint8_t* b) {
  uint32_t c = crc32Ieee(b, 40);
  b[40] = (uint8_t)c; b[41] = (uint8_t)(c >> 8); b[42] = (uint8_t)(c >> 16); b[43] = (uint8_t)(c >> 24);
}

static PersistedState sample(Mode m, const char* receipt, uint32_t epoch) {
  PersistedState s;
  s.mode = m;
  strncpy(s.receipt, receipt, kReceiptLen - 1);
  s.confirmedAtEpoch = epoch;
  return s;
}

static void assertDefault(const PersistedState& s) {
  PersistedState d;
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Mode::Idle, (uint8_t)s.mode);
  TEST_ASSERT_EQUAL_UINT32(0, s.confirmedAtEpoch);
  TEST_ASSERT_EQUAL_MEMORY(d.receipt, s.receipt, kReceiptLen);
}

void test_crc32_known_vector() {
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32Ieee((const uint8_t*)"123456789", 9));
}

void test_layout() {
  uint8_t b[kPersistedBlobLen];
  encodeState(sample(Mode::Confirmed, "abc", 0x01020304u), b);
  TEST_ASSERT_EQUAL_UINT8(1, b[0]);
  TEST_ASSERT_EQUAL_UINT8(3, b[1]);
  TEST_ASSERT_EQUAL_UINT8('a', b[2]);
  TEST_ASSERT_EQUAL_UINT8(0, b[5]);
  TEST_ASSERT_EQUAL_UINT8(0x04, b[34]);
  TEST_ASSERT_EQUAL_UINT8(0x01, b[37]);
  TEST_ASSERT_EQUAL_UINT8(0, b[38]);
  TEST_ASSERT_EQUAL_UINT8(0, b[39]);
  TEST_ASSERT_EQUAL_UINT8(0, b[44]);
  TEST_ASSERT_EQUAL_UINT8(0, b[47]);
}

void test_encode_zeroes_after_nul_and_terminates() {
  PersistedState s;
  memset(s.receipt, 'x', kReceiptLen);  // not terminated
  s.receipt[3] = '\0';
  s.receipt[4] = 'Z';
  uint8_t b[kPersistedBlobLen];
  encodeState(s, b);
  for (size_t i = 3; i < kReceiptLen; i++) TEST_ASSERT_EQUAL_UINT8(0, b[2 + i]);
  PersistedState u;
  memset(u.receipt, 'y', kReceiptLen);  // fully unterminated
  encodeState(u, b);
  TEST_ASSERT_EQUAL_UINT8(0, b[2 + kReceiptLen - 1]);
  PersistedState o;
  TEST_ASSERT_TRUE(decodeState(b, kPersistedBlobLen, o));
}

void test_roundtrip_awaiting_ack() {
  uint8_t b[kPersistedBlobLen];
  encodeState(sample(Mode::EmergencyAwaitingAck, "rcptABCDEFGHIJKLMNOPQRSTUVWXYZ", 0), b);
  PersistedState o;
  TEST_ASSERT_TRUE(decodeState(b, kPersistedBlobLen, o));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Mode::EmergencyAwaitingAck, (uint8_t)o.mode);
  TEST_ASSERT_EQUAL_STRING("rcptABCDEFGHIJKLMNOPQRSTUVWXYZ", o.receipt);
  TEST_ASSERT_EQUAL_UINT32(0, o.confirmedAtEpoch);
}

void test_roundtrip_confirmed_epoch() {
  uint8_t b[kPersistedBlobLen];
  encodeState(sample(Mode::Confirmed, "r1", 0xDEADBEEFu), b);
  PersistedState o;
  TEST_ASSERT_TRUE(decodeState(b, kPersistedBlobLen, o));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Mode::Confirmed, (uint8_t)o.mode);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, o.confirmedAtEpoch);
  TEST_ASSERT_EQUAL_STRING("r1", o.receipt);
}

void test_bad_crc_rejected() {
  uint8_t b[kPersistedBlobLen];
  encodeState(sample(Mode::Confirmed, "r1", 5), b);
  b[34] ^= 1;
  PersistedState o = sample(Mode::Confirmed, "junk", 9);
  TEST_ASSERT_FALSE(decodeState(b, kPersistedBlobLen, o));
  assertDefault(o);
}

void test_wrong_len_rejected() {
  uint8_t b[kPersistedBlobLen + 1] = {};
  encodeState(sample(Mode::Confirmed, "r1", 5), b);
  PersistedState o = sample(Mode::Confirmed, "junk", 9);
  TEST_ASSERT_FALSE(decodeState(b, kPersistedBlobLen - 1, o));
  assertDefault(o);
  o = sample(Mode::Confirmed, "junk", 9);
  TEST_ASSERT_FALSE(decodeState(b, kPersistedBlobLen + 1, o));
  assertDefault(o);
}

void test_wrong_version_rejected() {
  uint8_t b[kPersistedBlobLen];
  encodeState(sample(Mode::Confirmed, "r1", 5), b);
  b[0] = 2;
  reseal(b);
  PersistedState o = sample(Mode::Confirmed, "junk", 9);
  TEST_ASSERT_FALSE(decodeState(b, kPersistedBlobLen, o));
  assertDefault(o);
}

void test_invalid_mode_rejected() {
  uint8_t b[kPersistedBlobLen];
  encodeState(sample(Mode::Confirmed, "r1", 5), b);
  b[1] = 4;
  reseal(b);
  PersistedState o = sample(Mode::Confirmed, "junk", 9);
  TEST_ASSERT_FALSE(decodeState(b, kPersistedBlobLen, o));
  assertDefault(o);
}

void test_unterminated_receipt_rejected() {
  uint8_t b[kPersistedBlobLen];
  encodeState(sample(Mode::Confirmed, "r1", 5), b);
  memset(b + 2, 'x', kReceiptLen);
  reseal(b);
  PersistedState o = sample(Mode::Confirmed, "junk", 9);
  TEST_ASSERT_FALSE(decodeState(b, kPersistedBlobLen, o));
  assertDefault(o);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc32_known_vector);
  RUN_TEST(test_layout);
  RUN_TEST(test_encode_zeroes_after_nul_and_terminates);
  RUN_TEST(test_roundtrip_awaiting_ack);
  RUN_TEST(test_roundtrip_confirmed_epoch);
  RUN_TEST(test_bad_crc_rejected);
  RUN_TEST(test_wrong_len_rejected);
  RUN_TEST(test_wrong_version_rejected);
  RUN_TEST(test_invalid_mode_rejected);
  RUN_TEST(test_unterminated_receipt_rejected);
  return UNITY_END();
}

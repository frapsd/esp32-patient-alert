#include <unity.h>
#include <map>
#include <string.h>
#include <string>
#include <utility>
#include <vector>
#include "alert_types.h"
#include "pushover_protocol.h"

void setUp() {}
void tearDown() {}

static int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}

static std::string pctDecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size()) {
      o += (char)(hexv(s[i + 1]) * 16 + hexv(s[i + 2]));
      i += 2;
    } else {
      o += s[i];
    }
  }
  return o;
}

static std::map<std::string, std::string> formDecode(const std::string& body) {
  std::map<std::string, std::string> m;
  size_t pos = 0;
  while (pos <= body.size()) {
    size_t amp = body.find('&', pos);
    if (amp == std::string::npos) amp = body.size();
    std::string kv = body.substr(pos, amp - pos);
    size_t eq = kv.find('=');
    if (eq != std::string::npos) m[pctDecode(kv.substr(0, eq))] = pctDecode(kv.substr(eq + 1));
    pos = amp + 1;
  }
  return m;
}

static void test_classify_table() {
  TEST_ASSERT_TRUE(classifyHttp(-1, false) == HttpOutcome::Transient);
  TEST_ASSERT_TRUE(classifyHttp(200, true) == HttpOutcome::Ok);
  TEST_ASSERT_TRUE(classifyHttp(200, false) == HttpOutcome::Rejected);
  TEST_ASSERT_TRUE(classifyHttp(400, false) == HttpOutcome::Rejected);
  TEST_ASSERT_TRUE(classifyHttp(429, false) == HttpOutcome::Rejected);
  TEST_ASSERT_TRUE(classifyHttp(500, false) == HttpOutcome::Transient);
  TEST_ASSERT_TRUE(classifyHttp(503, false) == HttpOutcome::Transient);
  TEST_ASSERT_TRUE(classifyHttp(302, false) == HttpOutcome::Transient);
}

static void test_parse_send_ok_with_receipt() {
  auto r = parseSendResponse(200, "{\"status\":1,\"request\":\"x\",\"receipt\":\"rABC\"}");
  TEST_ASSERT_TRUE(r.outcome == HttpOutcome::Ok);
  TEST_ASSERT_EQUAL_STRING("rABC", r.receipt);
}

static void test_parse_send_rejected() {
  auto r = parseSendResponse(400, "{\"status\":0,\"errors\":[\"application token is invalid\"]}");
  TEST_ASSERT_TRUE(r.outcome == HttpOutcome::Rejected);
  TEST_ASSERT_EQUAL_STRING("", r.receipt);
}

static void test_parse_send_garbage_200() {
  auto r = parseSendResponse(200, "<html>");
  TEST_ASSERT_TRUE(r.outcome == HttpOutcome::Transient);
}

static void test_parse_send_long_receipt_truncated() {
  std::string body = "{\"status\":1,\"receipt\":\"" + std::string(100, 'a') + "\"}";
  auto r = parseSendResponse(200, body.c_str());
  TEST_ASSERT_TRUE(r.outcome == HttpOutcome::Ok);
  TEST_ASSERT_EQUAL(31, strlen(r.receipt));
  TEST_ASSERT_EQUAL(0, r.receipt[kReceiptLen - 1]);
}

static void test_parse_receipt_acked() {
  auto r = parseReceiptResponse(200,
      "{\"status\":1,\"acknowledged\":1,\"acknowledged_by_device\":\"pixel-marco\",\"expired\":0}");
  TEST_ASSERT_TRUE(r.outcome == HttpOutcome::Ok);
  TEST_ASSERT_TRUE(r.acknowledged);
  TEST_ASSERT_FALSE(r.expired);
  TEST_ASSERT_EQUAL_STRING("pixel-marco", r.ackDevice);
}

static void test_parse_receipt_expired_unacked() {
  auto r = parseReceiptResponse(200, "{\"status\":1,\"acknowledged\":0,\"expired\":1}");
  TEST_ASSERT_TRUE(r.outcome == HttpOutcome::Ok);
  TEST_ASSERT_TRUE(r.expired);
  TEST_ASSERT_FALSE(r.acknowledged);
  TEST_ASSERT_EQUAL_STRING("", r.ackDevice);
}

static void test_parse_receipt_404() {
  auto r = parseReceiptResponse(404, "{\"status\":0}");
  TEST_ASSERT_TRUE(r.outcome == HttpOutcome::Rejected);
}

static void test_parse_receipt_garbage_200_and_long_device() {
  auto g = parseReceiptResponse(200, "<html>");
  TEST_ASSERT_TRUE(g.outcome == HttpOutcome::Transient);
  std::string body = "{\"status\":1,\"acknowledged\":1,\"acknowledged_by_device\":\"" + std::string(200, 'd') + "\"}";
  auto r = parseReceiptResponse(200, body.c_str());
  TEST_ASSERT_EQUAL(kDeviceLen - 1, strlen(r.ackDevice));
}

static void test_form_encode_utf8() {
  std::string s = formEncode({{"m", "a b\xE2\x80\x94\xC3\xA8&="}});
  TEST_ASSERT_EQUAL_STRING("m=a%20b%E2%80%94%C3%A8%26%3D", s.c_str());
}

static void test_emergency_body_fields() {
  auto m = formDecode(emergencyBody("TOK", "USR"));
  TEST_ASSERT_EQUAL_STRING("TOK", m["token"].c_str());
  TEST_ASSERT_EQUAL_STRING("USR", m["user"].c_str());
  TEST_ASSERT_EQUAL_STRING("2", m["priority"].c_str());
  TEST_ASSERT_EQUAL_STRING("30", m["retry"].c_str());
  TEST_ASSERT_EQUAL_STRING("1500", m["expire"].c_str());
  TEST_ASSERT_EQUAL_STRING("persistent", m["sound"].c_str());
  TEST_ASSERT_EQUAL_STRING("EMERGENZA", m["title"].c_str());
  TEST_ASSERT_EQUAL_STRING(
      "EMERGENZA \xE2\x80\x94 il paziente ha premuto il bottone. Conferma quando hai preso in carico.",
      m["message"].c_str());
}

static void test_light_body_with_and_without_time() {
  auto a = formDecode(lightBody("TOK", "USR", "14:32"));
  TEST_ASSERT_EQUAL_STRING("Il paziente chiede aiuto (non urgente). Richiesta delle 14:32.", a["message"].c_str());
  TEST_ASSERT_EQUAL_STRING("0", a["priority"].c_str());
  TEST_ASSERT_EQUAL_STRING("Richiesta di aiuto", a["title"].c_str());
  auto b = formDecode(lightBody("TOK", "USR", ""));
  TEST_ASSERT_EQUAL_STRING("Il paziente chiede aiuto (non urgente).", b["message"].c_str());
  auto c = formDecode(lightBody("TOK", "USR", nullptr));
  TEST_ASSERT_EQUAL_STRING("Il paziente chiede aiuto (non urgente).", c["message"].c_str());
}

static void test_ack_notice_named_and_anonymous() {
  auto a = formDecode(ackNoticeBody("TOK", "USR", "pixel-marco"));
  TEST_ASSERT_EQUAL_STRING("pixel-marco ha preso in carico l'emergenza.", a["message"].c_str());
  TEST_ASSERT_EQUAL_STRING("Presa in carico", a["title"].c_str());
  TEST_ASSERT_EQUAL_STRING("0", a["priority"].c_str());
  auto b = formDecode(ackNoticeBody("TOK", "USR", ""));
  TEST_ASSERT_EQUAL_STRING("Qualcuno ha preso in carico l'emergenza.", b["message"].c_str());
}

static void test_receipt_path() {
  TEST_ASSERT_EQUAL_STRING("/1/receipts/rABC.json?token=TOK", receiptPath("rABC", "TOK").c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_classify_table);
  RUN_TEST(test_parse_send_ok_with_receipt);
  RUN_TEST(test_parse_send_rejected);
  RUN_TEST(test_parse_send_garbage_200);
  RUN_TEST(test_parse_send_long_receipt_truncated);
  RUN_TEST(test_parse_receipt_acked);
  RUN_TEST(test_parse_receipt_expired_unacked);
  RUN_TEST(test_parse_receipt_404);
  RUN_TEST(test_parse_receipt_garbage_200_and_long_device);
  RUN_TEST(test_form_encode_utf8);
  RUN_TEST(test_emergency_body_fields);
  RUN_TEST(test_light_body_with_and_without_time);
  RUN_TEST(test_ack_notice_named_and_anonymous);
  RUN_TEST(test_receipt_path);
  return UNITY_END();
}

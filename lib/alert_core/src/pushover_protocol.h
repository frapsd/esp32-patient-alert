#pragma once
#include <string>
#include <utility>
#include <vector>
#include "alert_types.h"

HttpOutcome classifyHttp(int httpCode, bool bodyStatusOk);

struct SendResponse {
  HttpOutcome outcome;
  char receipt[kReceiptLen];
};
SendResponse parseSendResponse(int httpCode, const char* body);

struct ReceiptStatus {
  HttpOutcome outcome;
  bool acknowledged;
  bool expired;
  char ackDevice[kDeviceLen];
};
ReceiptStatus parseReceiptResponse(int httpCode, const char* body);

std::string formEncode(const std::vector<std::pair<std::string, std::string>>& fields);
std::string lightBody(const char* token, const char* user, const char* hhmm);
std::string emergencyBody(const char* token, const char* user);
std::string ackNoticeBody(const char* token, const char* user, const char* device);
std::string receiptPath(const char* receipt, const char* token);

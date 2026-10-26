#pragma once
#include <string>

struct HttpResult {
  int code;          // HTTP status, or <= 0 on a network/TLS/timeout error
  std::string body;  // at most 4 KB
};

// HTTPS to api.pushover.net, certificate verified against PUSHOVER_CA_PEM.
// Synchronous; feeds the task watchdog between phases (must be called from
// the loop task, which is subscribed to it). Never logs form or query, which
// contain the app token.
HttpResult pushoverPost(const char* path, const std::string& form);
HttpResult pushoverGet(const std::string& pathAndQuery);

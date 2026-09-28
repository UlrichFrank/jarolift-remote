#include "dongle_client.h"

#ifdef USE_ESP32

#include <esp_http_client.h>

namespace esphome {
namespace jarolift {

static bool post_api(const std::string &host, const char *cmd, std::string *body, std::string *error) {
  std::string url = "http://" + host + "/api";
  esp_http_client_config_t cfg{};
  cfg.url = url.c_str();
  cfg.method = HTTP_METHOD_POST;
  cfg.timeout_ms = 4000;
  cfg.disable_auto_redirect = true;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr) {
    *error = "HTTP client could not be created";
    return false;
  }
  std::string form = std::string("cmd=") + cmd;
  esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
  bool ok = false;
  esp_err_t err = esp_http_client_open(client, int(form.size()));
  if (err != ESP_OK) {
    *error = std::string("dongle not reachable: ") + esp_err_to_name(err);
  } else if (esp_http_client_write(client, form.data(), int(form.size())) < 0) {
    *error = "sending the request failed";
  } else if (esp_http_client_fetch_headers(client) < 0) {
    *error = "no answer from the dongle";
  } else if (esp_http_client_get_status_code(client) != 200) {
    *error = "dongle answered HTTP " + std::to_string(esp_http_client_get_status_code(client));
  } else {
    char buf[512];
    int n;
    body->clear();
    while ((n = esp_http_client_read(client, buf, sizeof(buf))) > 0 && body->size() < 8192) body->append(buf, size_t(n));
    ok = n >= 0;
    if (!ok) *error = "reading the answer failed";
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return ok;
}

bool read_dongle(const std::string &host, bool names, DongleConfig *out, std::string *error) {
  std::string body;
  // Sent with raw spaces, exactly like the dongle's own web page does.
  if (!post_api(host, "get config", &body, error)) return false;
  DongleConfig cfg;
  if (!parse_dongle_config(body, &cfg, error)) {
    *error = "unexpected answer: " + *error;
    return false;
  }
  if (names) {
    if (!post_api(host, "get channel name", &body, error)) return false;
    parse_dongle_channel_names(body, &cfg);
  }
  *out = cfg;
  return true;
}

}  // namespace jarolift
}  // namespace esphome

#endif

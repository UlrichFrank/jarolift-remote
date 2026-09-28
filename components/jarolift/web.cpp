#include "web.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include <esp_http_server.h>
#include <esp_https_server.h>
#include <esp_system.h>
#include <mbedtls/base64.h>

#include "core_commands.h"
#include "core_dongle.h"
#include "core_url.h"
#include "core_validate.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/network/util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "dongle_client.h"
#include "jarolift.h"
#include "tls_store.h"

namespace esphome {
namespace jarolift {

static const char *const TAG = "jarolift.web";
static constexpr size_t MAX_BODY = 16 * 1024;

// Generated from www/index.html by __init__.py (gzip).
extern const uint8_t INDEX_HTML_GZ[];
extern const size_t INDEX_HTML_GZ_LEN;

namespace {

struct WebState {
  JaroliftComponent *hub;
  std::string credentials;  // "user:password"
  TlsStore tls;
  httpd_handle_t https{nullptr};
  httpd_handle_t http{nullptr};
  // Last dongle read (httpd task only), applied by POST /api/migration/apply.
  bool dongle_read{false};
  std::string dongle_host;
  DongleConfig dongle;
};
WebState *web = nullptr;

void start_https();

// ------------------------------------------------------------------------------------------
// Helpers

const char *reset_reason_name(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_SW:
      return "software restart";
    case ESP_RST_PANIC:
      return "crash (panic)";
    case ESP_RST_INT_WDT:
      return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
      return "task watchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_EXT:
      return "external reset";
    default:
      return "other";
  }
}

std::string header(httpd_req_t *req, const char *name) {
  size_t len = httpd_req_get_hdr_value_len(req, name);
  if (len == 0) return "";
  std::string value(len + 1, '\0');
  if (httpd_req_get_hdr_value_str(req, name, &value[0], len + 1) != ESP_OK) return "";
  value.resize(len);
  return value;
}

std::string path_of(httpd_req_t *req) {
  std::string uri = req->uri;
  size_t q = uri.find('?');
  return q == std::string::npos ? uri : uri.substr(0, q);
}

std::string query_arg(httpd_req_t *req, const char *key) {
  size_t len = httpd_req_get_url_query_len(req);
  if (len == 0) return "";
  std::string query(len + 1, '\0');
  if (httpd_req_get_url_query_str(req, &query[0], len + 1) != ESP_OK) return "";
  char value[32];
  if (httpd_query_key_value(query.c_str(), key, value, sizeof(value)) != ESP_OK) return "";
  return value;
}

bool constant_time_equal(const std::string &a, const std::string &b) {
  unsigned char diff = a.size() == b.size() ? 0 : 1;
  for (size_t i = 0; i < a.size(); i++) diff |= uint8_t(a[i]) ^ uint8_t(i < b.size() ? b[i] : 0);
  return diff == 0;
}

bool authorised(httpd_req_t *req) {
  std::string auth = header(req, "Authorization");
  if (auth.compare(0, 6, "Basic ") == 0) {
    unsigned char decoded[160];
    size_t out = 0;
    std::string b64 = auth.substr(6);
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &out, reinterpret_cast<const unsigned char *>(b64.data()),
                              b64.size()) == 0 &&
        constant_time_equal(std::string(reinterpret_cast<char *>(decoded), out), web->credentials))
      return true;
  }
  httpd_resp_set_status(req, "401 Unauthorized");
  httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"jarolift-remote\", charset=\"UTF-8\"");
  httpd_resp_send(req, "authentication required", HTTPD_RESP_USE_STRLEN);
  return false;
}

esp_err_t send_json(httpd_req_t *req, int code, JsonDocument &doc) {
  std::string out;
  serializeJson(doc, out);
  httpd_resp_set_status(req, code == 200 ? "200 OK" : code == 400 ? "400 Bad Request" : "404 Not Found");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, out.data(), out.size());
}

esp_err_t send_ok(httpd_req_t *req) {
  JsonDocument doc;
  doc["ok"] = true;
  return send_json(req, 200, doc);
}

esp_err_t send_errors(httpd_req_t *req, const std::vector<FieldError> &errors) {
  JsonDocument doc;
  JsonArray arr = doc["errors"].to<JsonArray>();
  for (const auto &e : errors) {
    JsonObject o = arr.add<JsonObject>();
    o["field"] = e.field;
    o["message"] = e.message;
  }
  return send_json(req, 400, doc);
}

// Reads the whole request body (at most MAX_BODY bytes).
bool read_body(httpd_req_t *req, std::string *body) {
  if (req->content_len > MAX_BODY) return false;
  body->resize(req->content_len);
  size_t received = 0;
  while (received < req->content_len) {
    int n = httpd_req_recv(req, &(*body)[received], req->content_len - received);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (n <= 0) return false;
    received += size_t(n);
  }
  return true;
}

bool parse_json_body(httpd_req_t *req, JsonDocument &doc) {
  std::string body;
  if (!read_body(req, &body)) {
    send_errors(req, {{"", "request body missing or larger than 16 kB"}});
    return false;
  }
  if (deserializeJson(doc, body) || !doc.is<JsonObject>()) {
    send_errors(req, {{"", "expected a JSON object"}});
    return false;
  }
  return true;
}

void certificate_to_json(JsonObject o) {
  const auto &info = web->tls.info();
  o["subject"] = info.subject;
  o["issuer"] = info.issuer;
  o["not_after"] = info.not_after;
  o["self_signed"] = info.self_signed;
}

// ------------------------------------------------------------------------------------------
// Settings <-> JSON

void settings_to_json(const Settings &s, JsonObject root) {
  root["dhcp"] = s.dhcp;
  root["ip"] = format_ipv4(s.ip);
  root["subnet"] = format_ipv4(s.subnet);
  root["gateway"] = format_ipv4(s.gateway);
  root["dns"] = format_ipv4(s.dns);
  root["broker_url"] = s.broker_url;
  root["mqtt_username"] = s.mqtt_username;
  root["mqtt_password_set"] = !s.mqtt_password.empty();
  root["client_id"] = s.client_id;
  root["ca_certificate"] = s.ca_certificate;
  root["master_key_set"] = s.master_msb != 0 || s.master_lsb != 0;
  root["serial_prefix"] = format_hex(s.serial_prefix, 24);
  root["learn_mode_new"] = s.learn_mode_new;
  JsonArray channels = root["channels"].to<JsonArray>();
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    const auto &c = s.channels[ch];
    JsonObject o = channels.add<JsonObject>();
    o["channel"] = ch;
    o["enabled"] = c.enabled;
    o["name"] = c.name;
    o["label"] = c.label;
  }
  JsonArray groups = root["groups"].to<JsonArray>();
  for (uint8_t g = 0; g < MAX_GROUPS; g++) {
    const auto &grp = s.groups[g];
    JsonObject o = groups.add<JsonObject>();
    o["index"] = g;
    o["enabled"] = grp.enabled;
    o["name"] = grp.name;
    o["label"] = grp.label;
    JsonArray members = o["members"].to<JsonArray>();
    for (uint8_t m = 0; m < NUM_CHANNELS; m++)
      if (grp.members & (1u << m)) members.add(m);
  }
}

void settings_from_json(JsonObject root, Settings &s, std::vector<FieldError> &errors, bool *prefix_changed) {
  auto ip_field = [&](const char *key, uint32_t *out) {
    if (!root[key].is<const char *>()) return;
    uint32_t v;
    if (parse_ipv4(root[key].as<std::string>(), &v)) {
      *out = v;
    } else {
      errors.push_back({key, "invalid IPv4 address"});
    }
  };
  auto str_field = [&](const char *key, std::string *out) {
    if (root[key].is<const char *>()) *out = root[key].as<std::string>();
  };
  auto hex_field = [&](const char *key, int bits, uint32_t *out) -> bool {
    if (!root[key].is<const char *>()) return false;
    uint32_t v;
    if (!parse_hex(root[key].as<std::string>(), bits, &v)) {
      errors.push_back({key, "expected a " + std::to_string(bits) + "-bit hexadecimal number"});
      return false;
    }
    *out = v;
    return true;
  };

  if (root["dhcp"].is<bool>()) s.dhcp = root["dhcp"].as<bool>();
  ip_field("ip", &s.ip);
  if (root["subnet"].is<const char *>()) {
    uint32_t v;
    if (parse_netmask(root["subnet"].as<std::string>(), &v)) {
      s.subnet = v;
    } else {
      errors.push_back({"subnet", "invalid network mask"});
    }
  }
  ip_field("gateway", &s.gateway);
  ip_field("dns", &s.dns);
  str_field("broker_url", &s.broker_url);
  str_field("mqtt_username", &s.mqtt_username);
  str_field("mqtt_password", &s.mqtt_password);  // absent = unchanged, "" = no password
  str_field("client_id", &s.client_id);
  str_field("ca_certificate", &s.ca_certificate);
  hex_field("master_msb", 32, &s.master_msb);
  hex_field("master_lsb", 32, &s.master_lsb);
  uint32_t prefix;
  if (hex_field("serial_prefix", 24, &prefix) && prefix != s.serial_prefix) {
    s.serial_prefix = prefix;
    *prefix_changed = true;
  }
  if (root["learn_mode_new"].is<bool>()) s.learn_mode_new = root["learn_mode_new"].as<bool>();

  // "channels": [{"channel": n, "enabled", "name", "label"}], "groups": [{"index": n, "enabled",
  // "name", "label", "members": [channel, ...]}] - only the listed entries and fields change.
  if (root["channels"].is<JsonArray>()) {
    for (JsonObject o : root["channels"].as<JsonArray>()) {
      int ch = o["channel"] | -1;
      if (ch < 0 || ch >= NUM_CHANNELS) {
        errors.push_back({"channels", "channel number must be 0 to 15"});
        continue;
      }
      auto &c = s.channels[ch];
      if (o["enabled"].is<bool>()) c.enabled = o["enabled"].as<bool>();
      if (o["name"].is<const char *>()) c.name = o["name"].as<std::string>();
      if (o["label"].is<const char *>()) c.label = o["label"].as<std::string>();
    }
  }
  if (root["groups"].is<JsonArray>()) {
    for (JsonObject o : root["groups"].as<JsonArray>()) {
      int g = o["index"] | -1;
      if (g < 0 || g >= MAX_GROUPS) {
        errors.push_back({"groups", "group index must be 0 to 7"});
        continue;
      }
      auto &grp = s.groups[g];
      if (o["enabled"].is<bool>()) grp.enabled = o["enabled"].as<bool>();
      if (o["name"].is<const char *>()) grp.name = o["name"].as<std::string>();
      if (o["label"].is<const char *>()) grp.label = o["label"].as<std::string>();
      if (o["members"].is<JsonArray>()) {
        grp.members = 0;
        for (JsonVariant m : o["members"].as<JsonArray>()) {
          int mc = m | -1;
          if (mc >= 0 && mc < NUM_CHANNELS) {
            grp.members |= uint16_t(1u << mc);
          } else {
            errors.push_back({"grp" + std::to_string(g) + ".members", "member channel must be 0 to 15"});
          }
        }
      }
    }
  }
}

// ------------------------------------------------------------------------------------------
// Handlers

esp_err_t handle_status(httpd_req_t *req) {
  JaroliftComponent *hub = web->hub;
  JsonDocument doc;
  char build[Application::BUILD_TIME_STR_SIZE];
  App.get_build_time_string(build);
  doc["firmware"] = build;
  std::string ip;
  for (auto &addr : network::get_ip_addresses()) {
    if (addr.is_set() && addr.is_ip4()) {
      char ipbuf[network::IP_ADDRESS_BUFFER_SIZE];
      ip = addr.str_to(ipbuf);
    }
  }
  doc["ip"] = ip;
  doc["network_connected"] = network::is_connected();
  doc["uptime_s"] = millis() / 1000;
  doc["reset_reason"] = reset_reason_name(esp_reset_reason());
  doc["radio"] = hub->radio_present();
  Settings settings = hub->settings_copy();
  doc["broker_url"] = settings.broker_url;
  doc["broker_state"] = hub->mqtt().state_name();
  doc["broker_error"] = hub->mqtt().last_error();
  doc["learn_mode_new"] = settings.learn_mode_new;
  doc["counter"] = hub->counters().get();
  MigrationStatus mig = hub->migration_status();
  JsonObject m = doc["migration"].to<JsonObject>();
  m["active"] = mig.active;
  if (mig.active) {
    m["host"] = mig.host;
    m["dongle_reachable"] = mig.dongle_reachable;
    m["dongle_counter"] = mig.dongle_counter;
    m["margin"] = mig.margin;
    m["last_check_s_ago"] = millis() / 1000 - mig.last_check_s;
  }
  certificate_to_json(doc["certificate"].to<JsonObject>());
  JsonArray shutters = doc["shutters"].to<JsonArray>();
  for (const auto &s : hub->shutter_infos()) {
    JsonObject o = shutters.add<JsonObject>();
    o["name"] = s.name;
    o["label"] = s.label;
    o["channel"] = s.channel;
    o["state"] = s.state;
    o["position"] = s.position;
    if (!std::isnan(s.open_duration)) o["open_duration"] = s.open_duration;
    if (!std::isnan(s.close_duration)) o["close_duration"] = s.close_duration;
  }
  JsonArray groups = doc["groups"].to<JsonArray>();
  for (const auto &g : hub->group_infos()) {
    JsonObject o = groups.add<JsonObject>();
    o["index"] = g.index;
    o["name"] = g.name;
    o["label"] = g.label;
    JsonArray members = o["members"].to<JsonArray>();
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++)
      if (g.members & (1u << ch))
        members.add(settings.channels[ch].label.empty() ? settings.channels[ch].name : settings.channels[ch].label);
  }
  return send_json(req, 200, doc);
}

esp_err_t handle_settings_post(httpd_req_t *req) {
  JsonDocument in;
  if (!parse_json_body(req, in)) return ESP_OK;
  JsonObject root = in.as<JsonObject>();
  Settings wanted = web->hub->settings_copy();
  std::vector<FieldError> errors;
  bool prefix_changed = false;
  settings_from_json(root, wanted, errors, &prefix_changed);
  if (prefix_changed && !(root["confirm_regenerate_serials"] | false))
    errors.push_back({"serial_prefix", "changing the serial prefix requires confirmation: every motor must be taught again"});
  // Report range and consistency problems of the other fields in the same response.
  for (auto &e : validate_settings(wanted)) {
    bool known = false;
    for (auto &f : errors) known = known || f.field == e.field;
    if (!known) errors.push_back(e);
  }
  if (!errors.empty()) return send_errors(req, errors);
  bool restart_required = false;
  errors = web->hub->update_settings(wanted, &restart_required);
  if (!errors.empty()) return send_errors(req, errors);
  JsonDocument out;
  out["ok"] = true;
  out["restart_required"] = restart_required;
  return send_json(req, 200, out);
}

// {"target": "kueche", "command": "OPEN|CLOSE|STOP|SHADE|SETSHADE|LEARN|POSITION", "position": 0..100}
esp_err_t handle_command(httpd_req_t *req) {
  JsonDocument in;
  if (!parse_json_body(req, in)) return ESP_OK;
  std::string target = in["target"] | "";
  std::string command = in["command"] | "";
  auto kind = web->hub->target_kind(target);
  if (kind == JaroliftComponent::TargetKind::NONE)
    return send_errors(req, {{"target", "unknown shutter or group '" + target + "'"}});
  bool group = kind == JaroliftComponent::TargetKind::GROUP;
  TargetCommand cmd;
  if (command == "LEARN") {
    cmd.type = TargetCommand::LEARN;  // only offered here, after the page's confirmation
  } else if (command == "UNLEARN") {
    cmd.type = TargetCommand::UNLEARN;
  } else if (command == "POSITION") {
    int position = in["position"] | -1;
    if (position >= 0 && position <= 100) {
      cmd.type = TargetCommand::POSITION;
      cmd.position = uint8_t(position);
    }
  } else {
    cmd = parse_target_command(command);
    if (cmd.type == TargetCommand::POSITION) cmd.type = TargetCommand::INVALID;
  }
  bool shutter_only = cmd.type == TargetCommand::SETSHADE || cmd.type == TargetCommand::LEARN ||
                      cmd.type == TargetCommand::UNLEARN;
  if (cmd.type == TargetCommand::INVALID || (group && shutter_only))
    return send_errors(req, {{"command", "invalid command for " + target}});
  JaroliftComponent *hub = web->hub;
  hub->run_in_loop([hub, target, cmd]() { hub->command(target, cmd); });
  return send_ok(req);
}

// {"target": "kueche", "which": "open|close", "seconds": 21.4}
esp_err_t handle_duration(httpd_req_t *req) {
  JsonDocument in;
  if (!parse_json_body(req, in)) return ESP_OK;
  std::string target = in["target"] | "";
  std::string which = in["which"] | "";
  float seconds = in["seconds"] | -1.0f;
  std::vector<FieldError> errors;
  number::Number *number = nullptr;
  if (which != "open" && which != "close") {
    errors.push_back({"which", "expected open or close"});
  } else if ((number = web->hub->duration_number(target, which == "open")) == nullptr) {
    errors.push_back({"target", "unknown shutter '" + target + "'"});
  }
  if (!(seconds >= MIN_DURATION_S && seconds <= MAX_DURATION_S))
    errors.push_back({"seconds", "travel time must be between 1 and 300 s"});
  if (!errors.empty()) return send_errors(req, errors);
  web->hub->run_in_loop([number, seconds]() { number->make_call().set_value(seconds).perform(); });
  return send_ok(req);
}

// {"counter": n, "confirm_lower": false}
esp_err_t handle_counter_post(httpd_req_t *req) {
  JsonDocument in;
  if (!parse_json_body(req, in)) return ESP_OK;
  long value = in["counter"].is<long>() ? in["counter"].as<long>() : -1;
  bool confirm_lower = in["confirm_lower"] | false;
  if (value < 0 || value > 0xFFFF) return send_errors(req, {{"counter", "counter must be between 0 and 65535"}});
  JaroliftComponent *hub = web->hub;
  uint16_t current = hub->counters().get();
  if (uint16_t(value) < current && !confirm_lower)
    return send_errors(req, {{"counter", "lowering the counter requires confirmation: motors may stop responding"}});
  uint16_t v = uint16_t(value);
  hub->run_in_loop([hub, v, confirm_lower]() {
    uint16_t before = hub->counters().get();
    if (hub->counters().set(v, confirm_lower) == CounterStore::SetResult::OK) {
      ESP_LOGW(TAG, "Counter set from %u to %u%s", before, v, v < before ? " (lowered)" : "");
    } else {
      ESP_LOGE(TAG, "Setting the counter failed");
    }
  });
  return send_ok(req);
}

void migration_to_json(JsonObject o) {
  MigrationStatus mig = web->hub->migration_status();
  o["active"] = mig.active;
  o["host"] = mig.host;
  o["margin"] = mig.margin;
  o["dongle_reachable"] = mig.dongle_reachable;
  o["dongle_counter"] = mig.dongle_counter;
  o["dongle_seen"] = mig.dongle_seen;
  o["last_check_s_ago"] = mig.active ? millis() / 1000 - mig.last_check_s : 0;
  o["last_error"] = mig.last_error;
  o["counter"] = web->hub->counters().get();
}

// {"host": "192.0.2.20"}: reads the dongle twice; the counters must match (nothing sent in between).
esp_err_t handle_migration_read(httpd_req_t *req) {
  JsonDocument in;
  if (!parse_json_body(req, in)) return ESP_OK;
  std::string host = in["host"] | "";
  uint32_t ip;
  if (!(parse_ipv4(host, &ip) || is_valid_hostname(host)))
    return send_errors(req, {{"host", "enter the dongle's IP address or host name"}});
  DongleConfig first, second;
  std::string error;
  if (!read_dongle(host, true, &first, &error) || !read_dongle(host, false, &second, &error))
    return send_errors(req, {{"host", error}});
  if (first.device_counter != second.device_counter || first.serial_prefix != second.serial_prefix)
    return send_errors(req, {{"host", "the dongle changed between two reads (a command was sent) - read again"}});
  web->dongle_read = true;
  web->dongle_host = host;
  web->dongle = first;
  ESP_LOGI(TAG, "Read dongle %s: prefix 0x%06X, counter %u", host.c_str(), unsigned(first.serial_prefix),
           first.device_counter);
  JsonDocument out;
  out["host"] = host;
  out["master_key_found"] = first.master_msb != 0 || first.master_lsb != 0;
  out["serial_prefix"] = format_hex(first.serial_prefix, 24);
  out["learn_mode_new"] = first.learn_mode_new;
  out["device_counter"] = first.device_counter;
  JsonArray names = out["channel_names"].to<JsonArray>();
  for (const auto &n : first.channel_names) names.add(n);
  return send_json(req, 200, out);
}

// {"margin": 8, "labels": true}
esp_err_t handle_migration_apply(httpd_req_t *req) {
  JsonDocument in;
  if (!parse_json_body(req, in)) return ESP_OK;
  if (!web->dongle_read) return send_errors(req, {{"host", "read the dongle first"}});
  int margin = in["margin"] | 8;
  bool labels = in["labels"] | true;
  if (margin < 1 || margin > 64) return send_errors(req, {{"margin", "margin must be between 1 and 64"}});
  JaroliftComponent *hub = web->hub;
  std::string host = web->dongle_host;
  DongleConfig dongle = web->dongle;
  hub->run_in_loop([hub, host, dongle, margin, labels]() { hub->apply_migration(host, dongle, uint16_t(margin), labels); });
  web->dongle_read = false;
  return send_ok(req);
}

// {"force": false}
esp_err_t handle_migration_release(httpd_req_t *req) {
  JsonDocument in;
  if (!parse_json_body(req, in)) return ESP_OK;
  bool force = in["force"] | false;
  if (!web->hub->release_migration(force))
    return send_errors(req, {{"migration", "the old dongle still answers - switch it off permanently first"}});
  return send_ok(req);
}

// {"certificate": "<PEM chain>", "private_key": "<PEM key>"}
esp_err_t handle_tls_post(httpd_req_t *req) {
  JsonDocument in;
  if (!parse_json_body(req, in)) return ESP_OK;
  std::string cert = in["certificate"] | "";
  std::string key = in["private_key"] | "";
  std::string error;
  if (!web->tls.install(cert, key, &error)) return send_errors(req, {{"certificate", error}});
  JsonDocument out;
  out["ok"] = true;
  certificate_to_json(out["certificate"].to<JsonObject>());
  esp_err_t rc = send_json(req, 200, out);
  // The server cannot restart itself from inside a request; let the main loop do it.
  web->hub->run_in_loop([]() { start_https(); });
  return rc;
}

esp_err_t handle_https(httpd_req_t *req) {
  if (!authorised(req)) return ESP_OK;
  std::string path = path_of(req);
  bool post = req->method == HTTP_POST;

  if (path == "/" && !post) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, reinterpret_cast<const char *>(INDEX_HTML_GZ), INDEX_HTML_GZ_LEN);
  }
  if (path == "/log" && !post) {
    std::string after = query_arg(req, "after");
    std::string body;
    uint32_t last = web->hub->log_buffer().collect(after.empty() ? 0 : uint32_t(strtoul(after.c_str(), nullptr, 10)), &body);
    char last_str[12];
    snprintf(last_str, sizeof(last_str), "%u", unsigned(last));
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "X-Log-Last", last_str);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body.data(), body.size());
  }
  if (path == "/api/status" && !post) return handle_status(req);
  if (path == "/api/settings" && !post) {
    JsonDocument doc;
    settings_to_json(web->hub->settings_copy(), doc.to<JsonObject>());
    return send_json(req, 200, doc);
  }
  if (path == "/api/settings" && post) return handle_settings_post(req);
  if (path == "/api/command" && post) return handle_command(req);
  if (path == "/api/duration" && post) return handle_duration(req);
  if (path == "/api/counter" && !post) {
    JsonDocument doc;
    doc["counter"] = web->hub->counters().get();
    return send_json(req, 200, doc);
  }
  if (path == "/api/counter" && post) return handle_counter_post(req);
  if (path == "/api/migration" && !post) {
    JsonDocument doc;
    migration_to_json(doc.to<JsonObject>());
    return send_json(req, 200, doc);
  }
  if (path == "/api/migration/read" && post) return handle_migration_read(req);
  if (path == "/api/migration/apply" && post) return handle_migration_apply(req);
  if (path == "/api/migration/release" && post) return handle_migration_release(req);
  if (path == "/api/tls" && !post) {
    JsonDocument doc;
    certificate_to_json(doc.to<JsonObject>());
    return send_json(req, 200, doc);
  }
  if (path == "/api/tls" && post) return handle_tls_post(req);
  if (path == "/api/restart" && post) {
    ESP_LOGI(TAG, "Restart requested from the web interface");
    web->hub->run_in_loop([]() { App.safe_reboot(); });
    return send_ok(req);
  }
  JsonDocument doc;
  doc["errors"][0]["field"] = "";
  doc["errors"][0]["message"] = "not found";
  return send_json(req, 404, doc);
}

esp_err_t handle_redirect(httpd_req_t *req) {
  std::string host = header(req, "Host");
  size_t colon = host.find(':');
  if (colon != std::string::npos) host = host.substr(0, colon);
  if (host.empty()) {
    for (auto &addr : network::get_ip_addresses()) {
      if (addr.is_set() && addr.is_ip4()) {
        char ipbuf[network::IP_ADDRESS_BUFFER_SIZE];
        host = addr.str_to(ipbuf);
      }
    }
  }
  std::string location = "https://" + host + req->uri;
  httpd_resp_set_status(req, "301 Moved Permanently");
  httpd_resp_set_hdr(req, "Location", location.c_str());
  return httpd_resp_send(req, nullptr, 0);
}

void register_all(httpd_handle_t server, esp_err_t (*handler)(httpd_req_t *)) {
  for (httpd_method_t method : {HTTP_GET, HTTP_POST, HTTP_HEAD, HTTP_PUT, HTTP_DELETE}) {
    httpd_uri_t uri{};
    uri.uri = "/*";
    uri.method = method;
    uri.handler = handler;
    httpd_register_uri_handler(server, &uri);
  }
}

void start_https() {
  if (web->https != nullptr) {
    httpd_ssl_stop(web->https);
    web->https = nullptr;
  }
  httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
  conf.servercert = reinterpret_cast<const uint8_t *>(web->tls.certificate().c_str());
  conf.servercert_len = web->tls.certificate().size() + 1;
  conf.prvtkey_pem = reinterpret_cast<const uint8_t *>(web->tls.private_key().c_str());
  conf.prvtkey_len = web->tls.private_key().size() + 1;
  conf.httpd.max_open_sockets = 3;
  conf.httpd.lru_purge_enable = true;
  conf.httpd.uri_match_fn = httpd_uri_match_wildcard;
  conf.httpd.max_uri_handlers = 8;
  conf.httpd.stack_size = 10240;
  conf.httpd.ctrl_port = 32768;
  esp_err_t err = httpd_ssl_start(&web->https, &conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTPS server failed to start: %s", esp_err_to_name(err));
    web->https = nullptr;
    return;
  }
  register_all(web->https, handle_https);
  ESP_LOGI(TAG, "HTTPS on port 443 (%s)", web->tls.info().self_signed ? "self-signed" : web->tls.info().issuer.c_str());
}

void start_http_redirect() {
  httpd_config_t conf = HTTPD_DEFAULT_CONFIG();
  conf.server_port = 80;
  conf.ctrl_port = 32769;
  conf.max_open_sockets = 2;
  conf.lru_purge_enable = true;
  conf.uri_match_fn = httpd_uri_match_wildcard;
  conf.max_uri_handlers = 8;
  if (httpd_start(&web->http, &conf) != ESP_OK) {
    ESP_LOGE(TAG, "HTTP redirect server failed to start");
    web->http = nullptr;
    return;
  }
  register_all(web->http, handle_redirect);
}

}  // namespace

void setup_web(JaroliftComponent *hub, const std::string &username, const std::string &password) {
  web = new WebState();  // lives as long as the firmware
  web->hub = hub;
  web->credentials = username + ":" + password;
  if (!web->tls.begin(App.get_name())) {
    ESP_LOGE(TAG, "No usable HTTPS certificate - web interface disabled");
    return;
  }
  start_https();
  start_http_redirect();
}

}  // namespace jarolift
}  // namespace esphome

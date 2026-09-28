#include "core_url.h"

#include <cctype>

namespace esphome {
namespace jarolift {

const char *scheme_name(BrokerScheme scheme) {
  switch (scheme) {
    case BrokerScheme::MQTT:
      return "mqtt";
    case BrokerScheme::MQTTS:
      return "mqtts";
    case BrokerScheme::WS:
      return "ws";
    case BrokerScheme::WSS:
      return "wss";
  }
  return "?";
}

static uint16_t default_port(BrokerScheme scheme) {
  switch (scheme) {
    case BrokerScheme::MQTT:
      return 1883;
    case BrokerScheme::MQTTS:
      return 8883;
    case BrokerScheme::WS:
      return 80;
    case BrokerScheme::WSS:
      return 443;
  }
  return 0;
}

std::string BrokerUrl::to_string() const {
  if (!enabled) return "";
  return std::string(scheme_name(scheme)) + "://" + host + ":" + std::to_string(port) + path;
}

static std::string lower(std::string s) {
  for (auto &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

static std::string trim(const std::string &s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

bool parse_ipv4(const std::string &text, uint32_t *out) {
  uint32_t value = 0;
  int parts = 0;
  size_t i = 0;
  while (parts < 4) {
    if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) return false;
    uint32_t octet = 0;
    size_t start = i;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
      octet = octet * 10 + uint32_t(text[i] - '0');
      if (i - start >= 3 || octet > 255) return false;
      i++;
    }
    if (i - start > 1 && text[start] == '0') return false;  // no leading zeros
    value = (value << 8) | octet;
    parts++;
    if (parts < 4) {
      if (i >= text.size() || text[i] != '.') return false;
      i++;
    }
  }
  if (i != text.size()) return false;
  *out = value;
  return true;
}

bool is_valid_hostname(const std::string &host) {
  // RFC 1123: labels of 1..63 letters, digits and hyphens, no leading or trailing hyphen.
  if (host.empty() || host.size() > 253) return false;
  size_t start = 0;
  bool all_numeric = true;
  while (true) {
    size_t dot = host.find('.', start);
    size_t end = dot == std::string::npos ? host.size() : dot;
    size_t len = end - start;
    if (len == 0 || len > 63) return false;
    if (host[start] == '-' || host[end - 1] == '-') return false;
    for (size_t i = start; i < end; i++) {
      unsigned char c = static_cast<unsigned char>(host[i]);
      if (!std::isalnum(c) && c != '-') return false;
      if (!std::isdigit(c)) all_numeric = false;
    }
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  // All-numeric dotted names are IPv4 attempts; they must parse as such.
  return !all_numeric;
}

bool parse_broker_url(const std::string &input, BrokerUrl *out, std::string *error) {
  BrokerUrl url;
  std::string rest = trim(input);
  if (rest.empty()) {
    *out = url;  // disabled
    return true;
  }

  size_t sep = rest.find("://");
  if (sep != std::string::npos) {
    std::string scheme = lower(rest.substr(0, sep));
    if (scheme == "mqtt") {
      url.scheme = BrokerScheme::MQTT;
    } else if (scheme == "mqtts") {
      url.scheme = BrokerScheme::MQTTS;
    } else if (scheme == "ws") {
      url.scheme = BrokerScheme::WS;
    } else if (scheme == "wss") {
      url.scheme = BrokerScheme::WSS;
    } else {
      *error = "unsupported scheme '" + scheme + "', use mqtt, mqtts, ws or wss";
      return false;
    }
    rest = rest.substr(sep + 3);
  }

  size_t slash = rest.find('/');
  std::string authority = rest.substr(0, slash);
  if (slash != std::string::npos) {
    if (!scheme_is_websocket(url.scheme)) {
      *error = "a path is only allowed for ws and wss";
      return false;
    }
    url.path = rest.substr(slash);
    for (char c : url.path) {
      if (std::isspace(static_cast<unsigned char>(c)) || c == '?' || c == '#') {
        *error = "invalid character in path";
        return false;
      }
    }
  } else if (scheme_is_websocket(url.scheme)) {
    url.path = "/mqtt";
  }

  if (authority.find('@') != std::string::npos) {
    *error = "credentials in the URL are not supported, use the username and password fields";
    return false;
  }
  if (!authority.empty() && authority[0] == '[') {
    *error = "IPv6 addresses are not supported";
    return false;
  }

  size_t colon = authority.find(':');
  url.host = lower(authority.substr(0, colon));
  if (url.host.empty()) {
    *error = "host is missing";
    return false;
  }
  uint32_t ip;
  bool looks_numeric = url.host.find_first_not_of("0123456789.") == std::string::npos;
  if (looks_numeric ? !parse_ipv4(url.host, &ip) : !is_valid_hostname(url.host)) {
    *error = "invalid host '" + url.host + "'";
    return false;
  }

  url.port = default_port(url.scheme);
  if (colon != std::string::npos) {
    std::string port = authority.substr(colon + 1);
    if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos) {
      *error = "invalid port '" + port + "'";
      return false;
    }
    unsigned long value = std::stoul(port);
    if (value < 1 || value > 65535) {
      *error = "port " + port + " is out of range 1..65535";
      return false;
    }
    url.port = uint16_t(value);
  }

  url.enabled = true;
  *out = url;
  return true;
}

}  // namespace jarolift
}  // namespace esphome

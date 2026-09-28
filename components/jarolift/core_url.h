#pragma once

#include <cstdint>
#include <string>

namespace esphome {
namespace jarolift {

enum class BrokerScheme : uint8_t { MQTT, MQTTS, WS, WSS };

const char *scheme_name(BrokerScheme scheme);
inline bool scheme_uses_tls(BrokerScheme s) { return s == BrokerScheme::MQTTS || s == BrokerScheme::WSS; }
inline bool scheme_is_websocket(BrokerScheme s) { return s == BrokerScheme::WS || s == BrokerScheme::WSS; }

struct BrokerUrl {
  bool enabled{false};  // false for an empty URL: no broker connection
  BrokerScheme scheme{BrokerScheme::MQTT};
  std::string host;
  uint16_t port{0};
  std::string path;  // WebSocket only, starts with '/'

  // Normalised form, e.g. "mqtts://mqtt.home.lan:8883" or "wss://host:443/mqtt".
  std::string to_string() const;
};

// Parses `[<scheme>://]<host>[:<port>][/<path>]`. On failure returns false and sets `error`
// to a message that names the problem.
bool parse_broker_url(const std::string &input, BrokerUrl *out, std::string *error);

bool is_valid_hostname(const std::string &host);
bool parse_ipv4(const std::string &text, uint32_t *out);  // out in network order a.b.c.d -> 0xaabbccdd

}  // namespace jarolift
}  // namespace esphome

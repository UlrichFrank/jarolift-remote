#include <doctest/doctest.h>

#include "core_url.h"

using namespace esphome::jarolift;

static BrokerUrl ok(const std::string &in) {
  BrokerUrl url;
  std::string err;
  CAPTURE(in);
  CHECK_MESSAGE(parse_broker_url(in, &url, &err), err);
  return url;
}

static std::string fail(const std::string &in) {
  BrokerUrl url;
  std::string err;
  CAPTURE(in);
  CHECK_FALSE(parse_broker_url(in, &url, &err));
  CHECK_FALSE(err.empty());
  return err;
}

TEST_CASE("plain IP defaults to mqtt on 1883") {
  auto u = ok("192.0.2.10");
  CHECK(u.enabled);
  CHECK(u.scheme == BrokerScheme::MQTT);
  CHECK(u.host == "192.0.2.10");
  CHECK(u.port == 1883);
  CHECK(u.to_string() == "mqtt://192.0.2.10:1883");
}

TEST_CASE("default value from the build") { CHECK(ok("mqtt://192.0.2.10:1883").to_string() == "mqtt://192.0.2.10:1883"); }

TEST_CASE("hostname with TLS uses 8883") {
  auto u = ok("mqtts://mqtt.home.example.org");
  CHECK(u.scheme == BrokerScheme::MQTTS);
  CHECK(u.host == "mqtt.home.example.org");
  CHECK(u.port == 8883);
  CHECK(scheme_uses_tls(u.scheme));
}

TEST_CASE("WebSocket with port and path") {
  auto u = ok("wss://broker.example.org:9443/ws");
  CHECK(u.scheme == BrokerScheme::WSS);
  CHECK(u.port == 9443);
  CHECK(u.path == "/ws");
  CHECK(u.to_string() == "wss://broker.example.org:9443/ws");
}

TEST_CASE("WebSocket defaults") {
  auto ws = ok("ws://broker");
  CHECK(ws.port == 80);
  CHECK(ws.path == "/mqtt");
  CHECK(ok("wss://broker").port == 443);
}

TEST_CASE("scheme and host are case-insensitive, whitespace is trimmed") {
  auto u = ok("  MQTTS://Broker.Home.LAN:8884 ");
  CHECK(u.scheme == BrokerScheme::MQTTS);
  CHECK(u.host == "broker.home.lan");
  CHECK(u.port == 8884);
}

TEST_CASE("single-label hostname") { CHECK(ok("mosquitto").host == "mosquitto"); }

TEST_CASE("empty URL disables the broker") {
  CHECK_FALSE(ok("").enabled);
  CHECK_FALSE(ok("   ").enabled);
  CHECK(ok("").to_string().empty());
}

TEST_CASE("unsupported scheme is named") { CHECK(fail("http://192.0.2.10").find("'http'") != std::string::npos); }

TEST_CASE("path only for WebSocket") {
  CHECK(fail("mqtt://192.0.2.10/mqtt").find("only allowed for ws and wss") != std::string::npos);
  fail("192.0.2.10/mqtt");
}

TEST_CASE("port out of range or malformed") {
  CHECK(fail("mqtt://broker:70000").find("port") != std::string::npos);
  fail("mqtt://broker:0");
  fail("mqtt://broker:");
  fail("mqtt://broker:12a");
}

TEST_CASE("invalid hosts") {
  fail("mqtt://");
  fail("mqtt://:1883");
  fail("mqtt://256.1.1.1");
  fail("mqtt://192.0.2");
  fail("mqtt://192.0.02.1");
  fail("mqtt://-bad.example");
  fail("mqtt://bad_host");
  fail("mqtt://a..b");
  fail("mqtt://[::1]:1883");
  fail("mqtt://user:pw@broker");
}

TEST_CASE("ipv4 parser") {
  uint32_t ip;
  REQUIRE(parse_ipv4("192.0.2.10", &ip));
  CHECK(ip == 0x0A000AF1);
  CHECK_FALSE(parse_ipv4("1.2.3.4.5", &ip));
  CHECK_FALSE(parse_ipv4("1.2.3.", &ip));
  CHECK_FALSE(parse_ipv4("", &ip));
}

#include <doctest/doctest.h>

#include <map>

#include "core_settings.h"

using namespace esphome::jarolift;

struct FakeKv : KeyValueStore {
  std::map<std::string, std::string> data;
  int commits = 0;
  bool get(const std::string &k, std::string *v) override {
    auto it = data.find(k);
    if (it == data.end()) return false;
    *v = it->second;
    return true;
  }
  bool set(const std::string &k, const std::string &v) override {
    data[k] = v;
    return true;
  }
  bool commit() override {
    commits++;
    return true;
  }
};

static Settings defaults() {
  Settings d;
  d.ip = 0x0A000A3C;
  d.subnet = 0xFFFFFF00;
  d.gateway = 0x0A000A01;
  d.dns = 0x0A000A01;
  d.broker_url = "mqtt://192.0.2.10:1883";
  d.client_id = "jarolift-remote-aabbcc";
  d.master_msb = 0x01234567;
  d.master_lsb = 0x89abcdef;
  d.serial_prefix = 0x12deaf;
  return d;
}

TEST_CASE("nothing saved: defaults apply") {
  FakeKv kv;
  SettingsStore store(&kv, defaults());
  CHECK(store.load() == defaults());
}

TEST_CASE("saved fields survive a reload, unsaved fields keep the default") {
  FakeKv kv;
  SettingsStore store(&kv, defaults());
  Settings s = store.load();
  s.broker_url = "mqtts://mqtt.home.lan";
  s.channels[5] = {true, "kind3-fenster", "Kinderzimmer Fenster"};
  REQUIRE(store.save(s));
  CHECK(kv.data.count("broker_url") == 1);
  CHECK(kv.data.count("ip") == 0);

  // A new build with a different default IP: the unsaved IP follows the new default.
  Settings new_defaults = defaults();
  new_defaults.ip = 0x0A000A3D;
  SettingsStore rebooted(&kv, new_defaults);
  Settings r = rebooted.load();
  CHECK(r.broker_url == "mqtts://mqtt.home.lan");
  CHECK(r.channels[5].label == "Kinderzimmer Fenster");
  CHECK(r.ip == 0x0A000A3D);
}

TEST_CASE("every field round-trips") {
  FakeKv kv;
  SettingsStore store(&kv, defaults());
  Settings s = defaults();
  s.dhcp = true;
  s.ip = 1;
  s.subnet = 2;
  s.gateway = 3;
  s.dns = 4;
  s.broker_url = "wss://b:1/x";
  s.mqtt_username = "u";
  s.mqtt_password = "p";
  s.client_id = "c";
  s.ca_certificate = "-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n";
  s.master_msb = 0xFFFFFFFF;
  s.master_lsb = 0;
  s.serial_prefix = 0xABCDEF;
  s.learn_mode_new = false;
  for (int i = 0; i < NUM_CHANNELS; i++) s.channels[i] = {true, "s" + std::to_string(i), "L|" + std::to_string(i)};
  s.groups[0] = {true, "alle", "Alle", 0xFFFF};
  s.groups[7] = {true, "zwei", "", 0x0003};
  REQUIRE(store.save(s));
  CHECK(SettingsStore(&kv, defaults()).load() == s);
}

TEST_CASE("unknown settings version is ignored") {
  FakeKv kv;
  kv.data["version"] = "99";
  kv.data["broker_url"] = "garbage";
  CHECK(SettingsStore(&kv, defaults()).load() == defaults());
}

TEST_CASE("validation") {
  CHECK(validate_settings(defaults()).empty());

  Settings s = defaults();
  s.subnet = 0xFF00FF00;
  s.broker_url = "http://x";
  s.client_id = "";
  s.ca_certificate = "nope";
  s.channels[2] = {true, "x", std::string(41, 'x')};
  auto errors = validate_settings(s);
  std::map<std::string, std::string> byField;
  for (auto &e : errors) byField[e.field] = e.message;
  CHECK(byField.count("subnet"));
  CHECK(byField.count("broker_url"));
  CHECK(byField["broker_url"].find("http") != std::string::npos);
  CHECK(byField.count("client_id"));
  CHECK(byField.count("ca_certificate"));
  CHECK(byField.count("ch2.label"));

  Settings dhcp = defaults();
  dhcp.dhcp = true;
  dhcp.ip = 0;
  dhcp.subnet = 0;
  CHECK(validate_settings(dhcp).empty());

  Settings wrong_gw = defaults();
  wrong_gw.gateway = 0xC0A80001;
  CHECK_FALSE(validate_settings(wrong_gw).empty());
}

#include "core_settings.h"

#include <cstdio>
#include <cstdlib>

#include "core_url.h"
#include "core_validate.h"

namespace esphome {
namespace jarolift {

bool Settings::operator==(const Settings &o) const {
  for (int i = 0; i < NUM_CHANNELS; i++)
    if (channels[i] != o.channels[i]) return false;
  for (int i = 0; i < MAX_GROUPS; i++)
    if (groups[i] != o.groups[i]) return false;
  return dhcp == o.dhcp && ip == o.ip && subnet == o.subnet && gateway == o.gateway && dns == o.dns &&
         broker_url == o.broker_url && mqtt_username == o.mqtt_username && mqtt_password == o.mqtt_password &&
         client_id == o.client_id && ca_certificate == o.ca_certificate && master_msb == o.master_msb &&
         master_lsb == o.master_lsb && serial_prefix == o.serial_prefix && learn_mode_new == o.learn_mode_new;
}

std::string format_ipv4(uint32_t ip) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
  return buf;
}

std::vector<FieldError> validate_settings(const Settings &s) {
  std::vector<FieldError> errors;
  if (!s.dhcp) {
    uint32_t mask;
    if (s.ip == 0) errors.push_back({"ip", "IP address is required without DHCP"});
    if (!parse_netmask(format_ipv4(s.subnet), &mask)) errors.push_back({"subnet", "invalid network mask"});
    if (s.gateway == 0) errors.push_back({"gateway", "gateway is required without DHCP"});
    if (s.dns == 0) errors.push_back({"dns", "DNS server is required without DHCP"});
    if (s.ip != 0 && s.gateway != 0 && (s.ip & s.subnet) != (s.gateway & s.subnet))
      errors.push_back({"gateway", "gateway is not in the IP address's network"});
  }
  BrokerUrl url;
  std::string error;
  if (!parse_broker_url(s.broker_url, &url, &error)) errors.push_back({"broker_url", error});
  if (s.client_id.empty() || s.client_id.size() > SettingsStore::MAX_CLIENT_ID)
    errors.push_back({"client_id", "client ID must be 1 to 64 characters"});
  if (!s.ca_certificate.empty() && !is_valid_pem_certificate(s.ca_certificate))
    errors.push_back({"ca_certificate", "not a PEM certificate"});
  if (s.serial_prefix > 0xFFFFFF) errors.push_back({"serial_prefix", "serial prefix must fit into 24 bits"});
  for (const auto &e : validate_layout(s.channels, s.groups)) errors.push_back({e.field, e.message});
  return errors;
}

// Field codecs: everything is stored as text.
static std::string enc(uint32_t v) { return std::to_string(v); }
static std::string enc(bool v) { return v ? "1" : "0"; }
static void dec(const std::string &s, uint32_t *v) { *v = uint32_t(std::strtoul(s.c_str(), nullptr, 10)); }
static void dec(const std::string &s, bool *v) { *v = s == "1"; }
static void dec(const std::string &s, std::string *v) { *v = s; }
static const std::string &enc(const std::string &v) { return v; }
static std::string enc(const ChannelConfig &v) { return encode_channel(v); }
static void dec(const std::string &s, ChannelConfig *v) { decode_channel(s, v); }
static std::string enc(const GroupConfig &v) { return encode_group(v); }
static void dec(const std::string &s, GroupConfig *v) { decode_group(s, v); }

template<typename F> static void for_each_field(Settings &s, F f) {
  f("dhcp", s.dhcp);
  f("ip", s.ip);
  f("subnet", s.subnet);
  f("gateway", s.gateway);
  f("dns", s.dns);
  f("broker_url", s.broker_url);
  f("mqtt_user", s.mqtt_username);
  f("mqtt_pass", s.mqtt_password);
  f("client_id", s.client_id);
  f("ca_cert", s.ca_certificate);
  f("master_msb", s.master_msb);
  f("master_lsb", s.master_lsb);
  f("serial_prefix", s.serial_prefix);
  f("learn_new", s.learn_mode_new);
  // "sh" keys: the layout format of firmware with typed channel slots ("ch" keys) is not read back.
  for (int i = 0; i < NUM_CHANNELS; i++) f("sh" + std::to_string(i), s.channels[i]);
  for (int i = 0; i < MAX_GROUPS; i++) f("grp" + std::to_string(i), s.groups[i]);
}

Settings SettingsStore::load() {
  Settings s = defaults_;
  std::string version;
  // Unknown future layouts are ignored rather than misread.
  if (kv_->get("version", &version) && version != std::to_string(VERSION)) return s;
  for_each_field(s, [&](const std::string &key, auto &field) {
    std::string raw;
    if (kv_->get(key, &raw)) dec(raw, &field);
  });
  return s;
}

bool SettingsStore::save(const Settings &settings) {
  Settings current = load();
  Settings wanted = settings;
  bool ok = kv_->set("version", std::to_string(VERSION));
  // Walk both structs in lockstep; write only changed fields so untouched ones keep their default.
  std::vector<std::pair<std::string, std::string>> want_values;
  for_each_field(wanted, [&](const std::string &key, auto &field) { want_values.emplace_back(key, enc(field)); });
  size_t i = 0;
  for_each_field(current, [&](const std::string &key, auto &field) {
    const auto &w = want_values[i++];
    if (w.second != enc(field)) ok = kv_->set(key, w.second) && ok;
  });
  return kv_->commit() && ok;
}

}  // namespace jarolift
}  // namespace esphome

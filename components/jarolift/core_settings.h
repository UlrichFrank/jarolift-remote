#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core_channels.h"
#include "core_frame.h"

namespace esphome {
namespace jarolift {

// Key/value backend (an NVS namespace on the device, a map in host tests).
class KeyValueStore {
 public:
  virtual ~KeyValueStore() = default;
  virtual bool get(const std::string &key, std::string *value) = 0;
  virtual bool set(const std::string &key, const std::string &value) = 0;
  virtual bool commit() = 0;
};

// Settings editable in the web interface. Build-time values serve as defaults.
struct Settings {
  // Network
  bool dhcp{false};
  uint32_t ip{0}, subnet{0}, gateway{0}, dns{0};
  // Broker
  std::string broker_url;
  std::string mqtt_username;
  std::string mqtt_password;
  std::string client_id;
  std::string ca_certificate;
  // Jarolift identity
  uint32_t master_msb{0}, master_lsb{0};
  uint32_t serial_prefix{0};
  bool learn_mode_new{true};
  // Layout: sixteen shutter channels and up to eight groups (channel bitmasks)
  Channels channels;
  Groups groups;

  bool operator==(const Settings &o) const;
  bool operator!=(const Settings &o) const { return !(*this == o); }
};

struct FieldError {
  std::string field;
  std::string message;
};

// Checks every field; returns an empty list if the settings may be saved.
std::vector<FieldError> validate_settings(const Settings &s);

std::string format_ipv4(uint32_t ip);

class SettingsStore {
 public:
  static constexpr int VERSION = 1;
  static constexpr size_t MAX_CLIENT_ID = 64;

  SettingsStore(KeyValueStore *kv, const Settings &defaults) : kv_(kv), defaults_(defaults) {}

  // Effective settings: saved values where present, defaults otherwise.
  Settings load();

  // Persists the fields that differ from the currently effective settings.
  bool save(const Settings &settings);

 protected:
  KeyValueStore *kv_;
  Settings defaults_;
};

}  // namespace jarolift
}  // namespace esphome

#pragma once

#include <cstdint>
#include <string>

#include "core_frame.h"

namespace esphome {
namespace jarolift {

// Identity and counter of the old ESP8266 dongle (madmartin/Jarolift_MQTT), as read from its web
// API: `POST /api cmd=get config` and `cmd=get channel name`.
struct DongleConfig {
  uint32_t master_msb{0}, master_lsb{0};
  uint32_t serial_prefix{0};
  bool learn_mode_new{true};
  uint16_t device_counter{0};
  std::string channel_names[NUM_CHANNELS];
};

// Parses the `get config` answer (key=value lines, `checkbox=<name>=0|1`). Returns false and names
// the problem if master key, serial prefix or device counter are missing or malformed. WiFi
// credentials in the answer are ignored.
bool parse_dongle_config(const std::string &text, DongleConfig *out, std::string *error);

// Parses the `get channel name` answer (`channel_<n>=<name>` lines).
void parse_dongle_channel_names(const std::string &text, DongleConfig *out);

}  // namespace jarolift
}  // namespace esphome

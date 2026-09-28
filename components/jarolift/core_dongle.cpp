#include "core_dongle.h"

#include <cstdlib>

#include "core_validate.h"

namespace esphome {
namespace jarolift {

template<typename F> static void for_each_line(const std::string &text, F f) {
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t eq = line.find('=');
    if (eq != std::string::npos) f(line.substr(0, eq), line.substr(eq + 1));
    start = end + 1;
  }
}

bool parse_dongle_config(const std::string &text, DongleConfig *out, std::string *error) {
  DongleConfig cfg = *out;
  bool have_msb = false, have_lsb = false, have_serial = false, have_counter = false;
  for_each_line(text, [&](const std::string &key, const std::string &value) {
    if (key == "master_msb") {
      have_msb = parse_hex(value, 32, &cfg.master_msb);
    } else if (key == "master_lsb") {
      have_lsb = parse_hex(value, 32, &cfg.master_lsb);
    } else if (key == "serial") {
      have_serial = parse_hex(value, 24, &cfg.serial_prefix);
    } else if (key == "devicecounter") {
      char *end = nullptr;
      unsigned long v = std::strtoul(value.c_str(), &end, 10);
      have_counter = !value.empty() && end && *end == '\0' && v <= 0xFFFF;
      cfg.device_counter = uint16_t(v);
    } else if (key == "checkbox" && value.compare(0, 11, "learn_mode=") == 0) {
      cfg.learn_mode_new = value.substr(11) == "1";
    }
  });
  if (!have_msb || !have_lsb) {
    *error = "master key missing or not a 32-bit hex number";
    return false;
  }
  if (!have_serial) {
    *error = "serial prefix missing or not a 24-bit hex number";
    return false;
  }
  if (!have_counter) {
    *error = "device counter missing or out of range";
    return false;
  }
  *out = cfg;
  return true;
}

void parse_dongle_channel_names(const std::string &text, DongleConfig *out) {
  for_each_line(text, [&](const std::string &key, const std::string &value) {
    if (key.compare(0, 8, "channel_") != 0) return;
    int ch = std::atoi(key.c_str() + 8);
    if (ch >= 0 && ch < NUM_CHANNELS) out->channel_names[ch] = value;
  });
}

}  // namespace jarolift
}  // namespace esphome

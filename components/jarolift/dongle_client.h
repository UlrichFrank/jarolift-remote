#pragma once

#ifdef USE_ESP32

#include <string>

#include "core_dongle.h"

namespace esphome {
namespace jarolift {

// Reads the old dongle's configuration over its web API (`POST http://<host>/api`).
// Blocking; call it from a task that may wait a few seconds, never from the main loop.
// `names` = false skips the channel names (used by the counter tracker).
bool read_dongle(const std::string &host, bool names, DongleConfig *out, std::string *error);

}  // namespace jarolift
}  // namespace esphome

#endif

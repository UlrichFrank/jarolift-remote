#pragma once

#include "core_frame.h"

namespace esphome {
namespace jarolift {

// The 433 MHz radio. The CC1101 driver (SPI setup, RMT transmit on GPIO 4, receive on GPIO 36)
// is tasks 3.1/3.4 and needs the module attached; until then the radio reports itself absent
// and the hub drops frames without consuming counter values.
class Radio {
 public:
  bool setup();
  bool present() const { return present_; }
  bool busy() const { return false; }
  // Starts sending the telegram `repetitions` times. Only valid while present() and !busy().
  bool transmit(const Frame &frame, int repetitions);
  bool poll_received(RawTelegram *out) { return false; }

 protected:
  bool present_{false};
};

}  // namespace jarolift
}  // namespace esphome

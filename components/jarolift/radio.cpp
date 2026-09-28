#include "radio.h"

#include "esphome/core/log.h"

namespace esphome {
namespace jarolift {

static const char *const TAG = "jarolift.radio";

bool Radio::setup() {
  ESP_LOGW(TAG, "CC1101 driver not built yet - frames are not sent");
  present_ = false;
  return false;
}

bool Radio::transmit(const Frame &, int) { return false; }

}  // namespace jarolift
}  // namespace esphome

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "esphome/core/helpers.h"

namespace esphome {
namespace jarolift {

// Recent log lines for the web interface's Log page. Filled from the logger callback.
class LogBuffer {
 public:
  static constexpr size_t LINES = 80;
  static constexpr size_t MAX_LINE = 240;

  void add(const char *message, size_t len);
  // Lines with a sequence number greater than `after`, oldest first; returns the last number.
  uint32_t collect(uint32_t after, std::string *out);

 protected:
  Mutex mutex_;
  std::vector<std::string> lines_{LINES};
  uint32_t next_seq_{1};
};

}  // namespace jarolift
}  // namespace esphome

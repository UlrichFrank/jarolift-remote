#include "log_buffer.h"

namespace esphome {
namespace jarolift {

void LogBuffer::add(const char *message, size_t len) {
  std::string line;
  line.reserve(len < MAX_LINE ? len : MAX_LINE);
  // Strip ANSI colour sequences.
  for (size_t i = 0; i < len && line.size() < MAX_LINE; i++) {
    if (message[i] == '\033') {
      while (i < len && message[i] != 'm') i++;
      continue;
    }
    if (message[i] != '\r' && message[i] != '\n') line.push_back(message[i]);
  }
  LockGuard lock(mutex_);
  lines_[next_seq_ % LINES] = std::move(line);
  next_seq_++;
}

uint32_t LogBuffer::collect(uint32_t after, std::string *out) {
  LockGuard lock(mutex_);
  uint32_t last = next_seq_ - 1;
  uint32_t first = last >= LINES ? last - LINES + 1 : 1;
  if (after + 1 > first) first = after + 1;
  for (uint32_t seq = first; seq <= last; seq++) {
    out->append(lines_[seq % LINES]);
    out->push_back('\n');
  }
  return last;
}

}  // namespace jarolift
}  // namespace esphome

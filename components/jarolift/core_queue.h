#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "core_frame.h"

namespace esphome {
namespace jarolift {

enum class Command : uint8_t { UP, DOWN, STOP, SHADE, SETSHADE, LEARN, UNLEARN };

const char *command_name(Command command);

// One telegram of a command sequence. Each step consumes one counter value.
struct Step {
  uint8_t button;
  uint8_t repetitions;
  uint32_t pause_after_ms;
};

// Telegram sequence of a command, as the old dongle's cmd_*() functions send it.
std::vector<Step> command_steps(Command command, bool learn_mode_new);

// A telegram target: one channel (group_mask == 0), or a group addressed by the channel bitmask of
// its members, sent with the serial of channel 0 (as dewenni's JaroliftController does).
struct Request {
  uint8_t channel;
  Command command;
  uint16_t group_mask{0};

  bool is_group() const { return group_mask != 0; }
  uint16_t mask() const { return is_group() ? group_mask : uint16_t(1u << channel); }
  bool same_target(const Request &o) const {
    return is_group() == o.is_group() && (is_group() ? group_mask == o.group_mask : channel == o.channel);
  }
};

// Transmit queue for the single radio. STOP for a target drops the queued drive commands
// (UP, DOWN, SHADE) of that target and goes ahead of everything else still queued.
class TxQueue {
 public:
  void push(const Request &request);
  bool pop(Request *out);
  bool empty() const { return queue_.empty(); }
  size_t size() const { return queue_.size(); }

 protected:
  std::deque<Request> queue_;
};

}  // namespace jarolift
}  // namespace esphome

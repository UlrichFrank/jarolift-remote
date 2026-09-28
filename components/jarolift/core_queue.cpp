#include "core_queue.h"

#include <algorithm>

namespace esphome {
namespace jarolift {

const char *command_name(Command command) {
  switch (command) {
    case Command::UP:
      return "UP";
    case Command::DOWN:
      return "DOWN";
    case Command::STOP:
      return "STOP";
    case Command::SHADE:
      return "SHADE";
    case Command::SETSHADE:
      return "SETSHADE";
    case Command::LEARN:
      return "LEARN";
    case Command::UNLEARN:
      return "UNLEARN";
  }
  return "?";
}

std::vector<Step> command_steps(Command command, bool learn_mode_new) {
  switch (command) {
    case Command::UP:
      return {{BUTTON_UP, 2, 0}};
    case Command::DOWN:
      return {{BUTTON_DOWN, 2, 0}};
    case Command::STOP:
      return {{BUTTON_STOP, 2, 0}};
    case Command::SHADE:
      // A long STOP press: the motor drives to its taught shade position.
      return {{BUTTON_STOP, 20, 0}};
    case Command::SETSHADE:
      // Four short STOPs, then a safety pause so the end points are not erased by accident.
      return {{BUTTON_STOP, 1, 300}, {BUTTON_STOP, 1, 300}, {BUTTON_STOP, 1, 300}, {BUTTON_STOP, 1, 2300}};
    case Command::LEARN:
      if (learn_mode_new) return {{BUTTON_UP_DOWN, 1, 1000}, {BUTTON_STOP, 1, 0}};
      return {{BUTTON_LEARN_OLD, 1, 0}};
    case Command::UNLEARN:
      // dewenni's cmdUnlearn: UP+DOWN, six STOPs, UP - the motor forgets this channel's serial.
      return {{BUTTON_UP_DOWN, 2, 300}, {BUTTON_STOP, 2, 300}, {BUTTON_STOP, 2, 300}, {BUTTON_STOP, 2, 300},
              {BUTTON_STOP, 2, 300},    {BUTTON_STOP, 2, 300}, {BUTTON_STOP, 2, 300}, {BUTTON_UP, 2, 0}};
  }
  return {};
}

static bool is_drive(Command command) {
  return command == Command::UP || command == Command::DOWN || command == Command::SHADE;
}

void TxQueue::push(const Request &request) {
  if (request.command != Command::STOP) {
    queue_.push_back(request);
    return;
  }
  queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                              [&](const Request &r) { return r.same_target(request) && is_drive(r.command); }),
               queue_.end());
  // Ahead of every queued non-STOP request, behind STOPs that are already waiting.
  auto pos = std::find_if(queue_.begin(), queue_.end(), [](const Request &r) { return r.command != Command::STOP; });
  bool duplicate = std::any_of(queue_.begin(), pos, [&](const Request &r) { return r.same_target(request); });
  if (!duplicate) queue_.insert(pos, request);
}

bool TxQueue::pop(Request *out) {
  if (queue_.empty()) return false;
  *out = queue_.front();
  queue_.pop_front();
  return true;
}

}  // namespace jarolift
}  // namespace esphome

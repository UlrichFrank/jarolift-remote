#pragma once

#include <cstdint>
#include <string>

namespace esphome {
namespace jarolift {

// A command payload from `jarolift/<name>/set` or the web interface.
struct TargetCommand {
  enum Type : uint8_t { INVALID, OPEN, CLOSE, STOP, SHADE, SETSHADE, LEARN, UNLEARN, POSITION } type{INVALID};
  uint8_t position{0};  // 0..100 for POSITION, 100 = open
};

const char *target_command_name(TargetCommand::Type type);

// Keywords OPEN, CLOSE, STOP, SHADE, SETSHADE (case-insensitive) or an integer 0..100.
// LEARN and UNLEARN are not accepted here: they are only offered in the web interface, after confirmation.
TargetCommand parse_target_command(const std::string &payload);

// Travel time in seconds: a positive decimal number within [min_s, max_s].
bool parse_duration(const std::string &payload, float min_s, float max_s, float *out);

}  // namespace jarolift
}  // namespace esphome

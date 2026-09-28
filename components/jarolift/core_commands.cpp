#include "core_commands.h"

#include <cctype>
#include <cstdlib>

namespace esphome {
namespace jarolift {

const char *target_command_name(TargetCommand::Type type) {
  switch (type) {
    case TargetCommand::OPEN:
      return "OPEN";
    case TargetCommand::CLOSE:
      return "CLOSE";
    case TargetCommand::STOP:
      return "STOP";
    case TargetCommand::SHADE:
      return "SHADE";
    case TargetCommand::SETSHADE:
      return "SETSHADE";
    case TargetCommand::LEARN:
      return "LEARN";
    case TargetCommand::UNLEARN:
      return "UNLEARN";
    case TargetCommand::POSITION:
      return "POSITION";
    case TargetCommand::INVALID:
      break;
  }
  return "INVALID";
}

static std::string normalise(const std::string &payload) {
  size_t b = payload.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = payload.find_last_not_of(" \t\r\n");
  std::string s = payload.substr(b, e - b + 1);
  for (auto &c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

TargetCommand parse_target_command(const std::string &payload) {
  std::string s = normalise(payload);
  TargetCommand cmd;
  if (s == "OPEN") {
    cmd.type = TargetCommand::OPEN;
  } else if (s == "CLOSE") {
    cmd.type = TargetCommand::CLOSE;
  } else if (s == "STOP") {
    cmd.type = TargetCommand::STOP;
  } else if (s == "SHADE") {
    cmd.type = TargetCommand::SHADE;
  } else if (s == "SETSHADE") {
    cmd.type = TargetCommand::SETSHADE;
  } else if (!s.empty() && s.size() <= 3 && s.find_first_not_of("0123456789") == std::string::npos) {
    int value = std::atoi(s.c_str());
    if (value <= 100) {
      cmd.type = TargetCommand::POSITION;
      cmd.position = uint8_t(value);
    }
  }
  return cmd;
}

bool parse_duration(const std::string &payload, float min_s, float max_s, float *out) {
  std::string s = normalise(payload);
  if (s.empty() || s.size() > 8) return false;
  bool dot = false;
  for (char c : s) {
    if (c == '.') {
      if (dot) return false;
      dot = true;
    } else if (!std::isdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  if (s == ".") return false;
  float value = std::strtof(s.c_str(), nullptr);
  if (value < min_s || value > max_s) return false;
  *out = value;
  return true;
}

}  // namespace jarolift
}  // namespace esphome

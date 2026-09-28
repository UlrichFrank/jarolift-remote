#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core_frame.h"

namespace esphome {
namespace jarolift {

static constexpr uint8_t MAX_GROUPS = 8;
static constexpr size_t MAX_CHANNEL_NAME = 32;
static constexpr size_t MAX_CHANNEL_LABEL = 40;

// One of the sixteen shutter channels.
struct ChannelConfig {
  bool enabled{false};
  std::string name;   // MQTT topic level, kebab-case
  std::string label;  // display only

  bool operator==(const ChannelConfig &o) const {
    return enabled == o.enabled && name == o.name && label == o.label;
  }
  bool operator!=(const ChannelConfig &o) const { return !(*this == o); }
};

// A group: shutters addressed together by one telegram whose channel bitmask holds all members.
struct GroupConfig {
  bool enabled{false};
  std::string name;
  std::string label;
  uint16_t members{0};  // bit n = channel n

  bool operator==(const GroupConfig &o) const {
    return enabled == o.enabled && name == o.name && label == o.label && members == o.members;
  }
  bool operator!=(const GroupConfig &o) const { return !(*this == o); }
};

struct LayoutError {
  std::string field;  // "ch3.name", "grp1.members", ...
  std::string message;
};

using Channels = ChannelConfig[NUM_CHANNELS];
using Groups = GroupConfig[MAX_GROUPS];

bool is_valid_topic_name(const std::string &name);

// Topic name from a display label, like the web page does: "OG Eltern" -> "og-eltern",
// German umlauts transliterated (UTF-8), everything else non-alphanumeric becomes '-'.
std::string topic_from_label(const std::string &label);

// Checks channels and groups together; returns an empty list if the layout may be saved.
std::vector<LayoutError> validate_layout(const Channels &channels, const Groups &groups);

// Names whose retained state topics must be cleared when switching from `before` to `after`:
// shutters that are disabled or renamed.
std::vector<std::string> stale_shutter_names(const Channels &before, const Channels &after);

// Storage forms: channel "enabled|name|label", group "enabled|members|name|label"
// (label last, so it may contain '|').
std::string encode_channel(const ChannelConfig &c);
bool decode_channel(const std::string &text, ChannelConfig *out);
std::string encode_group(const GroupConfig &g);
bool decode_group(const std::string &text, GroupConfig *out);

}  // namespace jarolift
}  // namespace esphome

#include "core_channels.h"

#include <cstdlib>

namespace esphome {
namespace jarolift {

bool is_valid_topic_name(const std::string &name) {
  if (name.empty() || name.size() > MAX_CHANNEL_NAME || name == "bridge") return false;
  if (name.front() == '-' || name.back() == '-') return false;
  char prev = 0;
  for (char c : name) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok || (c == '-' && prev == '-')) return false;
    prev = c;
  }
  return true;
}

std::string topic_from_label(const std::string &label) {
  std::string out;
  auto push = [&](const char *s) {
    for (; *s; s++) out.push_back(*s);
  };
  for (size_t i = 0; i < label.size(); i++) {
    unsigned char c = static_cast<unsigned char>(label[i]);
    if (c == 0xC3 && i + 1 < label.size()) {
      unsigned char d = static_cast<unsigned char>(label[++i]);
      if (d == 0xA4 || d == 0x84) {
        push("ae");
      } else if (d == 0xB6 || d == 0x96) {
        push("oe");
      } else if (d == 0xBC || d == 0x9C) {
        push("ue");
      } else if (d == 0x9F) {
        push("ss");
      } else {
        out.push_back('-');
      }
    } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out.push_back(char(c));
    } else if (c >= 'A' && c <= 'Z') {
      out.push_back(char(c - 'A' + 'a'));
    } else {
      out.push_back('-');
    }
  }
  std::string collapsed;
  for (char c : out)
    if (c != '-' || (!collapsed.empty() && collapsed.back() != '-')) collapsed.push_back(c);
  while (!collapsed.empty() && collapsed.back() == '-') collapsed.pop_back();
  if (collapsed.size() > MAX_CHANNEL_NAME) collapsed.resize(MAX_CHANNEL_NAME);
  while (!collapsed.empty() && collapsed.back() == '-') collapsed.pop_back();
  return collapsed;
}

std::vector<LayoutError> validate_layout(const Channels &channels, const Groups &groups) {
  std::vector<LayoutError> errors;
  std::vector<std::pair<std::string, std::string>> used;  // name, owner
  auto check_name = [&](const std::string &field, const std::string &owner, const std::string &name) {
    if (!is_valid_topic_name(name)) {
      errors.push_back({field, "name must be kebab-case (a-z, 0-9, '-'), 1 to 32 characters, not 'bridge'"});
      return;
    }
    for (const auto &u : used) {
      if (u.first == name) {
        errors.push_back({field, "name '" + name + "' is already used by " + u.second});
        return;
      }
    }
    used.emplace_back(name, owner);
  };

  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    const auto &c = channels[i];
    std::string key = "ch" + std::to_string(i) + ".";
    if (c.label.size() > MAX_CHANNEL_LABEL) errors.push_back({key + "label", "label is longer than 40 characters"});
    // Owners are named as the web page numbers channels and groups: from 1.
    if (c.enabled) check_name(key + "name", "the shutter on channel " + std::to_string(i + 1), c.name);
  }
  for (uint8_t g = 0; g < MAX_GROUPS; g++) {
    const auto &grp = groups[g];
    std::string key = "grp" + std::to_string(g) + ".";
    if (grp.label.size() > MAX_CHANNEL_LABEL) errors.push_back({key + "label", "label is longer than 40 characters"});
    if (!grp.enabled) continue;
    check_name(key + "name", "group " + std::to_string(g + 1), grp.name);
    std::string group_name = grp.name.empty() ? "group " + std::to_string(g + 1) : "'" + grp.name + "'";
    if (grp.members == 0) errors.push_back({key + "members", "a group needs at least one member"});
    for (uint8_t m = 0; m < NUM_CHANNELS; m++) {
      if ((grp.members & (1u << m)) && !channels[m].enabled) {
        std::string member = channels[m].name.empty() ? "channel " + std::to_string(m + 1) : "'" + channels[m].name + "'";
        errors.push_back({key + "members", group_name + ": member " + member + " is not an active shutter"});
      }
    }
  }
  return errors;
}

std::vector<std::string> stale_shutter_names(const Channels &before, const Channels &after) {
  std::vector<std::string> names;
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    if (!before[i].enabled) continue;
    bool still_published = false;
    for (uint8_t j = 0; j < NUM_CHANNELS; j++)
      still_published = still_published || (after[j].enabled && after[j].name == before[i].name);
    if (!still_published) names.push_back(before[i].name);
  }
  return names;
}

std::string encode_channel(const ChannelConfig &c) {
  return std::string(c.enabled ? "1" : "0") + "|" + c.name + "|" + c.label;
}

bool decode_channel(const std::string &text, ChannelConfig *out) {
  size_t a = text.find('|');
  if (a != 1 || (text[0] != '0' && text[0] != '1')) return false;
  size_t b = text.find('|', a + 1);
  if (b == std::string::npos) return false;
  out->enabled = text[0] == '1';
  out->name = text.substr(a + 1, b - a - 1);
  out->label = text.substr(b + 1);
  return true;
}

std::string encode_group(const GroupConfig &g) {
  return std::string(g.enabled ? "1" : "0") + "|" + std::to_string(g.members) + "|" + g.name + "|" + g.label;
}

bool decode_group(const std::string &text, GroupConfig *out) {
  size_t a = text.find('|');
  if (a != 1 || (text[0] != '0' && text[0] != '1')) return false;
  size_t b = text.find('|', a + 1);
  if (b == std::string::npos) return false;
  size_t c = text.find('|', b + 1);
  if (c == std::string::npos) return false;
  out->enabled = text[0] == '1';
  out->members = uint16_t(std::strtoul(text.substr(a + 1, b - a - 1).c_str(), nullptr, 10));
  out->name = text.substr(b + 1, c - b - 1);
  out->label = text.substr(c + 1);
  return true;
}

}  // namespace jarolift
}  // namespace esphome

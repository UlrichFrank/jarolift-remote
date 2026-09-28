#include <doctest/doctest.h>

#include <algorithm>
#include <map>

#include "core_channels.h"

using namespace esphome::jarolift;

static void house(Channels &c, Groups &g) {
  const char *names[] = {"kueche", "terrasse", "wohnzimmer-links", "wohnzimmer-rechts", "kind3-balkontuer",
                         "kind3-fenster", "kind4-fenster", "eltern-links", "eltern-rechts"};
  for (int i = 0; i < NUM_CHANNELS; i++) c[i] = {};
  for (int i = 0; i < 9; i++) c[i] = {true, names[i], ""};
  for (int i = 0; i < MAX_GROUPS; i++) g[i] = {};
  g[0] = {true, "alle", "Alle", 0x1FF};
  g[1] = {true, "eg", "EG", 0x00F};
  g[2] = {true, "og", "OG", 0x1F0};
  g[3] = {true, "og-eltern", "", 0x180};
  g[4] = {true, "og-kinder", "", 0x070};
}

static std::map<std::string, std::string> errors_of(const Channels &c, const Groups &g) {
  std::map<std::string, std::string> m;
  for (auto &e : validate_layout(c, g)) m[e.field] = e.message;
  return m;
}

TEST_CASE("the house layout is valid") {
  Channels c;
  Groups g;
  house(c, g);
  CHECK(validate_layout(c, g).empty());
}

TEST_CASE("topic names") {
  CHECK(is_valid_topic_name("kueche"));
  CHECK(is_valid_topic_name("og-eltern"));
  CHECK_FALSE(is_valid_topic_name(""));
  CHECK_FALSE(is_valid_topic_name("Küche"));
  CHECK_FALSE(is_valid_topic_name("a b"));
  CHECK_FALSE(is_valid_topic_name("a/b"));
  CHECK_FALSE(is_valid_topic_name("-a"));
  CHECK_FALSE(is_valid_topic_name("a--b"));
  CHECK_FALSE(is_valid_topic_name("bridge"));
  CHECK_FALSE(is_valid_topic_name(std::string(33, 'a')));
}

TEST_CASE("add a shutter and a group") {
  Channels c;
  Groups g;
  house(c, g);
  c[14] = {true, "gaeste-fenster", "Gäste"};
  g[5] = {true, "sued", "Süd", (1u << 1) | (1u << 2) | (1u << 14)};
  CHECK(validate_layout(c, g).empty());
}

TEST_CASE("names are unique across shutters and groups") {
  Channels c;
  Groups g;
  house(c, g);
  c[14] = {true, "alle", ""};
  g[5] = {true, "kueche", "", 1};
  auto e = errors_of(c, g);
  CHECK(e.count("grp0.name"));  // 'alle' taken by channel 14 first
  CHECK(e.count("grp5.name"));
}

TEST_CASE("invalid groups") {
  Channels c;
  Groups g;
  house(c, g);
  g[5] = {true, "leer", "", 0};
  g[6] = {true, "frei", "", 1u << 12};  // channel 12 is not an active shutter
  g[7] = {true, "Süd", "", 1};
  auto e = errors_of(c, g);
  CHECK(e.count("grp5.members"));
  CHECK(e.count("grp6.members"));
  CHECK(e.count("grp7.name"));
}

TEST_CASE("disabling a member invalidates its groups") {
  Channels c;
  Groups g;
  house(c, g);
  c[7].enabled = false;
  auto e = errors_of(c, g);
  CHECK(e.count("grp0.members"));
  CHECK(e.count("grp3.members"));
}

TEST_CASE("disabled entries may keep any name") {
  Channels c;
  Groups g;
  house(c, g);
  c[14] = {false, "kueche", ""};
  g[6] = {false, "", "", 0};
  CHECK(validate_layout(c, g).empty());
}

TEST_CASE("stale names on rename and disable") {
  Channels before, after;
  Groups g;
  house(before, g);
  house(after, g);
  after[5].name = "gaeste-fenster";
  after[6].enabled = false;
  auto stale = stale_shutter_names(before, after);
  std::sort(stale.begin(), stale.end());
  CHECK(stale == std::vector<std::string>{"kind3-fenster", "kind4-fenster"});
}

TEST_CASE("encodings round-trip, labels may contain the separator") {
  ChannelConfig c{true, "kueche", "EG | Küche"}, c2;
  REQUIRE(decode_channel(encode_channel(c), &c2));
  CHECK(c2 == c);
  GroupConfig g{true, "og-kinder", "Kinder | OG", 0x070}, g2;
  REQUIRE(decode_group(encode_group(g), &g2));
  CHECK(g2 == g);
  CHECK_FALSE(decode_channel("garbage", &c2));
  CHECK_FALSE(decode_group("2|1|x|", &g2));
}

TEST_CASE("topic from label") {
  CHECK(topic_from_label("OG Eltern") == "og-eltern");
  CHECK(topic_from_label("EG Küche Fenster") == "eg-kueche-fenster");
  CHECK(topic_from_label("Alle") == "alle");
  CHECK(topic_from_label("  Größe / Öffnung!! ") == "groesse-oeffnung");
  CHECK(topic_from_label("ÄÖÜ") == "aeoeue");
  CHECK(topic_from_label("---") == "");
  CHECK(topic_from_label(std::string(40, 'x')).size() == MAX_CHANNEL_NAME);
}

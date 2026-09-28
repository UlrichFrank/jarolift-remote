#include <doctest/doctest.h>

#include "core_commands.h"

using namespace esphome::jarolift;

TEST_CASE("keywords are case-insensitive") {
  CHECK(parse_target_command("OPEN").type == TargetCommand::OPEN);
  CHECK(parse_target_command("close").type == TargetCommand::CLOSE);
  CHECK(parse_target_command(" Stop\n").type == TargetCommand::STOP);
  CHECK(parse_target_command("shade").type == TargetCommand::SHADE);
  CHECK(parse_target_command("SetShade").type == TargetCommand::SETSHADE);
}

TEST_CASE("positions 0..100") {
  auto c = parse_target_command("30");
  CHECK(c.type == TargetCommand::POSITION);
  CHECK(c.position == 30);
  CHECK(parse_target_command("0").position == 0);
  CHECK(parse_target_command("100").position == 100);
  CHECK(parse_target_command("100").type == TargetCommand::POSITION);
}

TEST_CASE("invalid payloads") {
  for (const char *p : {"half", "150", "-1", "", "1000", "30.5", "UP", "DOWN", "LEARN", "OPEN CLOSE", "1e2"}) {
    CAPTURE(p);
    CHECK(parse_target_command(p).type == TargetCommand::INVALID);
  }
}

TEST_CASE("durations") {
  float d;
  CHECK(parse_duration("21.4", 1, 300, &d));
  CHECK(d == doctest::Approx(21.4));
  CHECK(parse_duration("20", 1, 300, &d));
  CHECK_FALSE(parse_duration("0", 1, 300, &d));
  CHECK_FALSE(parse_duration("-5", 1, 300, &d));
  CHECK_FALSE(parse_duration("301", 1, 300, &d));
  CHECK_FALSE(parse_duration("abc", 1, 300, &d));
  CHECK_FALSE(parse_duration("1.2.3", 1, 300, &d));
  CHECK_FALSE(parse_duration("", 1, 300, &d));
  CHECK_FALSE(parse_duration(".", 1, 300, &d));
}

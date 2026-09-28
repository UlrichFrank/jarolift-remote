#include <doctest/doctest.h>

#include <vector>

#include "core_queue.h"

using namespace esphome::jarolift;

static std::vector<Request> drain(TxQueue &q) {
  std::vector<Request> out;
  Request r;
  while (q.pop(&r)) out.push_back(r);
  return out;
}

TEST_CASE("requests keep their order") {
  TxQueue q;
  q.push({4, Command::UP});
  q.push({5, Command::UP});
  q.push({6, Command::UP});
  auto out = drain(q);
  REQUIRE(out.size() == 3);
  CHECK(out[0].channel == 4);
  CHECK(out[1].channel == 5);
  CHECK(out[2].channel == 6);
}

TEST_CASE("STOP overtakes and cancels queued drive commands of its channel") {
  TxQueue q;
  q.push({1, Command::UP});
  q.push({7, Command::DOWN});
  q.push({2, Command::SHADE});
  q.push({7, Command::STOP});
  auto out = drain(q);
  REQUIRE(out.size() == 3);
  CHECK(out[0].channel == 7);
  CHECK(out[0].command == Command::STOP);
  CHECK(out[1].channel == 1);
  CHECK(out[2].channel == 2);
}

TEST_CASE("STOPs keep their order among themselves and are not duplicated") {
  TxQueue q;
  q.push({1, Command::UP});
  q.push({3, Command::STOP});
  q.push({4, Command::STOP});
  q.push({3, Command::STOP});
  auto out = drain(q);
  REQUIRE(out.size() == 3);
  CHECK(out[0].channel == 3);
  CHECK(out[1].channel == 4);
  CHECK(out[2].channel == 1);
}

TEST_CASE("STOP leaves SETSHADE and LEARN of its channel alone") {
  TxQueue q;
  q.push({5, Command::SETSHADE});
  q.push({5, Command::LEARN});
  q.push({5, Command::STOP});
  auto out = drain(q);
  REQUIRE(out.size() == 3);
  CHECK(out[0].command == Command::STOP);
}

TEST_CASE("command sequences match the old dongle") {
  auto up = command_steps(Command::UP, true);
  REQUIRE(up.size() == 1);
  CHECK(up[0].button == BUTTON_UP);
  CHECK(up[0].repetitions == 2);
  CHECK(command_steps(Command::DOWN, true)[0].button == BUTTON_DOWN);
  CHECK(command_steps(Command::STOP, true)[0].repetitions == 2);

  auto shade = command_steps(Command::SHADE, true);
  REQUIRE(shade.size() == 1);
  CHECK(shade[0].button == BUTTON_STOP);
  CHECK(shade[0].repetitions == 20);

  auto setshade = command_steps(Command::SETSHADE, true);
  REQUIRE(setshade.size() == 4);
  for (auto &s : setshade) {
    CHECK(s.button == BUTTON_STOP);
    CHECK(s.repetitions == 1);
  }
  CHECK(setshade[0].pause_after_ms == 300);
  CHECK(setshade[3].pause_after_ms == 2300);

  auto learn_new = command_steps(Command::LEARN, true);
  REQUIRE(learn_new.size() == 2);
  CHECK(learn_new[0].button == BUTTON_UP_DOWN);
  CHECK(learn_new[0].pause_after_ms == 1000);
  CHECK(learn_new[1].button == BUTTON_STOP);

  auto learn_old = command_steps(Command::LEARN, false);
  REQUIRE(learn_old.size() == 1);
  CHECK(learn_old[0].button == BUTTON_LEARN_OLD);
}

TEST_CASE("group targets: STOP cancels the same group's drive, not a channel's") {
  TxQueue q;
  q.push({0, Command::DOWN, 0x1FF});
  q.push({3, Command::DOWN});
  q.push({0, Command::STOP, 0x1FF});
  auto out = drain(q);
  REQUIRE(out.size() == 2);
  CHECK(out[0].command == Command::STOP);
  CHECK(out[0].group_mask == 0x1FF);
  CHECK(out[1].channel == 3);
  CHECK(out[1].command == Command::DOWN);
}

TEST_CASE("request masks") {
  Request single{5, Command::UP};
  Request group{0, Command::UP, 0x0006};
  CHECK(single.mask() == 0x0020);
  CHECK(group.mask() == 0x0006);
  CHECK_FALSE(single.same_target(group));
}

TEST_CASE("unlearn follows dewenni: UP+DOWN, six STOPs, UP") {
  auto u = command_steps(Command::UNLEARN, true);
  REQUIRE(u.size() == 8);
  CHECK(u[0].button == BUTTON_UP_DOWN);
  for (int i = 1; i <= 6; i++) CHECK(u[i].button == BUTTON_STOP);
  CHECK(u[7].button == BUTTON_UP);
}

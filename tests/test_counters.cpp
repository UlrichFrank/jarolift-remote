#include <doctest/doctest.h>

#include <vector>

#include "core_counters.h"

using namespace esphome::jarolift;

struct FakeStorage : CounterStorage {
  std::vector<uint16_t> stored;  // empty = nothing stored
  bool fail = false;
  int saves = 0;
  int load(uint16_t *values, int max) override {
    int n = 0;
    for (uint16_t v : stored)
      if (n < max) values[n++] = v;
    return n;
  }
  bool save(uint16_t value) override {
    if (fail) return false;
    stored = {value};
    saves++;
    return true;
  }
};

TEST_CASE("first start uses the initial value") {
  FakeStorage s;
  CounterStore store(&s);
  REQUIRE(store.begin(27026));
  CHECK(store.get() == 27026);
  CHECK(s.stored == std::vector<uint16_t>{27026});
}

TEST_CASE("stored counter wins over the initial value") {
  FakeStorage s;
  s.stored = {42};
  CounterStore store(&s);
  REQUIRE(store.begin(500));
  CHECK(store.get() == 42);
  CHECK(s.saves == 0);
}

TEST_CASE("one counter for every channel and group, strictly increasing") {
  FakeStorage s;
  CounterStore store(&s);
  store.begin(10);
  uint16_t a, b, c;
  REQUIRE(store.reserve(&a));
  REQUIRE(store.reserve(&b));
  REQUIRE(store.reserve(&c));
  CHECK(a == 10);
  CHECK(b == 11);
  CHECK(c == 12);
  CHECK(s.stored == std::vector<uint16_t>{13});
}

TEST_CASE("per-channel counters of older firmware merge into the highest") {
  FakeStorage s;
  s.stored = {100, 900, 105, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  CounterStore store(&s);
  REQUIRE(store.begin(0));
  CHECK(store.get() == 900);
  CHECK(s.stored == std::vector<uint16_t>{900});
}

TEST_CASE("counter survives a reboot") {
  FakeStorage s;
  {
    CounterStore store(&s);
    store.begin(0);
    uint16_t v;
    store.reserve(&v);
    store.reserve(&v);
  }
  CounterStore rebooted(&s);
  rebooted.begin(0);
  uint16_t v;
  REQUIRE(rebooted.reserve(&v));
  CHECK(v == 2);
}

TEST_CASE("failed persistence blocks the transmission and never reuses a value") {
  FakeStorage s;
  CounterStore store(&s);
  store.begin(7);
  s.fail = true;
  uint16_t v = 0;
  CHECK_FALSE(store.reserve(&v));
  s.fail = false;
  REQUIRE(store.reserve(&v));
  CHECK(v == 8);
}

TEST_CASE("lowering the counter needs confirmation") {
  FakeStorage s;
  CounterStore store(&s);
  store.begin(100);
  CHECK(store.set(50, false) == CounterStore::SetResult::LOWER_NOT_CONFIRMED);
  CHECK(store.get() == 100);
  CHECK(store.set(150, false) == CounterStore::SetResult::OK);
  CHECK(store.set(50, true) == CounterStore::SetResult::OK);
  CHECK(store.get() == 50);
}

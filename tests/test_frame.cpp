#include <doctest/doctest.h>

#include <cstdint>
#include <numeric>

#include "core_frame.h"
#include "reference/KeeloqLib.h"

using namespace esphome::jarolift;

// Verbatim port of the old dongle's frame construction (Jarolift_MQTT.ino: cmd_up(), keygen(),
// keeloq(), radio_tx()), with its global state, as the oracle for build_frame().
namespace sketch {
static const uint8_t disc_low[16] = {0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80, 0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t disc_high[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80};
static const uint8_t serials[16] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF};

struct Out {
  uint64_t pack;
  uint8_t disc_h;
};

Out send(uint32_t master_msb, uint32_t master_lsb, uint32_t prefix, int channel, uint64_t button, uint16_t devcnt) {
  // cmd_generate_serials(): serial_number = prefix << 8, channel i gets serial_number + i
  uint64_t new_serial = uint32_t((prefix << 8) + channel);
  uint8_t disc_l = disc_low[channel];
  uint8_t disc_h = disc_high[channel];
  int disc = (disc_l << 8) | serials[channel];
  // keygen() - decrypt() takes a 32-bit unsigned long on the ESP8266
  Keeloq k(master_msb, master_lsb);
  uint32_t device_key_lsb = uint32_t(k.decrypt(uint32_t(new_serial | 0x20000000)));
  uint32_t device_key_msb = uint32_t(k.decrypt(uint32_t(new_serial | 0x60000000)));
  // keeloq()
  Keeloq kd(device_key_msb, device_key_lsb);
  unsigned int result = (disc << 16) | devcnt;
  uint32_t dec = uint32_t(kd.encrypt(result));
  // radio_tx()
  uint64_t pack = (button << 60) | (new_serial << 32) | dec;
  return Out{pack, disc_h};
}
}  // namespace sketch

TEST_CASE("build_frame reproduces the old dongle's frames") {
  const uint32_t masters[][2] = {{0x01234567, 0x89abcdef}, {0xDEADBEEF, 0x00C0FFEE}};
  const uint32_t prefixes[] = {0x12deaf, 0x000001, 0xF0A0B0};
  const uint8_t buttons[] = {BUTTON_UP, BUTTON_DOWN, BUTTON_STOP, BUTTON_UP_DOWN, BUTTON_LEARN_OLD};
  const uint16_t counters[] = {0, 1, 1234, 0xFFFF};
  for (auto &m : masters)
    for (uint32_t prefix : prefixes)
      for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++)
        for (uint8_t b : buttons)
          for (uint16_t c : counters) {
            CAPTURE(prefix);
            CAPTURE(int(ch));
            CAPTURE(int(b));
            CAPTURE(c);
            auto want = sketch::send(m[0], m[1], prefix, ch, b, c);
            Frame got = build_frame({m[0], m[1]}, channel_serial(prefix, ch), ch, b, c);
            CHECK(got.data == want.pack);
            CHECK(got.disc_high == want.disc_h);
          }
}

// Port of dewenni's JaroliftController::cmdGroup(): serial of channel 0, disc = mask | serial low byte.
static sketch::Out dewenni_group(uint32_t msb, uint32_t lsb, uint32_t prefix, uint16_t mask, uint64_t button,
                                 uint16_t devcnt) {
  uint64_t new_serial = uint32_t((prefix << 8) | 0);
  uint8_t discL = mask & 0x00FF;
  uint8_t discH = (mask >> 8) & 0x00FF;
  int disc = (discL << 8) | (new_serial & 0xFF);
  Keeloq k(msb, lsb);
  uint32_t lsb_key = uint32_t(k.decrypt(uint32_t(new_serial | 0x20000000)));
  uint32_t msb_key = uint32_t(k.decrypt(uint32_t(new_serial | 0x60000000)));
  Keeloq kd(msb_key, lsb_key);
  unsigned int result = (disc << 16) | devcnt;
  uint32_t enc = uint32_t(kd.encrypt(result));
  return {(button << 60) | (new_serial << 32) | enc, discH};
}

TEST_CASE("group frames match dewenni's bitmask groups") {
  const uint16_t masks[] = {0x0001, 0x0006, 0x01FF, 0x0180, 0xC000, 0xFFFF};
  for (uint16_t mask : masks)
    for (uint8_t b : {BUTTON_UP, BUTTON_DOWN, BUTTON_STOP})
      for (uint16_t c : {uint16_t(0), uint16_t(27026), uint16_t(0xFFFF)}) {
        CAPTURE(mask);
        auto want = dewenni_group(0x01234567, 0x89abcdef, 0x12deaf, mask, b, c);
        Frame got = build_frame_mask({0x01234567, 0x89abcdef}, channel_serial(0x12deaf, 0), mask, b, c);
        CHECK(got.data == want.pack);
        CHECK(got.disc_high == want.disc_h);
      }
}

TEST_CASE("frame_pulses matches the old dongle's timing") {
  Frame f = build_frame({1, 2}, channel_serial(0x12deaf, 3), 3, BUTTON_UP, 42);
  auto one = frame_pulses(f, 1);
  uint32_t total = std::accumulate(one.begin(), one.end(), 0u, [](uint32_t s, const Pulse &p) { return s + p.duration_us; });
  // 1150 + 13 * 780 + 3500 + 72 * 1200 + 16000
  CHECK(total == 117190);
  CHECK(one.front().level == false);
  CHECK(one.back().level == true);
  for (size_t i = 1; i < one.size(); i++) CHECK(one[i].level != one[i - 1].level);

  auto two = frame_pulses(f, 2);
  uint32_t total2 = std::accumulate(two.begin(), two.end(), 0u, [](uint32_t s, const Pulse &p) { return s + p.duration_us; });
  CHECK(total2 == 2 * 117190);
}

// Drives the RX decoder with the TX pulse train as the receiver sees it (RX pin inverted).
static void feed(RxDecoder &rx, const std::vector<Pulse> &pulses, uint32_t start_us, int jitter_us = 0) {
  uint32_t t = start_us;
  int n = 0;
  for (const auto &p : pulses) {
    rx.on_edge(!p.level, t);
    int j = jitter_us ? ((n++ % 3) - 1) * jitter_us : 0;
    t += p.duration_us + j;
  }
  rx.on_edge(false, t);
}

TEST_CASE("receiver decodes a transmitted telegram") {
  const KeeloqKey master{0x01234567, 0x89abcdef};
  for (uint8_t ch : {uint8_t(0), uint8_t(7), uint8_t(9), uint8_t(13)}) {
    CAPTURE(int(ch));
    uint32_t serial = channel_serial(0x012dea, ch);
    Frame f = build_frame(master, serial, ch, BUTTON_DOWN, 777);
    RxDecoder rx;
    feed(rx, frame_pulses(f, 1), 1000, 60);
    RawTelegram raw;
    REQUIRE(rx.poll(&raw));
    CHECK(raw.serial == (serial & 0x0FFFFFFF));
    CHECK(raw.button == BUTTON_DOWN);
    DecodedTelegram t;
    REQUIRE(decode_telegram(raw, master, &t));
    CHECK(t.counter == 777);
    CHECK(t.channel_mask == (1u << ch));
    CHECK_FALSE(rx.poll(&raw));
  }
}

TEST_CASE("receiver rejects corrupted or foreign telegrams") {
  const KeeloqKey master{0x01234567, 0x89abcdef};
  Frame f = build_frame(master, channel_serial(0x012dea, 2), 2, BUTTON_UP, 5);
  RxDecoder rx;
  feed(rx, frame_pulses(f, 1), 0);
  RawTelegram raw;
  REQUIRE(rx.poll(&raw));
  DecodedTelegram t;
  CHECK_FALSE(decode_telegram(raw, {0x11111111, 0x22222222}, &t));
  raw.hopcode ^= 0x00010000;
  CHECK_FALSE(decode_telegram(raw, master, &t));
}

TEST_CASE("receiver ignores noise and truncated telegrams") {
  RxDecoder rx;
  uint32_t t = 0;
  for (int i = 0; i < 500; i++) {
    t += 50 + (i * 37) % 5000;
    rx.on_edge(i & 1, t);
  }
  RawTelegram raw;
  CHECK_FALSE(rx.poll(&raw));

  Frame f = build_frame({1, 2}, channel_serial(0x012dea, 1), 1, BUTTON_UP, 1);
  auto pulses = frame_pulses(f, 1);
  pulses.resize(pulses.size() - 20);
  feed(rx, pulses, t + 100000);
  CHECK_FALSE(rx.poll(&raw));
}

#include "core_frame.h"

namespace esphome {
namespace jarolift {

static constexpr uint32_t LEAD_IN_US = 1150;
static constexpr int PREAMBLE_PULSES = 13;
static constexpr uint32_t PREAMBLE_LOW_US = 400;
static constexpr uint32_t PREAMBLE_HIGH_US = 380;
static constexpr uint32_t GAP_US = 3500;
static constexpr uint32_t SHORT_US = 400;
static constexpr uint32_t LONG_US = 800;
static constexpr uint32_t REPEAT_PAUSE_US = 16000;

Frame build_frame_mask(const KeeloqKey &master, uint32_t serial, uint16_t channel_mask, uint8_t button,
                       uint16_t counter) {
  uint8_t disc_low = channel_mask & 0xFF;
  uint8_t disc_high = channel_mask >> 8;
  uint32_t disc = (uint32_t(disc_low) << 8) | (serial & 0xFF);
  uint32_t hopcode = keeloq_encrypt((disc << 16) | counter, keeloq_device_key(master, serial));
  uint64_t data = (uint64_t(button) << 60) | (uint64_t(serial) << 32) | hopcode;
  return Frame{data, disc_high};
}

static void append(std::vector<Pulse> &out, bool level, uint32_t us) {
  if (!out.empty() && out.back().level == level) {
    out.back().duration_us += us;
  } else {
    out.push_back(Pulse{level, us});
  }
}

static void append_bit(std::vector<Pulse> &out, bool one) {
  append(out, false, one ? SHORT_US : LONG_US);
  append(out, true, one ? LONG_US : SHORT_US);
}

std::vector<Pulse> frame_pulses(const Frame &frame, int repetitions) {
  std::vector<Pulse> out;
  for (int r = 0; r < repetitions; r++) {
    append(out, false, LEAD_IN_US);
    for (int i = 0; i < PREAMBLE_PULSES; i++) {
      append(out, false, PREAMBLE_LOW_US);
      append(out, true, PREAMBLE_HIGH_US);
    }
    append(out, true, GAP_US);
    for (int i = 0; i < 64; i++) append_bit(out, (frame.data >> i) & 1);
    for (int i = 0; i < 8; i++) append_bit(out, (frame.disc_high >> i) & 1);
    append(out, true, REPEAT_PAUSE_US);
  }
  return out;
}

bool decode_telegram(const RawTelegram &raw, const KeeloqKey &master, DecodedTelegram *out) {
  uint32_t plain = keeloq_decrypt(raw.hopcode, keeloq_device_key(master, raw.serial));
  // The low discrimination byte carries the low byte of the serial.
  if (((plain >> 16) & 0xFF) != (raw.serial & 0xFF)) return false;
  out->serial = raw.serial;
  out->button = raw.button;
  out->counter = plain & 0xFFFF;
  out->channel_mask = uint16_t((plain >> 24) & 0xFF) | uint16_t(raw.disc_high) << 8;
  return true;
}

void RxDecoder::on_edge(bool level, uint32_t now_us) {
  uint32_t duration = now_us - last_edge_us_;
  last_edge_us_ = now_us;
  if (level) {
    // A LOW phase just ended: the sync gap, or the second half of a bit.
    if (duration >= SYNC_MIN_US && duration <= SYNC_MAX_US) {
      synced_ = true;
      count_ = 0;
    } else if (synced_ && (duration < BIT_MIN_US || duration > BIT_MAX_US)) {
      synced_ = false;
    }
    return;
  }
  // A HIGH phase just ended: its length alone decides the bit (short = 1). The LOW phase of
  // the last bit merges into the pause between repetitions and cannot be measured.
  if (!synced_) return;
  if (duration < BIT_MIN_US || duration > BIT_MAX_US) {
    synced_ = false;
    return;
  }
  uint8_t mask = 1u << (count_ & 7);
  if (duration < BIT_THRESHOLD_US) {
    bits_[count_ >> 3] |= mask;
  } else {
    bits_[count_ >> 3] &= ~mask;
  }
  if (++count_ < BITS) return;
  synced_ = false;
  if (ready_) return;  // previous telegram not consumed yet; this repetition is dropped
  uint64_t data = 0;
  for (int i = 7; i >= 0; i--) data = (data << 8) | bits_[i];
  frame_.hopcode = uint32_t(data);
  frame_.serial = uint32_t(data >> 32) & 0x0FFFFFFF;
  frame_.button = uint8_t(data >> 60);
  frame_.disc_high = bits_[8];
  ready_ = true;
}

bool RxDecoder::poll(RawTelegram *out) {
  if (!ready_) return false;
  *out = frame_;
  ready_ = false;
  return true;
}

}  // namespace jarolift
}  // namespace esphome

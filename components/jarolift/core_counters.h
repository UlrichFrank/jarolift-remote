#pragma once

#include <cstdint>

namespace esphome {
namespace jarolift {

// Persistence backend for the rolling counter (NVS on the device, a fake in host tests).
class CounterStorage {
 public:
  virtual ~CounterStorage() = default;
  // Loads up to `max` stored values and returns how many there were (0 = nothing stored). Older
  // firmware stored one counter per channel; those values are merged by taking the highest.
  virtual int load(uint16_t *values, int max) = 0;
  virtual bool save(uint16_t value) = 0;
};

// The device's single rolling counter, shared by all channels and groups exactly like the old
// dongle's device counter: motors identify the device by its serial base, so every telegram must
// carry a higher value than the one before, whatever channel it addresses.
// The stored value is the next one to transmit.
class CounterStore {
 public:
  static constexpr int MAX_LEGACY_VALUES = 16;

  explicit CounterStore(CounterStorage *storage) : storage_(storage) {}

  // Loads the counter; on first start (nothing stored) it starts at `initial`.
  bool begin(uint16_t initial);

  // Reserves the next counter value and persists the advanced counter before returning it.
  // Returns false - and nothing may be sent - if it could not be persisted.
  bool reserve(uint16_t *value);

  // Sets the next counter value. Lowering requires `allow_lower`.
  enum class SetResult { OK, LOWER_NOT_CONFIRMED, STORAGE_FAILED };
  SetResult set(uint16_t value, bool allow_lower);

  uint16_t get() const { return counter_; }

 protected:
  CounterStorage *storage_;
  uint16_t counter_{0};
};

}  // namespace jarolift
}  // namespace esphome

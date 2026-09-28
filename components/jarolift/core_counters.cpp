#include "core_counters.h"

namespace esphome {
namespace jarolift {

bool CounterStore::begin(uint16_t initial) {
  uint16_t values[MAX_LEGACY_VALUES];
  int stored = storage_->load(values, MAX_LEGACY_VALUES);
  if (stored <= 0) {
    counter_ = initial;
    return storage_->save(counter_);
  }
  uint16_t highest = 0;
  for (int i = 0; i < stored; i++)
    if (values[i] > highest) highest = values[i];
  counter_ = highest;
  return stored == 1 || storage_->save(counter_);  // rewrite a legacy per-channel layout once
}

bool CounterStore::reserve(uint16_t *value) {
  uint16_t current = counter_;
  counter_ = current + 1;  // wraps at 16 bits like the Keeloq counter itself
  // Keep the advanced value in RAM even if saving fails: never hand out `current` twice.
  if (!storage_->save(counter_)) return false;
  *value = current;
  return true;
}

CounterStore::SetResult CounterStore::set(uint16_t value, bool allow_lower) {
  if (value < counter_ && !allow_lower) return SetResult::LOWER_NOT_CONFIRMED;
  uint16_t previous = counter_;
  counter_ = value;
  if (!storage_->save(counter_)) {
    counter_ = previous;
    return SetResult::STORAGE_FAILED;
  }
  return SetResult::OK;
}

}  // namespace jarolift
}  // namespace esphome

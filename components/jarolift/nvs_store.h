#pragma once

#include "core_counters.h"
#include "core_settings.h"

namespace esphome {
namespace jarolift {

// Settings fields in their own NVS namespace, one key per field.
class NvsKeyValueStore : public KeyValueStore {
 public:
  explicit NvsKeyValueStore(const char *ns) : ns_(ns) {}
  bool get(const std::string &key, std::string *value) override;
  bool set(const std::string &key, const std::string &value) override;
  bool commit() override;

 protected:
  bool open_();
  const char *ns_;
  uint32_t handle_{0};
  bool opened_{false};
};

// The rolling counter, committed on every save so a value is on flash before it is sent. Reads
// the per-channel blob of older firmware as well.
class NvsCounterStorage : public CounterStorage {
 public:
  int load(uint16_t *values, int max) override;
  bool save(uint16_t value) override;
};

}  // namespace jarolift
}  // namespace esphome

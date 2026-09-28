#include "nvs_store.h"

#ifdef USE_ESP32
#include <nvs.h>

#include "esphome/core/log.h"

namespace esphome {
namespace jarolift {

static const char *const TAG = "jarolift.nvs";
static const char *const COUNTER_NS = "jarolift_cnt";
static const char *const COUNTER_KEY = "counters";

bool NvsKeyValueStore::open_() {
  if (opened_) return true;
  nvs_handle_t h;
  esp_err_t err = nvs_open(ns_, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_open(%s) failed: %s", ns_, esp_err_to_name(err));
    return false;
  }
  handle_ = h;
  opened_ = true;
  return true;
}

bool NvsKeyValueStore::get(const std::string &key, std::string *value) {
  if (!open_()) return false;
  size_t len = 0;
  if (nvs_get_str(handle_, key.c_str(), nullptr, &len) != ESP_OK || len == 0) return false;
  std::string buf(len, '\0');
  if (nvs_get_str(handle_, key.c_str(), &buf[0], &len) != ESP_OK) return false;
  buf.resize(len - 1);  // drop the terminating NUL
  *value = buf;
  return true;
}

bool NvsKeyValueStore::set(const std::string &key, const std::string &value) {
  if (!open_()) return false;
  esp_err_t err = nvs_set_str(handle_, key.c_str(), value.c_str());
  if (err != ESP_OK) ESP_LOGE(TAG, "nvs_set_str(%s) failed: %s", key.c_str(), esp_err_to_name(err));
  return err == ESP_OK;
}

bool NvsKeyValueStore::commit() { return open_() && nvs_commit(handle_) == ESP_OK; }

int NvsCounterStorage::load(uint16_t *values, int max) {
  nvs_handle_t h;
  if (nvs_open(COUNTER_NS, NVS_READONLY, &h) != ESP_OK) return 0;
  size_t len = 0;
  esp_err_t err = nvs_get_blob(h, COUNTER_KEY, nullptr, &len);
  if (err != ESP_OK || len == 0 || len % sizeof(uint16_t) != 0 || len > sizeof(uint16_t) * size_t(max)) {
    nvs_close(h);
    return 0;
  }
  err = nvs_get_blob(h, COUNTER_KEY, values, &len);
  nvs_close(h);
  return err == ESP_OK ? int(len / sizeof(uint16_t)) : 0;
}

bool NvsCounterStorage::save(uint16_t value) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(COUNTER_NS, NVS_READWRITE, &h);
  if (err == ESP_OK) err = nvs_set_blob(h, COUNTER_KEY, &value, sizeof(value));
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) ESP_LOGE(TAG, "saving the counter failed: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

}  // namespace jarolift
}  // namespace esphome
#endif

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core_channels.h"
#include "core_commands.h"
#include "core_counters.h"
#include "core_dongle.h"
#include "core_frame.h"
#include "core_queue.h"
#include "core_settings.h"
#include "esphome/components/number/number.h"
#include "esphome/components/time_based/cover/time_based_cover.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "log_buffer.h"
#include "mqtt_link.h"
#include "nvs_store.h"
#include "radio.h"

namespace esphome {
namespace jarolift {

static constexpr float MIN_DURATION_S = 1.0f;
static constexpr float MAX_DURATION_S = 300.0f;

// Hardware of one channel slot.
struct Slot {
  time_based::TimeBasedCover *cover{nullptr};
  number::Number *open_duration{nullptr};
  number::Number *close_duration{nullptr};
  // Durations waiting for the cover to become idle (ms, 0 = nothing pending).
  uint32_t pending_open_ms{0};
  uint32_t pending_close_ms{0};
  std::string published_state;
  int published_position{-1};
};

// Snapshots for the web server task.
struct ShutterInfo {
  uint8_t channel;
  std::string name, label, state;
  int position;
  float open_duration, close_duration;
};
struct GroupInfo {
  uint8_t index;
  std::string name, label;
  uint16_t members;
};

// Takeover from the old dongle: while `host` is set, the device does not transmit, and it keeps
// its counter at the dongle's counter plus `margin`, so the first own telegram after the dongle is
// switched off is ahead of everything the dongle sent.
struct MigrationStatus {
  bool active{false};
  std::string host;
  uint16_t margin{8};
  bool dongle_reachable{false};
  bool dongle_seen{false};
  uint16_t dongle_counter{0};
  uint32_t last_check_s{0};
  std::string last_error;
};

class JaroliftComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  // Build configuration
  Settings &defaults() { return defaults_; }
  void set_web_credentials(const std::string &user, const std::string &password) {
    web_username_ = user;
    web_password_ = password;
  }
  void set_initial_counter(uint16_t value) { initial_counter_ = value; }
  void set_reset_network(bool value) { reset_network_ = value; }
  void add_slot(uint8_t channel, time_based::TimeBasedCover *cover, number::Number *open_duration,
                number::Number *close_duration);

  // Runs before the Ethernet component: hands a saved static configuration to it.
  void apply_network_early();

  // Called from the covers' open/close/stop actions.
  void on_cover_action(uint8_t channel, Command command);
  // Called from the duration numbers' set actions.
  void set_duration(uint8_t channel, bool open, float seconds);

  // Commands from MQTT and the web interface (main loop only). False for unknown targets or
  // commands that do not apply to the target.
  bool command(const std::string &target, const TargetCommand &cmd);

  // Runs `f` in the main loop; safe to call from other tasks.
  void run_in_loop(std::function<void()> &&f) { this->defer(std::move(f)); }

  // Thread-safe views for the web server task.
  Settings settings_copy();
  std::vector<ShutterInfo> shutter_infos();
  std::vector<GroupInfo> group_infos();
  enum class TargetKind { NONE, SHUTTER, GROUP };
  TargetKind target_kind(const std::string &name);
  number::Number *duration_number(const std::string &shutter, bool open);
  MigrationStatus migration_status();

  // Validates and, if valid, applies new settings in the main loop. Broker and layout changes take
  // effect without a restart; network changes need a restart.
  std::vector<FieldError> update_settings(const Settings &wanted, bool *restart_required);

  // Takeover (main loop): adopt identity, labels and counter of the dongle at `host` and lock
  // transmission until the operator confirms the dongle is switched off.
  void apply_migration(const std::string &host, const DongleConfig &dongle, uint16_t margin, bool labels);
  // Ends the lock. Refused (false) while the dongle still answers, unless `force`.
  bool release_migration(bool force);

  MqttLink &mqtt() { return mqtt_; }
  LogBuffer &log_buffer() { return log_buffer_; }
  CounterStore &counters() { return *counters_; }
  bool radio_present() const { return radio_.present(); }

 protected:
  int find_channel_(const std::string &name) const;
  int find_group_(const std::string &name) const;
  std::string state_of_(uint8_t channel) const;
  int position_of_(uint8_t channel) const;
  void apply_shutter_(uint8_t channel, const TargetCommand &cmd, bool silent);
  void apply_group_(uint8_t index, const TargetCommand &cmd);
  void enqueue_(const Request &request);
  void process_tx_();
  void apply_pending_durations_();
  void apply_settings_(const Settings &wanted);

  void start_migration_watch_();
  static void migration_watch_task_(void *arg);
  void on_dongle_counter_(uint16_t dongle_counter);

  void start_mqtt_();
  void on_mqtt_connected_();
  void on_mqtt_message_(const std::string &topic, const std::string &payload);
  void publish_shutter_(uint8_t channel, bool force);
  void publish_durations_(uint8_t channel);
  void clear_retained_(const std::string &name);
  std::string topic_(const std::string &name, const char *leaf) const;

  Settings defaults_;
  Settings settings_;  // written only by the main loop, under settings_mutex_
  Mutex settings_mutex_;
  std::string web_username_, web_password_;
  uint16_t initial_counter_{0};
  bool reset_network_{false};

  Slot slots_[NUM_CHANNELS];

  std::unique_ptr<NvsKeyValueStore> kv_;
  std::unique_ptr<NvsCounterStorage> counter_storage_;
  std::unique_ptr<CounterStore> counters_;
  std::unique_ptr<SettingsStore> settings_store_;

  MigrationStatus migration_;  // under migration_mutex_
  Mutex migration_mutex_;
  bool watch_running_{false};

  Radio radio_;
  TxQueue queue_;
  // Transmission in progress: remaining steps of the current request.
  Request current_{};
  std::vector<Step> steps_;
  size_t step_index_{0};
  uint32_t next_step_at_{0};

  bool suppress_actions_{false};
  MqttLink mqtt_;
  LogBuffer log_buffer_;
};

// Setup hook that runs before the Ethernet component (which takes the manual IP in its setup).
class NetworkBootstrap : public Component {
 public:
  explicit NetworkBootstrap(JaroliftComponent *hub) : hub_(hub) {}
  void setup() override { hub_->apply_network_early(); }
  float get_setup_priority() const override { return setup_priority::ETHERNET + 1.0f; }

 protected:
  JaroliftComponent *hub_;
};

}  // namespace jarolift
}  // namespace esphome

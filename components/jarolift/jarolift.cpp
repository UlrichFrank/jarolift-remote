#include "jarolift.h"

#include <cmath>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "dongle_client.h"
#include "esphome/components/logger/logger.h"
#ifdef USE_ETHERNET
#include "esphome/components/ethernet/ethernet_component.h"
#endif
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "web.h"

namespace esphome {
namespace jarolift {

static const char *const TAG = "jarolift";
static const char *const ROOT = "jarolift";
static constexpr uint32_t MIGRATION_POLL_MS = 15000;

void JaroliftComponent::add_slot(uint8_t channel, time_based::TimeBasedCover *cover, number::Number *open_duration,
                                 number::Number *close_duration) {
  if (channel >= NUM_CHANNELS) return;
  slots_[channel].cover = cover;
  slots_[channel].open_duration = open_duration;
  slots_[channel].close_duration = close_duration;
}

void JaroliftComponent::apply_network_early() {
  NvsKeyValueStore kv("jarolift_cfg");
  SettingsStore store(&kv, defaults_);
  Settings s = store.load();
  if (reset_network_ && !s.dhcp) {
    ESP_LOGW(TAG, "Network reset requested by the build: switching back to DHCP");
    s.dhcp = true;
    store.save(s);
  }
  if (s.dhcp) {
    ESP_LOGI(TAG, "Network: DHCP");
    return;
  }
  Settings check = s;
  check.broker_url.clear();  // only the network fields matter here
  for (const auto &e : validate_settings(check)) {
    if (e.field == "ip" || e.field == "subnet" || e.field == "gateway" || e.field == "dns") {
      ESP_LOGE(TAG, "Saved static network settings invalid (%s: %s) - using DHCP", e.field.c_str(), e.message.c_str());
      return;
    }
  }
#ifdef USE_ETHERNET
  auto ip = [](uint32_t v) { return network::IPAddress(v >> 24, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF); };
  ethernet::ManualIP manual{ip(s.ip), ip(s.gateway), ip(s.subnet), ip(s.dns), network::IPAddress(0, 0, 0, 0)};
  ethernet::global_eth_component->set_manual_ip(manual);
  ESP_LOGI(TAG, "Network: static %s/%s gw %s dns %s", format_ipv4(s.ip).c_str(), format_ipv4(s.subnet).c_str(),
           format_ipv4(s.gateway).c_str(), format_ipv4(s.dns).c_str());
#endif
}

void JaroliftComponent::setup() {
  if (logger::global_logger != nullptr) {
    logger::global_logger->add_log_callback(this, [](void *self, uint8_t, const char *, const char *message, size_t len) {
      static_cast<JaroliftComponent *>(self)->log_buffer_.add(message, len);
    });
  }

  if (defaults_.client_id.empty()) defaults_.client_id = "jarolift-remote-" + get_mac_address().substr(6);
  kv_ = make_unique<NvsKeyValueStore>("jarolift_cfg");
  settings_store_ = make_unique<SettingsStore>(kv_.get(), defaults_);
  settings_ = settings_store_->load();
  if (!validate_layout(settings_.channels, settings_.groups).empty()) {
    ESP_LOGE(TAG, "Stored layout is invalid, using the build layout");
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) settings_.channels[ch] = defaults_.channels[ch];
    for (uint8_t g = 0; g < MAX_GROUPS; g++) settings_.groups[g] = defaults_.groups[g];
  }

  counter_storage_ = make_unique<NvsCounterStorage>();
  counters_ = make_unique<CounterStore>(counter_storage_.get());
  if (!counters_->begin(initial_counter_)) ESP_LOGE(TAG, "Rolling counter could not be initialised");

  {
    std::string host, margin;
    if (kv_->get("mig_host", &host) && !host.empty()) {
      LockGuard lock(migration_mutex_);
      migration_.active = true;
      migration_.host = host;
      if (kv_->get("mig_margin", &margin)) migration_.margin = uint16_t(atoi(margin.c_str()));
    }
  }

  radio_.setup();

  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    auto &s = slots_[ch];
    if (s.cover == nullptr) {
      ESP_LOGE(TAG, "Channel %u has no cover slot in the YAML", ch);
      continue;
    }
    // Restored durations from the numbers; the covers are idle at boot.
    if (s.open_duration->has_state()) s.cover->set_open_duration(uint32_t(s.open_duration->state * 1000));
    if (s.close_duration->has_state()) s.cover->set_close_duration(uint32_t(s.close_duration->state * 1000));
    s.cover->add_on_state_callback([this, ch]() { this->publish_shutter_(ch, false); });
    s.open_duration->add_on_state_callback([this, ch](float) { this->publish_durations_(ch); });
    s.close_duration->add_on_state_callback([this, ch](float) { this->publish_durations_(ch); });
  }

  setup_web(this, web_username_, web_password_);
  start_mqtt_();
  if (migration_.active) start_migration_watch_();
}

void JaroliftComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Jarolift remote:");
  ESP_LOGCONFIG(TAG, "  Radio: %s", radio_.present() ? "CC1101" : "not present");
  ESP_LOGCONFIG(TAG, "  Broker: %s", settings_.broker_url.empty() ? "(disabled)" : settings_.broker_url.c_str());
  ESP_LOGCONFIG(TAG, "  Client ID: %s", settings_.client_id.c_str());
  ESP_LOGCONFIG(TAG, "  Learn mode: %s", settings_.learn_mode_new ? "new" : "old");
  ESP_LOGCONFIG(TAG, "  Rolling counter: %u", counters_->get());
  if (migration_.active)
    ESP_LOGCONFIG(TAG, "  Takeover from dongle %s in progress: transmission locked", migration_.host.c_str());
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    const auto &c = settings_.channels[ch];
    if (c.enabled) ESP_LOGCONFIG(TAG, "  Channel %2u: %s", ch, c.name.c_str());
  }
  for (uint8_t g = 0; g < MAX_GROUPS; g++) {
    const auto &grp = settings_.groups[g];
    if (grp.enabled) ESP_LOGCONFIG(TAG, "  Group   %u: %s (mask 0x%04X)", g + 1, grp.name.c_str(), grp.members);
  }
}

void JaroliftComponent::loop() {
  MqttLink::Event event;
  while (mqtt_.poll(&event)) {
    switch (event.type) {
      case MqttLink::Event::CONNECTED:
        on_mqtt_connected_();
        break;
      case MqttLink::Event::DISCONNECTED:
        ESP_LOGW(TAG, "Broker connection lost");
        break;
      case MqttLink::Event::MESSAGE:
        on_mqtt_message_(event.topic, event.payload);
        break;
    }
  }
  apply_pending_durations_();
  process_tx_();
}

// ---------------------------------------------------------------------------------------------
// Layout lookups

int JaroliftComponent::find_channel_(const std::string &name) const {
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++)
    if (settings_.channels[ch].enabled && settings_.channels[ch].name == name) return ch;
  return -1;
}

int JaroliftComponent::find_group_(const std::string &name) const {
  for (uint8_t g = 0; g < MAX_GROUPS; g++)
    if (settings_.groups[g].enabled && settings_.groups[g].name == name) return g;
  return -1;
}

std::string JaroliftComponent::state_of_(uint8_t channel) const {
  switch (slots_[channel].cover->current_operation) {
    case cover::COVER_OPERATION_OPENING:
      return "opening";
    case cover::COVER_OPERATION_CLOSING:
      return "closing";
    default:
      return position_of_(channel) == 0 ? "closed" : "open";
  }
}

int JaroliftComponent::position_of_(uint8_t channel) const {
  return int(std::lround(slots_[channel].cover->position * 100.0f));
}

Settings JaroliftComponent::settings_copy() {
  LockGuard lock(settings_mutex_);
  return settings_;
}

std::vector<ShutterInfo> JaroliftComponent::shutter_infos() {
  Settings s = settings_copy();
  std::vector<ShutterInfo> out;
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    if (!s.channels[ch].enabled || slots_[ch].cover == nullptr) continue;
    const auto &slot = slots_[ch];
    out.push_back(ShutterInfo{ch, s.channels[ch].name, s.channels[ch].label, state_of_(ch), position_of_(ch),
                              slot.open_duration->has_state() ? slot.open_duration->state : NAN,
                              slot.close_duration->has_state() ? slot.close_duration->state : NAN});
  }
  return out;
}

std::vector<GroupInfo> JaroliftComponent::group_infos() {
  Settings s = settings_copy();
  std::vector<GroupInfo> out;
  for (uint8_t g = 0; g < MAX_GROUPS; g++)
    if (s.groups[g].enabled) out.push_back(GroupInfo{g, s.groups[g].name, s.groups[g].label, s.groups[g].members});
  return out;
}

JaroliftComponent::TargetKind JaroliftComponent::target_kind(const std::string &name) {
  LockGuard lock(settings_mutex_);
  if (find_channel_(name) >= 0) return TargetKind::SHUTTER;
  if (find_group_(name) >= 0) return TargetKind::GROUP;
  return TargetKind::NONE;
}

number::Number *JaroliftComponent::duration_number(const std::string &shutter, bool open) {
  LockGuard lock(settings_mutex_);
  int ch = find_channel_(shutter);
  if (ch < 0) return nullptr;
  return open ? slots_[ch].open_duration : slots_[ch].close_duration;
}

// ---------------------------------------------------------------------------------------------
// Commands

bool JaroliftComponent::command(const std::string &target, const TargetCommand &cmd) {
  if (cmd.type == TargetCommand::INVALID) return false;
  const char *verb = target_command_name(cmd.type);
  std::string arg = cmd.type == TargetCommand::POSITION ? " " + std::to_string(cmd.position) : "";
  int ch = find_channel_(target);
  if (ch >= 0) {
    ESP_LOGI(TAG, "%s: %s%s", target.c_str(), verb, arg.c_str());
    apply_shutter_(uint8_t(ch), cmd, false);
    return true;
  }
  int g = find_group_(target);
  if (g < 0) return false;
  // Groups have no serial of their own: nothing to teach, forget or store a shade position for.
  if (cmd.type == TargetCommand::SETSHADE || cmd.type == TargetCommand::LEARN || cmd.type == TargetCommand::UNLEARN)
    return false;
  ESP_LOGI(TAG, "group %s: %s%s", target.c_str(), verb, arg.c_str());
  apply_group_(uint8_t(g), cmd);
  return true;
}

void JaroliftComponent::apply_shutter_(uint8_t channel, const TargetCommand &cmd, bool silent) {
  auto *cov = slots_[channel].cover;
  auto call = cov->make_call();
  switch (cmd.type) {
    case TargetCommand::OPEN:
      call.set_command_open();
      break;
    case TargetCommand::CLOSE:
      call.set_command_close();
      break;
    case TargetCommand::STOP:
      call.set_command_stop();
      break;
    case TargetCommand::POSITION:
      call.set_position(cmd.position / 100.0f);
      break;
    case TargetCommand::SHADE:
    case TargetCommand::SETSHADE:
    case TargetCommand::LEARN:
    case TargetCommand::UNLEARN:
      // The motor goes to a position the device does not know (or does not move): freeze the estimate.
      if (!silent) {
        Command c = cmd.type == TargetCommand::SHADE      ? Command::SHADE
                    : cmd.type == TargetCommand::SETSHADE ? Command::SETSHADE
                    : cmd.type == TargetCommand::LEARN    ? Command::LEARN
                                                          : Command::UNLEARN;
        enqueue_(Request{channel, c});
      }
      if (cov->current_operation == cover::COVER_OPERATION_IDLE) return;
      call.set_command_stop();
      silent = true;
      break;
    case TargetCommand::INVALID:
      return;
  }
  bool previous = suppress_actions_;
  suppress_actions_ = silent;
  call.perform();
  suppress_actions_ = previous;
}

void JaroliftComponent::apply_group_(uint8_t index, const TargetCommand &cmd) {
  const uint16_t members = settings_.groups[index].members;
  TargetCommand member_cmd = cmd;
  if (cmd.type == TargetCommand::POSITION && cmd.position == 100) member_cmd.type = TargetCommand::OPEN;
  if (cmd.type == TargetCommand::POSITION && cmd.position == 0) member_cmd.type = TargetCommand::CLOSE;

  auto for_members = [&](const TargetCommand &c, bool silent) {
    for (uint8_t m = 0; m < NUM_CHANNELS; m++)
      if ((members & (1u << m)) && settings_.channels[m].enabled) apply_shutter_(m, c, silent);
  };
  switch (member_cmd.type) {
    case TargetCommand::OPEN:
    case TargetCommand::CLOSE:
    case TargetCommand::STOP:
    case TargetCommand::SHADE: {
      // One telegram addressed to all members by their channel bits; every member follows
      // silently with its own durations.
      Command c = member_cmd.type == TargetCommand::OPEN    ? Command::UP
                  : member_cmd.type == TargetCommand::CLOSE ? Command::DOWN
                  : member_cmd.type == TargetCommand::STOP  ? Command::STOP
                                                            : Command::SHADE;
      enqueue_(Request{0, c, members});
      for_members(member_cmd, true);
      break;
    }
    case TargetCommand::POSITION:
      // Members travel at different speeds: position each one individually.
      for_members(member_cmd, false);
      break;
    default:
      break;
  }
}

void JaroliftComponent::on_cover_action(uint8_t channel, Command command) {
  if (suppress_actions_) return;
  // Only active channels are commanded; a cover in an unused slot never moves by itself.
  if (channel >= NUM_CHANNELS || !settings_.channels[channel].enabled) return;
  enqueue_(Request{channel, command});
}

void JaroliftComponent::enqueue_(const Request &request) {
  if (!request.is_group() && request.channel >= NUM_CHANNELS) {
    ESP_LOGW(TAG, "Rejected %s on invalid channel %u", command_name(request.command), request.channel);
    return;
  }
  queue_.push(request);
}

void JaroliftComponent::process_tx_() {
  const uint32_t now = millis();
  if (radio_.busy() || int32_t(now - next_step_at_) < 0) return;

  if (step_index_ >= steps_.size()) {
    steps_.clear();
    step_index_ = 0;
    if (!queue_.pop(&current_)) return;
    char target[24];
    if (current_.is_group()) {
      snprintf(target, sizeof(target), "mask 0x%04X", current_.group_mask);
    } else {
      snprintf(target, sizeof(target), "channel %u", current_.channel);
    }
    if (migration_.active) {
      ESP_LOGW(TAG, "Takeover in progress, transmission locked: %s on %s not sent", command_name(current_.command),
               target);
      return;
    }
    if (!radio_.present()) {
      ESP_LOGW(TAG, "No radio: %s on %s not sent", command_name(current_.command), target);
      return;
    }
    steps_ = command_steps(current_.command, settings_.learn_mode_new);
  }

  const Step &step = steps_[step_index_++];
  uint16_t counter;
  if (!counters_->reserve(&counter)) {
    ESP_LOGE(TAG, "Counter could not be persisted - %s not sent", command_name(current_.command));
    steps_.clear();
    step_index_ = 0;
    return;
  }
  // Groups use the serial of channel 0 with all member bits, as dewenni's controller does.
  uint8_t serial_channel = current_.is_group() ? 0 : current_.channel;
  uint32_t serial = channel_serial(settings_.serial_prefix, serial_channel);
  Frame frame =
      build_frame_mask({settings_.master_msb, settings_.master_lsb}, serial, current_.mask(), step.button, counter);
  ESP_LOGI(TAG, "TX %s 0x%04X %s button 0x%X x%u counter %u", current_.is_group() ? "group" : "channel",
           current_.mask(), command_name(current_.command), step.button, step.repetitions, counter);
  radio_.transmit(frame, step.repetitions);
  next_step_at_ = now + step.pause_after_ms;
}

// ---------------------------------------------------------------------------------------------
// Travel times

void JaroliftComponent::set_duration(uint8_t channel, bool open, float seconds) {
  if (channel >= NUM_CHANNELS) return;
  uint32_t ms = uint32_t(std::lround(seconds * 1000));
  if (open) {
    slots_[channel].pending_open_ms = ms;
  } else {
    slots_[channel].pending_close_ms = ms;
  }
}

void JaroliftComponent::apply_pending_durations_() {
  for (auto &s : slots_) {
    if (s.cover == nullptr) continue;
    // recompute_position_() reads the durations mid-travel, so only change them while idle.
    if (s.cover->current_operation != cover::COVER_OPERATION_IDLE) continue;
    if (s.pending_open_ms) {
      s.cover->set_open_duration(s.pending_open_ms);
      s.pending_open_ms = 0;
    }
    if (s.pending_close_ms) {
      s.cover->set_close_duration(s.pending_close_ms);
      s.pending_close_ms = 0;
    }
  }
}

// ---------------------------------------------------------------------------------------------
// Settings

static bool broker_fields_differ(const Settings &a, const Settings &b) {
  return a.broker_url != b.broker_url || a.mqtt_username != b.mqtt_username || a.mqtt_password != b.mqtt_password ||
         a.client_id != b.client_id || a.ca_certificate != b.ca_certificate;
}

static bool network_fields_differ(const Settings &a, const Settings &b) {
  return a.dhcp != b.dhcp || a.ip != b.ip || a.subnet != b.subnet || a.gateway != b.gateway || a.dns != b.dns;
}

std::vector<FieldError> JaroliftComponent::update_settings(const Settings &wanted, bool *restart_required) {
  auto errors = validate_settings(wanted);
  if (!errors.empty()) return errors;
  *restart_required = network_fields_differ(settings_copy(), wanted);
  this->defer([this, wanted]() { this->apply_settings_(wanted); });
  return errors;
}

void JaroliftComponent::apply_settings_(const Settings &wanted) {
  Settings previous = settings_copy();
  if (!settings_store_->save(wanted)) {
    ESP_LOGE(TAG, "Saving settings failed");
    return;
  }
  {
    LockGuard lock(settings_mutex_);
    settings_ = wanted;
  }
  ESP_LOGI(TAG, "Settings saved");
  if (previous.serial_prefix != wanted.serial_prefix)
    ESP_LOGW(TAG, "Serial prefix changed - every motor has to be taught again");

  bool shutters_changed = false;
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    const auto &a = previous.channels[ch], &b = wanted.channels[ch];
    if (a.enabled != b.enabled || a.name != b.name) {
      shutters_changed = true;
      ESP_LOGI(TAG, "Channel %u: %s '%s' -> %s '%s'", ch, a.enabled ? "active" : "free", a.name.c_str(),
               b.enabled ? "active" : "free", b.name.c_str());
    }
  }
  if (shutters_changed) {
    for (const auto &name : stale_shutter_names(previous.channels, wanted.channels)) clear_retained_(name);
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
      slots_[ch].published_state.clear();
      slots_[ch].published_position = -1;
      if (wanted.channels[ch].enabled) {
        publish_shutter_(ch, true);
        publish_durations_(ch);
      }
    }
  }

  if (broker_fields_differ(previous, wanted)) {
    ESP_LOGI(TAG, "Broker settings changed, reconnecting");
    start_mqtt_();
  }
}

// ---------------------------------------------------------------------------------------------
// Takeover from the old dongle

MigrationStatus JaroliftComponent::migration_status() {
  LockGuard lock(migration_mutex_);
  return migration_;
}

void JaroliftComponent::apply_migration(const std::string &host, const DongleConfig &dongle, uint16_t margin,
                                        bool labels) {
  Settings s = settings_copy();
  s.master_msb = dongle.master_msb;
  s.master_lsb = dongle.master_lsb;
  s.serial_prefix = dongle.serial_prefix;
  s.learn_mode_new = dongle.learn_mode_new;
  if (labels) {
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
      const std::string &n = dongle.channel_names[ch];
      if (n.empty()) continue;
      s.channels[ch].label = n.substr(0, MAX_CHANNEL_LABEL);
      // The dongle's group channels ("Alle", "OG Eltern", ...) name the groups with that topic.
      for (auto &grp : s.groups)
        if (grp.enabled && grp.label.empty() && grp.name == topic_from_label(n)) grp.label = n.substr(0, MAX_CHANNEL_LABEL);
    }
  }
  apply_settings_(s);

  kv_->set("mig_host", host);
  kv_->set("mig_margin", std::to_string(margin));
  kv_->commit();
  {
    LockGuard lock(migration_mutex_);
    migration_ = MigrationStatus{};
    migration_.active = true;
    migration_.host = host;
    migration_.margin = margin;
    migration_.dongle_reachable = true;
    migration_.dongle_seen = true;
    migration_.dongle_counter = dongle.device_counter;
    migration_.last_check_s = millis() / 1000;
  }
  ESP_LOGW(TAG, "Takeover from dongle %s: identity adopted (prefix 0x%06X, %s learn mode), transmission locked",
           host.c_str(), unsigned(dongle.serial_prefix), dongle.learn_mode_new ? "new" : "old");
  on_dongle_counter_(dongle.device_counter);
  start_migration_watch_();
}

void JaroliftComponent::on_dongle_counter_(uint16_t dongle_counter) {
  uint16_t margin;
  {
    LockGuard lock(migration_mutex_);
    if (!migration_.active) return;
    margin = migration_.margin;
  }
  uint32_t wanted = uint32_t(dongle_counter) + margin;
  if (wanted > 0xFFFF) wanted = 0xFFFF;
  if (wanted > counters_->get() && counters_->set(uint16_t(wanted), false) == CounterStore::SetResult::OK)
    ESP_LOGI(TAG, "Counter follows the dongle: %u + %u = %u", dongle_counter, margin, unsigned(wanted));
}

bool JaroliftComponent::release_migration(bool force) {
  MigrationStatus st = migration_status();
  if (!st.active) return true;
  if (st.dongle_reachable && !force) return false;
  kv_->set("mig_host", "");
  kv_->commit();
  {
    LockGuard lock(migration_mutex_);
    migration_.active = false;
  }
  ESP_LOGW(TAG, "Takeover finished%s: transmission released, counter %u", force ? " (forced)" : "",
           counters_->get());
  return true;
}

void JaroliftComponent::start_migration_watch_() {
  if (watch_running_) return;
  watch_running_ = true;
  xTaskCreate(&JaroliftComponent::migration_watch_task_, "dongle_watch", 6144, this, 1, nullptr);
}

void JaroliftComponent::migration_watch_task_(void *arg) {
  auto *self = static_cast<JaroliftComponent *>(arg);
  while (true) {
    MigrationStatus st = self->migration_status();
    if (!st.active) break;
    DongleConfig cfg;
    std::string error;
    bool ok = read_dongle(st.host, false, &cfg, &error);
    {
      LockGuard lock(self->migration_mutex_);
      self->migration_.dongle_reachable = ok;
      self->migration_.last_check_s = millis() / 1000;
      self->migration_.last_error = ok ? "" : error;
      if (ok) {
        self->migration_.dongle_seen = true;
        self->migration_.dongle_counter = cfg.device_counter;
      }
    }
    if (ok) {
      uint16_t counter = cfg.device_counter;
      self->run_in_loop([self, counter]() { self->on_dongle_counter_(counter); });
    }
    vTaskDelay(pdMS_TO_TICKS(MIGRATION_POLL_MS));
  }
  self->watch_running_ = false;
  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------------------------
// MQTT

std::string JaroliftComponent::topic_(const std::string &name, const char *leaf) const {
  return std::string(ROOT) + "/" + name + "/" + leaf;
}

void JaroliftComponent::start_mqtt_() {
  MqttOptions options;
  std::string error;
  if (!parse_broker_url(settings_.broker_url, &options.url, &error)) {
    ESP_LOGE(TAG, "Invalid broker URL '%s': %s", settings_.broker_url.c_str(), error.c_str());
    options.url.enabled = false;
  }
  options.username = settings_.mqtt_username;
  options.password = settings_.mqtt_password;
  options.client_id = settings_.client_id;
  options.ca_certificate = settings_.ca_certificate;
  options.will_topic = std::string(ROOT) + "/bridge/state";
  options.will_payload = "offline";
  mqtt_.start(options);
}

void JaroliftComponent::on_mqtt_connected_() {
  ESP_LOGI(TAG, "Connected to broker");
  mqtt_.publish(std::string(ROOT) + "/bridge/state", "online", true);
  mqtt_.subscribe(std::string(ROOT) + "/+/set");
  mqtt_.subscribe(std::string(ROOT) + "/+/get");
  mqtt_.subscribe(std::string(ROOT) + "/+/open_duration/set");
  mqtt_.subscribe(std::string(ROOT) + "/+/close_duration/set");
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    if (!settings_.channels[ch].enabled) continue;
    publish_shutter_(ch, true);
    publish_durations_(ch);
  }
}

void JaroliftComponent::on_mqtt_message_(const std::string &topic, const std::string &payload) {
  // jarolift/<name>/<leaf...>
  const std::string prefix = std::string(ROOT) + "/";
  if (topic.compare(0, prefix.size(), prefix) != 0) return;
  std::string rest = topic.substr(prefix.size());
  size_t slash = rest.find('/');
  if (slash == std::string::npos) return;
  std::string name = rest.substr(0, slash);
  std::string leaf = rest.substr(slash + 1);
  int ch = find_channel_(name);

  if (leaf == "set") {
    if (!command(name, parse_target_command(payload)))
      ESP_LOGW(TAG, "Ignored '%s' on %s", payload.c_str(), topic.c_str());
    return;
  }
  if (leaf == "get") {
    if (ch >= 0) {
      publish_shutter_(uint8_t(ch), true);
      publish_durations_(uint8_t(ch));
      return;
    }
    int g = find_group_(name);
    if (g < 0) return;
    for (uint8_t m = 0; m < NUM_CHANNELS; m++)
      if ((settings_.groups[g].members & (1u << m)) && settings_.channels[m].enabled) publish_shutter_(m, true);
    return;
  }
  if (leaf == "open_duration/set" || leaf == "close_duration/set") {
    float seconds;
    if (ch < 0 || !parse_duration(payload, MIN_DURATION_S, MAX_DURATION_S, &seconds)) {
      ESP_LOGW(TAG, "Ignored '%s' on %s", payload.c_str(), topic.c_str());
      return;
    }
    auto *number = leaf[0] == 'o' ? slots_[ch].open_duration : slots_[ch].close_duration;
    number->make_call().set_value(seconds).perform();
  }
}

void JaroliftComponent::publish_shutter_(uint8_t channel, bool force) {
  if (!settings_.channels[channel].enabled) return;
  auto &s = slots_[channel];
  const std::string &name = settings_.channels[channel].name;
  std::string state = state_of_(channel);
  int position = position_of_(channel);
  if (force || state != s.published_state) {
    if (mqtt_.publish(topic_(name, "state"), state, true)) s.published_state = state;
  }
  if (force || position != s.published_position) {
    if (mqtt_.publish(topic_(name, "position"), std::to_string(position), true)) s.published_position = position;
  }
}

void JaroliftComponent::publish_durations_(uint8_t channel) {
  if (!settings_.channels[channel].enabled) return;
  auto &s = slots_[channel];
  const std::string &name = settings_.channels[channel].name;
  char buf[16];
  if (s.open_duration->has_state()) {
    snprintf(buf, sizeof(buf), "%.1f", s.open_duration->state);
    mqtt_.publish(topic_(name, "open_duration"), buf, true);
  }
  if (s.close_duration->has_state()) {
    snprintf(buf, sizeof(buf), "%.1f", s.close_duration->state);
    mqtt_.publish(topic_(name, "close_duration"), buf, true);
  }
}

void JaroliftComponent::clear_retained_(const std::string &name) {
  ESP_LOGI(TAG, "Clearing retained topics of '%s'", name.c_str());
  for (const char *leaf : {"state", "position", "open_duration", "close_duration"}) mqtt_.publish(topic_(name, leaf), "", true);
}

}  // namespace jarolift
}  // namespace esphome

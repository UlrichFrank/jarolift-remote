#pragma once

#ifdef USE_ESP32

#include <deque>
#include <string>

#include <mqtt_client.h>

#include "core_url.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace jarolift {

struct MqttOptions {
  BrokerUrl url;
  std::string username;
  std::string password;
  std::string client_id;
  std::string ca_certificate;  // PEM; empty = built-in bundle of public root CAs
  std::string will_topic;
  std::string will_payload;
};

// Runtime-configured MQTT client on ESP-IDF's esp-mqtt. Events from the client task are queued
// and handed to the main loop through poll().
class MqttLink {
 public:
  enum class State { DISABLED, CONNECTING, CONNECTED, DISCONNECTED };
  struct Event {
    enum Type { CONNECTED, DISCONNECTED, MESSAGE } type;
    std::string topic;
    std::string payload;
  };

  // (Re)starts the client with new options; an disabled URL just stops it.
  void start(const MqttOptions &options);
  void stop();

  bool poll(Event *event);
  bool publish(const std::string &topic, const std::string &payload, bool retain, int qos = 1);
  bool subscribe(const std::string &topic, int qos = 1);

  State state() const { return state_; }
  const char *state_name() const;
  std::string last_error();

 protected:
  static void event_handler_(void *arg, esp_event_base_t base, int32_t id, void *data);
  void handle_event_(esp_mqtt_event_handle_t event);
  void push_(Event &&event);
  void set_error_(const std::string &error);

  esp_mqtt_client_handle_t volatile client_{nullptr};
  MqttOptions options_;
  std::string uri_host_;
  volatile State state_{State::DISABLED};
  Mutex mutex_;
  std::deque<Event> events_;
  std::string last_error_;
  std::string partial_topic_, partial_payload_;
};

}  // namespace jarolift
}  // namespace esphome

#endif

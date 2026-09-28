#include "mqtt_link.h"

#ifdef USE_ESP32

#include <esp_crt_bundle.h>
#include <esp_tls_errors.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esphome/core/log.h"

namespace esphome {
namespace jarolift {

static const char *const TAG = "jarolift.mqtt";
static constexpr size_t MAX_QUEUED_EVENTS = 32;

void MqttLink::start(const MqttOptions &options) {
  stop();
  options_ = options;
  if (!options_.url.enabled) {
    state_ = State::DISABLED;
    ESP_LOGI(TAG, "Broker disabled");
    return;
  }

  esp_mqtt_client_config_t cfg{};
  cfg.broker.address.hostname = options_.url.host.c_str();
  cfg.broker.address.port = options_.url.port;
  switch (options_.url.scheme) {
    case BrokerScheme::MQTT:
      cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
      break;
    case BrokerScheme::MQTTS:
      cfg.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
      break;
    case BrokerScheme::WS:
      cfg.broker.address.transport = MQTT_TRANSPORT_OVER_WS;
      break;
    case BrokerScheme::WSS:
      cfg.broker.address.transport = MQTT_TRANSPORT_OVER_WSS;
      break;
  }
  if (scheme_is_websocket(options_.url.scheme)) cfg.broker.address.path = options_.url.path.c_str();
  if (scheme_uses_tls(options_.url.scheme)) {
    if (options_.ca_certificate.empty()) {
      cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    } else {
      cfg.broker.verification.certificate = options_.ca_certificate.c_str();
      cfg.broker.verification.certificate_len = options_.ca_certificate.size() + 1;
    }
    cfg.broker.verification.skip_cert_common_name_check = false;
  }
  if (!options_.username.empty()) cfg.credentials.username = options_.username.c_str();
  if (!options_.password.empty()) cfg.credentials.authentication.password = options_.password.c_str();
  cfg.credentials.client_id = options_.client_id.c_str();
  cfg.session.last_will.topic = options_.will_topic.c_str();
  cfg.session.last_will.msg = options_.will_payload.c_str();
  cfg.session.last_will.qos = 1;
  cfg.session.last_will.retain = 1;
  cfg.session.keepalive = 15;
  cfg.network.reconnect_timeout_ms = 5000;
  cfg.network.timeout_ms = 10000;

  client_ = esp_mqtt_client_init(&cfg);
  if (client_ == nullptr) {
    set_error_("client initialisation failed");
    state_ = State::DISCONNECTED;
    return;
  }
  esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &MqttLink::event_handler_, this);
  state_ = State::CONNECTING;
  ESP_LOGI(TAG, "Connecting to %s as '%s'", options_.url.to_string().c_str(), options_.client_id.c_str());
  esp_err_t err = esp_mqtt_client_start(client_);
  if (err != ESP_OK) {
    set_error_(std::string("start failed: ") + esp_err_to_name(err));
    state_ = State::DISCONNECTED;
  }
}

// Tears a client down in its own task: esp_mqtt_client_stop() waits for the client task, which
// can sit in DNS resolution or a TCP connect for many seconds and must not block the main loop.
static void destroy_client_task(void *arg) {
  auto client = static_cast<esp_mqtt_client_handle_t>(arg);
  esp_mqtt_client_disconnect(client);
  esp_mqtt_client_stop(client);
  esp_mqtt_client_destroy(client);
  vTaskDelete(nullptr);
}

void MqttLink::stop() {
  esp_mqtt_client_handle_t old = client_;
  if (old == nullptr) return;
  if (state_ == State::CONNECTED && !options_.will_topic.empty()) {
    // A clean disconnect discards the last will, so say goodbye explicitly.
    esp_mqtt_client_enqueue(old, options_.will_topic.c_str(), options_.will_payload.c_str(), 0, 1, 1, true);
  }
  client_ = nullptr;  // events still arriving from `old` are ignored from now on
  state_ = State::DISABLED;
  if (xTaskCreate(destroy_client_task, "mqtt_stop", 4096, old, 1, nullptr) != pdPASS) {
    ESP_LOGW(TAG, "Could not start the teardown task, stopping inline");
    esp_mqtt_client_stop(old);
    esp_mqtt_client_destroy(old);
  }
  LockGuard lock(mutex_);
  events_.clear();
}

bool MqttLink::poll(Event *event) {
  LockGuard lock(mutex_);
  if (events_.empty()) return false;
  *event = std::move(events_.front());
  events_.pop_front();
  return true;
}

bool MqttLink::publish(const std::string &topic, const std::string &payload, bool retain, int qos) {
  if (client_ == nullptr || state_ != State::CONNECTED) return false;
  return esp_mqtt_client_enqueue(client_, topic.c_str(), payload.data(), int(payload.size()), qos, retain ? 1 : 0,
                                 true) >= 0;
}

bool MqttLink::subscribe(const std::string &topic, int qos) {
  if (client_ == nullptr || state_ != State::CONNECTED) return false;
  return esp_mqtt_client_subscribe_single(client_, topic.c_str(), qos) >= 0;
}

const char *MqttLink::state_name() const {
  switch (state_) {
    case State::DISABLED:
      return "disabled";
    case State::CONNECTING:
      return "connecting";
    case State::CONNECTED:
      return "connected";
    case State::DISCONNECTED:
      return "disconnected";
  }
  return "?";
}

std::string MqttLink::last_error() {
  LockGuard lock(mutex_);
  return last_error_;
}

void MqttLink::set_error_(const std::string &error) {
  ESP_LOGW(TAG, "%s", error.c_str());
  LockGuard lock(mutex_);
  last_error_ = error;
}

void MqttLink::push_(Event &&event) {
  LockGuard lock(mutex_);
  if (events_.size() >= MAX_QUEUED_EVENTS) events_.pop_front();
  events_.push_back(std::move(event));
}

void MqttLink::event_handler_(void *arg, esp_event_base_t, int32_t, void *data) {
  static_cast<MqttLink *>(arg)->handle_event_(static_cast<esp_mqtt_event_handle_t>(data));
}

static std::string describe_error(const esp_mqtt_error_codes_t *e) {
  if (e == nullptr) return "unknown error";
  switch (e->error_type) {
    case MQTT_ERROR_TYPE_TCP_TRANSPORT: {
      std::string s;
      if (e->esp_tls_cert_verify_flags != 0) {
        s = "TLS: the broker's certificate could not be verified";
      } else {
        switch (e->esp_tls_last_esp_err) {
          case ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME:
            s = "DNS: the broker's host name could not be resolved";
            break;
          case ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST:
            s = "connection to the broker failed";
            break;
          case ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT:
            s = "connection to the broker timed out";
            break;
          case ESP_ERR_ESP_TLS_CANNOT_CREATE_SOCKET:
            s = "no free network socket";
            break;
          default:
            s = std::string("transport error: ") + esp_err_to_name(e->esp_tls_last_esp_err);
        }
      }
      if (e->esp_tls_stack_err != 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), " (TLS error -0x%04X)", unsigned(-e->esp_tls_stack_err));
        s += buf;
      }
      if (e->esp_transport_sock_errno != 0) s += " (errno " + std::to_string(e->esp_transport_sock_errno) + ")";
      return s;
    }
    case MQTT_ERROR_TYPE_CONNECTION_REFUSED:
      switch (e->connect_return_code) {
        case MQTT_CONNECTION_REFUSE_BAD_USERNAME:
        case MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED:
          return "broker refused the connection: not authorised (check username and password)";
        case MQTT_CONNECTION_REFUSE_ID_REJECTED:
          return "broker refused the client ID";
        case MQTT_CONNECTION_REFUSE_SERVER_UNAVAILABLE:
          return "broker unavailable";
        default:
          return "broker refused the connection (code " + std::to_string(e->connect_return_code) + ")";
      }
    default:
      return "error type " + std::to_string(e->error_type);
  }
}

void MqttLink::handle_event_(esp_mqtt_event_handle_t event) {
  if (event->client != client_) return;  // a client that is being torn down
  switch (event->event_id) {
    case MQTT_EVENT_BEFORE_CONNECT:
      state_ = State::CONNECTING;
      break;
    case MQTT_EVENT_CONNECTED:
      state_ = State::CONNECTED;
      {
        LockGuard lock(mutex_);
        last_error_.clear();
      }
      push_(Event{Event::CONNECTED, "", ""});
      break;
    case MQTT_EVENT_DISCONNECTED:
      if (state_ == State::CONNECTED) push_(Event{Event::DISCONNECTED, "", ""});
      state_ = State::DISCONNECTED;
      break;
    case MQTT_EVENT_ERROR:
      set_error_(describe_error(event->error_handle));
      break;
    case MQTT_EVENT_DATA:
      // Large payloads arrive in chunks; commands are small, but assemble anyway.
      if (event->current_data_offset == 0) {
        partial_topic_.assign(event->topic, event->topic_len);
        partial_payload_.clear();
      }
      partial_payload_.append(event->data, event->data_len);
      if (event->current_data_offset + event->data_len >= event->total_data_len) {
        push_(Event{Event::MESSAGE, partial_topic_, partial_payload_});
      }
      break;
    default:
      break;
  }
}

}  // namespace jarolift
}  // namespace esphome

#endif

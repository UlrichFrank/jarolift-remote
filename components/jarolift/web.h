#pragma once

#include <string>

namespace esphome {
namespace jarolift {

class JaroliftComponent;

// Starts the web interface: HTTPS on 443 (basic auth) and a redirect-only HTTP server on 80.
void setup_web(JaroliftComponent *hub, const std::string &username, const std::string &password);

}  // namespace jarolift
}  // namespace esphome

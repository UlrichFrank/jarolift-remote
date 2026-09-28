#pragma once

#include <cstdint>
#include <string>

namespace esphome {
namespace jarolift {

// Network mask: valid IPv4 whose one-bits are contiguous from the top (0.0.0.0 excluded).
bool parse_netmask(const std::string &text, uint32_t *out);

// Hex number with optional 0x prefix, at most `bits` wide and at most bits/4 digits.
bool parse_hex(const std::string &text, int bits, uint32_t *out);

// Formats as 0x-prefixed, zero-padded hex with bits/4 digits.
std::string format_hex(uint32_t value, int bits);

// One or more PEM certificates ("-----BEGIN CERTIFICATE-----" ... base64 ... "-----END CERTIFICATE-----").
bool is_valid_pem_certificate(const std::string &text);

}  // namespace jarolift
}  // namespace esphome

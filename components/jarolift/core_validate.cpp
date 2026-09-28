#include "core_validate.h"

#include <cctype>
#include <cstdio>

#include "core_url.h"

namespace esphome {
namespace jarolift {

bool parse_netmask(const std::string &text, uint32_t *out) {
  uint32_t mask;
  if (!parse_ipv4(text, &mask) || mask == 0) return false;
  uint32_t inverted = ~mask;
  if ((inverted & (inverted + 1)) != 0) return false;  // ones must be contiguous from the top
  *out = mask;
  return true;
}

bool parse_hex(const std::string &text, int bits, uint32_t *out) {
  std::string digits = text;
  if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) digits = digits.substr(2);
  if (digits.empty() || digits.size() > size_t(bits / 4)) return false;
  uint32_t value = 0;
  for (char c : digits) {
    if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10;
    value = (value << 4) | uint32_t(d);
  }
  *out = value;
  return true;
}

std::string format_hex(uint32_t value, int bits) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "0x%0*x", bits / 4, value);
  return buf;
}

bool is_valid_pem_certificate(const std::string &text) {
  static const std::string BEGIN = "-----BEGIN CERTIFICATE-----";
  static const std::string END = "-----END CERTIFICATE-----";
  size_t pos = 0;
  int count = 0;
  while (true) {
    size_t begin = text.find(BEGIN, pos);
    if (begin == std::string::npos) break;
    // Only whitespace is allowed between certificates.
    for (size_t i = pos; i < begin; i++)
      if (!std::isspace(static_cast<unsigned char>(text[i]))) return false;
    size_t body = begin + BEGIN.size();
    size_t end = text.find(END, body);
    if (end == std::string::npos) return false;
    size_t chars = 0;
    for (size_t i = body; i < end; i++) {
      unsigned char c = static_cast<unsigned char>(text[i]);
      if (std::isspace(c)) continue;
      if (!std::isalnum(c) && c != '+' && c != '/' && c != '=') return false;
      chars++;
    }
    if (chars == 0 || chars % 4 != 0) return false;
    pos = end + END.size();
    count++;
  }
  for (size_t i = pos; i < text.size(); i++)
    if (!std::isspace(static_cast<unsigned char>(text[i]))) return false;
  return count > 0;
}

}  // namespace jarolift
}  // namespace esphome

#pragma once

#include <cstdint>

namespace esphome {
namespace jarolift {

// 64-bit Keeloq key split as in the original dongle: MSB = upper 32 bits.
struct KeeloqKey {
  uint32_t msb;
  uint32_t lsb;
};

uint32_t keeloq_encrypt(uint32_t data, const KeeloqKey &key);
uint32_t keeloq_decrypt(uint32_t data, const KeeloqKey &key);

// Normal key generation (Microchip AN745, appendix G): device key for a serial.
KeeloqKey keeloq_device_key(const KeeloqKey &master, uint32_t serial);

}  // namespace jarolift
}  // namespace esphome

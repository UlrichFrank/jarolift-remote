#include "core_keeloq.h"

namespace esphome {
namespace jarolift {

static constexpr uint32_t KEELOQ_NLF = 0x3A5C742E;
static constexpr int KEELOQ_ROUNDS = 528;

static inline uint32_t bit(uint32_t value, int n) { return (value >> n) & 1u; }

static inline uint32_t key_bit(const KeeloqKey &key, int n) {
  return n < 32 ? bit(key.lsb, n) : bit(key.msb, n - 32);
}

uint32_t keeloq_encrypt(uint32_t data, const KeeloqKey &key) {
  uint32_t x = data;
  for (int r = 0; r < KEELOQ_ROUNDS; r++) {
    int index = bit(x, 1) | bit(x, 9) << 1 | bit(x, 20) << 2 | bit(x, 26) << 3 | bit(x, 31) << 4;
    uint32_t b = bit(x, 0) ^ bit(x, 16) ^ bit(KEELOQ_NLF, index) ^ key_bit(key, r & 63);
    x = (x >> 1) ^ (b << 31);
  }
  return x;
}

uint32_t keeloq_decrypt(uint32_t data, const KeeloqKey &key) {
  uint32_t x = data;
  for (int r = 0; r < KEELOQ_ROUNDS; r++) {
    int index = bit(x, 0) | bit(x, 8) << 1 | bit(x, 19) << 2 | bit(x, 25) << 3 | bit(x, 30) << 4;
    uint32_t b = bit(x, 31) ^ bit(x, 15) ^ bit(KEELOQ_NLF, index) ^ key_bit(key, (15 - r) & 63);
    x = (x << 1) ^ b;
  }
  return x;
}

KeeloqKey keeloq_device_key(const KeeloqKey &master, uint32_t serial) {
  return KeeloqKey{keeloq_decrypt(serial | 0x60000000, master), keeloq_decrypt(serial | 0x20000000, master)};
}

}  // namespace jarolift
}  // namespace esphome

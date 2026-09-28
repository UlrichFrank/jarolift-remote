#include <doctest/doctest.h>

#include "core_keeloq.h"
#include "reference/KeeloqLib.h"

using namespace esphome::jarolift;

TEST_CASE("keeloq matches the original KeeloqLib") {
  const KeeloqKey keys[] = {{0x01020304, 0x05060708}, {0x5CEC6701, 0xB79FD949}, {0xFFFFFFFF, 0x00000000}, {0, 0}};
  const uint32_t plains[] = {6623281u, 0xF741E2DB, 0x00000000, 0xFFFFFFFF, 0x12345678, 0x80000001};
  for (const auto &key : keys) {
    Keeloq ref(key.msb, key.lsb);
    for (uint32_t p : plains) {
      CAPTURE(key.msb);
      CAPTURE(p);
      CHECK(keeloq_encrypt(p, key) == static_cast<uint32_t>(ref.encrypt(p)));
      CHECK(keeloq_decrypt(p, key) == static_cast<uint32_t>(ref.decrypt(p)));
    }
  }
}

TEST_CASE("keeloq published test vector") {
  // Key 0x5CEC6701B79FD949, plaintext 0xF741E2DB -> ciphertext 0xE44F4CDF
  CHECK(keeloq_encrypt(0xF741E2DB, {0x5CEC6701, 0xB79FD949}) == 0xE44F4CDF);
}

TEST_CASE("keeloq decrypt inverts encrypt") {
  const KeeloqKey key{0xDEADBEEF, 0x01234567};
  for (uint32_t p = 0; p < 2000; p += 7) CHECK(keeloq_decrypt(keeloq_encrypt(p * 2654435761u, key), key) == p * 2654435761u);
}

TEST_CASE("device key derivation follows AN745 normal learning") {
  const KeeloqKey master{0x01020304, 0x05060708};
  Keeloq ref(master.msb, master.lsb);
  const uint32_t serial = 0x12deaf05;
  KeeloqKey dev = keeloq_device_key(master, serial);
  CHECK(dev.lsb == static_cast<uint32_t>(ref.decrypt(serial | 0x20000000)));
  CHECK(dev.msb == static_cast<uint32_t>(ref.decrypt(serial | 0x60000000)));
}

#include <doctest/doctest.h>

#include "core_validate.h"

using namespace esphome::jarolift;

TEST_CASE("netmask must be contiguous") {
  uint32_t m;
  CHECK(parse_netmask("255.255.255.0", &m));
  CHECK(m == 0xFFFFFF00);
  CHECK(parse_netmask("255.255.254.0", &m));
  CHECK(parse_netmask("255.255.255.255", &m));
  CHECK_FALSE(parse_netmask("255.0.255.0", &m));
  CHECK_FALSE(parse_netmask("0.0.0.0", &m));
  CHECK_FALSE(parse_netmask("255.255.255.1", &m));
  CHECK_FALSE(parse_netmask("255.255.256.0", &m));
}

TEST_CASE("hex values of a given width") {
  uint32_t v;
  CHECK(parse_hex("0x01234567", 32, &v));
  CHECK(v == 0x01234567);
  CHECK(parse_hex("89ABCDEF", 32, &v));
  CHECK(v == 0x89ABCDEF);
  CHECK(parse_hex("0x12deaf", 24, &v));
  CHECK(v == 0x12deaf);
  CHECK_FALSE(parse_hex("0x1234567g", 32, &v));
  CHECK_FALSE(parse_hex("0x123456789", 32, &v));
  CHECK_FALSE(parse_hex("0x1234567", 24, &v));
  CHECK_FALSE(parse_hex("0x", 32, &v));
  CHECK_FALSE(parse_hex("", 24, &v));
  CHECK(format_hex(0x12deaf, 24) == "0x12deaf");
  CHECK(format_hex(0x1, 32) == "0x00000001");
}

static const char *CERT =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBszCCAVmgAwIBAgIUQ2Vy\n"
    "dGlmaWNhdGVib2R5AAAA\n"
    "-----END CERTIFICATE-----\n";

TEST_CASE("PEM certificates") {
  CHECK(is_valid_pem_certificate(CERT));
  CHECK(is_valid_pem_certificate(std::string(CERT) + "\n" + CERT));
  CHECK_FALSE(is_valid_pem_certificate(""));
  CHECK_FALSE(is_valid_pem_certificate("hello"));
  CHECK_FALSE(is_valid_pem_certificate("-----BEGIN CERTIFICATE-----\nabc!\n-----END CERTIFICATE-----\n"));
  CHECK_FALSE(is_valid_pem_certificate("-----BEGIN CERTIFICATE-----\nMIIB\n"));
  CHECK_FALSE(is_valid_pem_certificate("-----BEGIN CERTIFICATE-----\n-----END CERTIFICATE-----\n"));
  CHECK_FALSE(is_valid_pem_certificate("-----BEGIN PRIVATE KEY-----\nMIIB\n-----END PRIVATE KEY-----\n"));
  CHECK_FALSE(is_valid_pem_certificate(std::string("junk") + CERT));
}

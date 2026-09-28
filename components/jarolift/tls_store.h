#pragma once

#ifdef USE_ESP32

#include <string>

namespace esphome {
namespace jarolift {

struct CertificateInfo {
  std::string subject;
  std::string issuer;
  std::string not_after;  // "YYYY-MM-DD"
  bool self_signed{false};
};

// Certificate chain and private key of the HTTPS server, kept in NVS.
class TlsStore {
 public:
  // Loads the stored pair, or generates and stores a self-signed ECDSA P-256 certificate if none
  // is stored or the stored one does not validate.
  bool begin(const std::string &common_name);

  // Validates a PEM chain and key (parse, key matches the first certificate) and stores them.
  // On failure `error` names the problem and the current pair stays.
  bool install(const std::string &chain_pem, const std::string &key_pem, std::string *error);

  const std::string &certificate() const { return cert_; }
  const std::string &private_key() const { return key_; }
  const CertificateInfo &info() const { return info_; }

 protected:
  bool generate_self_signed_(const std::string &common_name);
  bool save_();

  std::string cert_;
  std::string key_;
  CertificateInfo info_;
};

// Validates a pair without storing it; fills `info` from the first certificate.
bool validate_certificate_pair(const std::string &chain_pem, const std::string &key_pem, CertificateInfo *info,
                               std::string *error);

}  // namespace jarolift
}  // namespace esphome

#endif

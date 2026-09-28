#include "tls_store.h"

#ifdef USE_ESP32

#include <cstring>
#include <memory>

#include <esp_random.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>
#include <nvs.h>

#include "esphome/core/log.h"

namespace esphome {
namespace jarolift {

static const char *const TAG = "jarolift.tls";
static const char *const NS = "jarolift_tls";

namespace {

// RAII bundle of the mbedTLS random generator.
struct Rng {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;
  bool ok;
  Rng() {
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    const char pers[] = "jarolift-tls";
    ok = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, reinterpret_cast<const unsigned char *>(pers),
                               sizeof(pers)) == 0;
  }
  ~Rng() {
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
  }
};

std::string dn_string(const mbedtls_x509_name *dn) {
  char buf[256];
  int n = mbedtls_x509_dn_gets(buf, sizeof(buf), dn);
  return n > 0 ? std::string(buf, size_t(n)) : std::string();
}

bool nvs_read(nvs_handle_t h, const char *key, std::string *out) {
  size_t len = 0;
  if (nvs_get_blob(h, key, nullptr, &len) != ESP_OK || len == 0) return false;
  std::string buf(len, '\0');
  if (nvs_get_blob(h, key, &buf[0], &len) != ESP_OK) return false;
  *out = buf;
  return true;
}

}  // namespace

bool validate_certificate_pair(const std::string &chain_pem, const std::string &key_pem, CertificateInfo *info,
                               std::string *error) {
  if (chain_pem.find("-----BEGIN CERTIFICATE-----") == std::string::npos) {
    *error = "certificate is not PEM";
    return false;
  }
  if (key_pem.find("PRIVATE KEY-----") == std::string::npos) {
    *error = "private key is not PEM";
    return false;
  }
  Rng rng;
  mbedtls_x509_crt crt;
  mbedtls_pk_context key;
  mbedtls_x509_crt_init(&crt);
  mbedtls_pk_init(&key);
  bool ok = false;
  int rc = mbedtls_x509_crt_parse(&crt, reinterpret_cast<const unsigned char *>(chain_pem.c_str()), chain_pem.size() + 1);
  if (rc != 0) {
    *error = "certificate does not parse (mbedTLS -0x" + std::to_string(-rc) + ")";
  } else if ((rc = mbedtls_pk_parse_key(&key, reinterpret_cast<const unsigned char *>(key_pem.c_str()),
                                        key_pem.size() + 1, nullptr, 0, mbedtls_ctr_drbg_random, &rng.drbg)) != 0) {
    *error = "private key does not parse (mbedTLS -0x" + std::to_string(-rc) + ")";
  } else if (mbedtls_pk_check_pair(&crt.pk, &key, mbedtls_ctr_drbg_random, &rng.drbg) != 0) {
    *error = "private key does not belong to the certificate";
  } else {
    info->subject = dn_string(&crt.subject);
    info->issuer = dn_string(&crt.issuer);
    char date[16];
    snprintf(date, sizeof(date), "%04d-%02d-%02d", crt.valid_to.year, crt.valid_to.mon, crt.valid_to.day);
    info->not_after = date;
    info->self_signed = crt.subject_raw.len == crt.issuer_raw.len &&
                        memcmp(crt.subject_raw.p, crt.issuer_raw.p, crt.subject_raw.len) == 0;
    ok = true;
  }
  mbedtls_pk_free(&key);
  mbedtls_x509_crt_free(&crt);
  return ok;
}

bool TlsStore::begin(const std::string &common_name) {
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
    std::string cert, key, error;
    bool loaded = nvs_read(h, "cert", &cert) && nvs_read(h, "key", &key);
    nvs_close(h);
    if (loaded && validate_certificate_pair(cert, key, &info_, &error)) {
      cert_ = cert;
      key_ = key;
      ESP_LOGI(TAG, "Certificate: %s, issued by %s, valid until %s", info_.subject.c_str(), info_.issuer.c_str(),
               info_.not_after.c_str());
      return true;
    }
    if (loaded) ESP_LOGW(TAG, "Stored certificate rejected: %s", error.c_str());
  }
  ESP_LOGI(TAG, "Generating a self-signed certificate");
  return generate_self_signed_(common_name) && save_();
}

bool TlsStore::install(const std::string &chain_pem, const std::string &key_pem, std::string *error) {
  CertificateInfo info;
  if (!validate_certificate_pair(chain_pem, key_pem, &info, error)) return false;
  std::string old_cert = cert_, old_key = key_;
  CertificateInfo old_info = info_;
  cert_ = chain_pem;
  key_ = key_pem;
  info_ = info;
  if (!save_()) {
    cert_ = old_cert;
    key_ = old_key;
    info_ = old_info;
    *error = "certificate could not be stored";
    return false;
  }
  ESP_LOGI(TAG, "Installed certificate: %s, issued by %s, valid until %s", info_.subject.c_str(),
           info_.issuer.c_str(), info_.not_after.c_str());
  return true;
}

bool TlsStore::save_() {
  nvs_handle_t h;
  esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
  if (err == ESP_OK) err = nvs_set_blob(h, "cert", cert_.c_str(), cert_.size());
  if (err == ESP_OK) err = nvs_set_blob(h, "key", key_.c_str(), key_.size());
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) ESP_LOGE(TAG, "Saving the certificate failed: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

bool TlsStore::generate_self_signed_(const std::string &common_name) {
  Rng rng;
  if (!rng.ok) return false;
  mbedtls_pk_context key;
  mbedtls_x509write_cert crt;
  mbedtls_pk_init(&key);
  mbedtls_x509write_crt_init(&crt);
  auto buf = std::make_unique<unsigned char[]>(2048);
  bool ok = false;
  do {
    if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0) break;
    if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), mbedtls_ctr_drbg_random, &rng.drbg) != 0)
      break;
    if (mbedtls_pk_write_key_pem(&key, buf.get(), 2048) != 0) break;
    std::string key_pem(reinterpret_cast<char *>(buf.get()));

    std::string dn = "CN=" + common_name + ",O=jarolift-remote (self-signed)";
    unsigned char serial[16];
    esp_fill_random(serial, sizeof(serial));
    serial[0] &= 0x7F;  // positive
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    if (mbedtls_x509write_crt_set_subject_name(&crt, dn.c_str()) != 0) break;
    if (mbedtls_x509write_crt_set_issuer_name(&crt, dn.c_str()) != 0) break;
    if (mbedtls_x509write_crt_set_serial_raw(&crt, serial, sizeof(serial)) != 0) break;
    if (mbedtls_x509write_crt_set_validity(&crt, "20260101000000", "20460101000000") != 0) break;
    if (mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1) != 0) break;
    if (mbedtls_x509write_crt_pem(&crt, buf.get(), 2048, mbedtls_ctr_drbg_random, &rng.drbg) != 0) break;
    std::string cert_pem(reinterpret_cast<char *>(buf.get()));

    std::string error;
    if (!validate_certificate_pair(cert_pem, key_pem, &info_, &error)) {
      ESP_LOGE(TAG, "Generated certificate invalid: %s", error.c_str());
      break;
    }
    cert_ = cert_pem;
    key_ = key_pem;
    ok = true;
  } while (false);
  mbedtls_x509write_crt_free(&crt);
  mbedtls_pk_free(&key);
  if (!ok) ESP_LOGE(TAG, "Generating a self-signed certificate failed");
  return ok;
}

}  // namespace jarolift
}  // namespace esphome

#endif

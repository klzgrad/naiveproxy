// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/x509_util.h"

#include <memory>

#include "base/lazy_instance.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "crypto/openssl_util.h"
#include "crypto/rsa_private_key.h"
#include "net/base/hash_value.h"
#include "net/cert/internal/cert_errors.h"
#include "net/cert/internal/name_constraints.h"
#include "net/cert/internal/parse_certificate.h"
#include "net/cert/internal/parse_name.h"
#include "net/cert/internal/signature_algorithm.h"
#include "net/cert/x509_certificate.h"
#include "net/der/encode_values.h"
#include "net/der/input.h"
#include "net/der/parse_values.h"
#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "third_party/boringssl/src/include/openssl/digest.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/mem.h"
#include "third_party/boringssl/src/include/openssl/pool.h"
#include "third_party/boringssl/src/include/openssl/stack.h"

namespace net {

namespace x509_util {

namespace {

bool AddRSASignatureAlgorithm(CBB* cbb, DigestAlgorithm algorithm) {
  // See RFC 4055.
  static const uint8_t kSHA256WithRSAEncryption[] = {
      0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b};

  // An AlgorithmIdentifier is described in RFC 5280, 4.1.1.2.
  CBB sequence, oid, params;
  if (!CBB_add_asn1(cbb, &sequence, CBS_ASN1_SEQUENCE) ||
      !CBB_add_asn1(&sequence, &oid, CBS_ASN1_OBJECT)) {
    return false;
  }

  switch (algorithm) {
    case DIGEST_SHA256:
      if (!CBB_add_bytes(&oid, kSHA256WithRSAEncryption,
                         sizeof(kSHA256WithRSAEncryption)))
        return false;
      break;
  }

  // All supported algorithms use null parameters.
  if (!CBB_add_asn1(&sequence, &params, CBS_ASN1_NULL) || !CBB_flush(cbb)) {
    return false;
  }

  return true;
}

const EVP_MD* ToEVP(DigestAlgorithm alg) {
  switch (alg) {
    case DIGEST_SHA256:
      return EVP_sha256();
  }
  return nullptr;
}

// Adds an X.509 Name with the specified common name to |cbb|.
bool AddNameWithCommonName(CBB* cbb, base::StringPiece common_name) {
  // See RFC 4519.
  static const uint8_t kCommonName[] = {0x55, 0x04, 0x03};

  // See RFC 5280, section 4.1.2.4.
  CBB rdns, rdn, attr, type, value;
  if (!CBB_add_asn1(cbb, &rdns, CBS_ASN1_SEQUENCE) ||
      !CBB_add_asn1(&rdns, &rdn, CBS_ASN1_SET) ||
      !CBB_add_asn1(&rdn, &attr, CBS_ASN1_SEQUENCE) ||
      !CBB_add_asn1(&attr, &type, CBS_ASN1_OBJECT) ||
      !CBB_add_bytes(&type, kCommonName, sizeof(kCommonName)) ||
      !CBB_add_asn1(&attr, &value, CBS_ASN1_UTF8STRING) ||
      !CBB_add_bytes(&value,
                     reinterpret_cast<const uint8_t*>(common_name.data()),
                     common_name.size()) ||
      !CBB_flush(cbb)) {
    return false;
  }
  return true;
}

bool AddTime(CBB* cbb, base::Time time) {
  der::GeneralizedTime generalized_time;
  if (!der::EncodeTimeAsGeneralizedTime(time, &generalized_time))
    return false;

  // Per RFC 5280, 4.1.2.5, times which fit in UTCTime must be encoded as
  // UTCTime rather than GeneralizedTime.
  CBB child;
  uint8_t* out;
  if (generalized_time.InUTCTimeRange()) {
    return CBB_add_asn1(cbb, &child, CBS_ASN1_UTCTIME) &&
           CBB_add_space(&child, &out, der::kUTCTimeLength) &&
           der::EncodeUTCTime(generalized_time, out) && CBB_flush(cbb);
  }

  return CBB_add_asn1(cbb, &child, CBS_ASN1_GENERALIZEDTIME) &&
         CBB_add_space(&child, &out, der::kGeneralizedTimeLength) &&
         der::EncodeGeneralizedTime(generalized_time, out) && CBB_flush(cbb);
}

bool GetCommonName(const der::Input& tlv, std::string* common_name) {
  RDNSequence rdn_sequence;
  if (!ParseName(tlv, &rdn_sequence))
    return false;

  for (const auto& rdn : rdn_sequence) {
    for (const auto& atv : rdn) {
      if (atv.type == TypeCommonNameOid()) {
        return atv.ValueAsString(common_name);
      }
    }
  }
  return true;
}

bool DecodeTime(const der::GeneralizedTime& generalized_time,
                base::Time* time) {
  base::Time::Exploded exploded = {0};
  exploded.year = generalized_time.year;
  exploded.month = generalized_time.month;
  exploded.day_of_month = generalized_time.day;
  exploded.hour = generalized_time.hours;
  exploded.minute = generalized_time.minutes;
  exploded.second = generalized_time.seconds;
  return base::Time::FromUTCExploded(exploded, time);
}

class BufferPoolSingleton {
 public:
  BufferPoolSingleton() : pool_(CRYPTO_BUFFER_POOL_new()) {}
  CRYPTO_BUFFER_POOL* pool() { return pool_; }

 private:
  // The singleton is leaky, so there is no need to use a smart pointer.
  CRYPTO_BUFFER_POOL* pool_;
};

base::LazyInstance<BufferPoolSingleton>::Leaky g_buffer_pool_singleton =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

bool GetTLSServerEndPointChannelBinding(const X509Certificate& certificate,
                                        std::string* token) {
  static const char kChannelBindingPrefix[] = "tls-server-end-point:";

  std::string der_encoded_certificate;
  if (!X509Certificate::GetDEREncoded(certificate.os_cert_handle(),
                                      &der_encoded_certificate))
    return false;

  der::Input tbs_certificate_tlv;
  der::Input signature_algorithm_tlv;
  der::BitString signature_value;
  if (!ParseCertificate(der::Input(&der_encoded_certificate),
                        &tbs_certificate_tlv, &signature_algorithm_tlv,
                        &signature_value, nullptr))
    return false;

  std::unique_ptr<SignatureAlgorithm> signature_algorithm =
      SignatureAlgorithm::Create(signature_algorithm_tlv, nullptr);
  if (!signature_algorithm)
    return false;

  const EVP_MD* digest_evp_md = nullptr;
  switch (signature_algorithm->digest()) {
    case net::DigestAlgorithm::Md2:
    case net::DigestAlgorithm::Md4:
      // Shouldn't be reachable.
      digest_evp_md = nullptr;
      break;

    // Per RFC 5929 section 4.1, MD5 and SHA1 map to SHA256.
    case net::DigestAlgorithm::Md5:
    case net::DigestAlgorithm::Sha1:
    case net::DigestAlgorithm::Sha256:
      digest_evp_md = EVP_sha256();
      break;

    case net::DigestAlgorithm::Sha384:
      digest_evp_md = EVP_sha384();
      break;

    case net::DigestAlgorithm::Sha512:
      digest_evp_md = EVP_sha512();
      break;
  }
  if (!digest_evp_md)
    return false;

  uint8_t digest[EVP_MAX_MD_SIZE];
  unsigned int out_size;
  if (!EVP_Digest(der_encoded_certificate.data(),
                  der_encoded_certificate.size(), digest, &out_size,
                  digest_evp_md, nullptr))
    return false;

  token->assign(kChannelBindingPrefix);
  token->append(digest, digest + out_size);
  return true;
}

// RSA keys created by CreateKeyAndSelfSignedCert will be of this length.
static const uint16_t kRSAKeyLength = 1024;

// Certificates made by CreateKeyAndSelfSignedCert will be signed using this
// digest algorithm.
static const DigestAlgorithm kSignatureDigestAlgorithm = DIGEST_SHA256;

bool CreateKeyAndSelfSignedCert(const std::string& subject,
                                uint32_t serial_number,
                                base::Time not_valid_before,
                                base::Time not_valid_after,
                                std::unique_ptr<crypto::RSAPrivateKey>* key,
                                std::string* der_cert) {
  std::unique_ptr<crypto::RSAPrivateKey> new_key(
      crypto::RSAPrivateKey::Create(kRSAKeyLength));
  if (!new_key.get())
    return false;

  bool success = CreateSelfSignedCert(new_key.get(),
                                      kSignatureDigestAlgorithm,
                                      subject,
                                      serial_number,
                                      not_valid_before,
                                      not_valid_after,
                                      der_cert);
  if (success)
    *key = std::move(new_key);

  return success;
}

bool CreateSelfSignedCert(crypto::RSAPrivateKey* key,
                          DigestAlgorithm alg,
                          const std::string& subject,
                          uint32_t serial_number,
                          base::Time not_valid_before,
                          base::Time not_valid_after,
                          std::string* der_encoded) {
  crypto::EnsureOpenSSLInit();
  crypto::OpenSSLErrStackTracer err_tracer(FROM_HERE);

  // Because |subject| only contains a common name and starts with 'CN=', there
  // is no need for a full RFC 2253 parser here. Do some sanity checks though.
  static const char kCommonNamePrefix[] = "CN=";
  if (!base::StartsWith(subject, kCommonNamePrefix,
                        base::CompareCase::SENSITIVE)) {
    LOG(ERROR) << "Subject must begin with " << kCommonNamePrefix;
    return false;
  }
  base::StringPiece common_name = subject;
  common_name.remove_prefix(sizeof(kCommonNamePrefix) - 1);

  // See RFC 5280, section 4.1. First, construct the TBSCertificate.
  bssl::ScopedCBB cbb;
  CBB tbs_cert, version, validity;
  uint8_t* tbs_cert_bytes;
  size_t tbs_cert_len;
  if (!CBB_init(cbb.get(), 64) ||
      !CBB_add_asn1(cbb.get(), &tbs_cert, CBS_ASN1_SEQUENCE) ||
      !CBB_add_asn1(&tbs_cert, &version,
                    CBS_ASN1_CONTEXT_SPECIFIC | CBS_ASN1_CONSTRUCTED | 0) ||
      !CBB_add_asn1_uint64(&version, 2) ||
      !CBB_add_asn1_uint64(&tbs_cert, serial_number) ||
      !AddRSASignatureAlgorithm(&tbs_cert, alg) ||       // signature
      !AddNameWithCommonName(&tbs_cert, common_name) ||  // issuer
      !CBB_add_asn1(&tbs_cert, &validity, CBS_ASN1_SEQUENCE) ||
      !AddTime(&validity, not_valid_before) ||
      !AddTime(&validity, not_valid_after) ||
      !AddNameWithCommonName(&tbs_cert, common_name) ||  // subject
      !EVP_marshal_public_key(&tbs_cert, key->key()) ||  // subjectPublicKeyInfo
      !CBB_finish(cbb.get(), &tbs_cert_bytes, &tbs_cert_len)) {
    return false;
  }
  bssl::UniquePtr<uint8_t> delete_tbs_cert_bytes(tbs_cert_bytes);

  // Sign the TBSCertificate and write the entire certificate.
  CBB cert, signature;
  bssl::ScopedEVP_MD_CTX ctx;
  uint8_t* sig_out;
  size_t sig_len;
  uint8_t* cert_bytes;
  size_t cert_len;
  if (!CBB_init(cbb.get(), tbs_cert_len) ||
      !CBB_add_asn1(cbb.get(), &cert, CBS_ASN1_SEQUENCE) ||
      !CBB_add_bytes(&cert, tbs_cert_bytes, tbs_cert_len) ||
      !AddRSASignatureAlgorithm(&cert, alg) ||
      !CBB_add_asn1(&cert, &signature, CBS_ASN1_BITSTRING) ||
      !CBB_add_u8(&signature, 0 /* no unused bits */) ||
      !EVP_DigestSignInit(ctx.get(), nullptr, ToEVP(alg), nullptr,
                          key->key()) ||
      // Compute the maximum signature length.
      !EVP_DigestSign(ctx.get(), nullptr, &sig_len, tbs_cert_bytes,
                      tbs_cert_len) ||
      !CBB_reserve(&signature, &sig_out, sig_len) ||
      // Actually sign the TBSCertificate.
      !EVP_DigestSign(ctx.get(), sig_out, &sig_len, tbs_cert_bytes,
                      tbs_cert_len) ||
      !CBB_did_write(&signature, sig_len) ||
      !CBB_finish(cbb.get(), &cert_bytes, &cert_len)) {
    return false;
  }
  bssl::UniquePtr<uint8_t> delete_cert_bytes(cert_bytes);
  der_encoded->assign(reinterpret_cast<char*>(cert_bytes), cert_len);
  return true;
}

bool ParseCertificateSandboxed(const base::StringPiece& certificate,
                               std::string* subject,
                               std::string* issuer,
                               base::Time* not_before,
                               base::Time* not_after,
                               std::vector<std::string>* dns_names,
                               std::vector<std::string>* ip_addresses) {
  der::Input cert_data(certificate);
  der::Input tbs_cert, signature_alg;
  der::BitString signature_value;
  if (!ParseCertificate(cert_data, &tbs_cert, &signature_alg, &signature_value,
                        nullptr))
    return false;

  ParsedTbsCertificate parsed_tbs_cert;
  if (!ParseTbsCertificate(tbs_cert, DefaultParseCertificateOptions(),
                           &parsed_tbs_cert, nullptr))
    return false;

  if (!GetCommonName(parsed_tbs_cert.subject_tlv, subject))
    return false;

  if (!GetCommonName(parsed_tbs_cert.issuer_tlv, issuer))
    return false;

  if (!DecodeTime(parsed_tbs_cert.validity_not_before, not_before))
    return false;

  if (!DecodeTime(parsed_tbs_cert.validity_not_after, not_after))
    return false;

  if (!parsed_tbs_cert.has_extensions)
    return true;

  std::map<der::Input, ParsedExtension> extensions;
  if (!ParseExtensions(parsed_tbs_cert.extensions_tlv, &extensions))
    return false;

  CertErrors unused_errors;
  std::vector<std::string> san;
  auto iter = extensions.find(SubjectAltNameOid());
  if (iter != extensions.end()) {
    std::unique_ptr<GeneralNames> subject_alt_names =
        GeneralNames::Create(iter->second.value, &unused_errors);
    if (subject_alt_names) {
      for (const auto& dns_name : subject_alt_names->dns_names)
        dns_names->push_back(dns_name.as_string());
      for (const auto& ip : subject_alt_names->ip_addresses)
        ip_addresses->push_back(ip.ToString());
    }
  }

  return true;
}

CRYPTO_BUFFER_POOL* GetBufferPool() {
  return g_buffer_pool_singleton.Get().pool();
}

bssl::UniquePtr<CRYPTO_BUFFER> CreateCryptoBuffer(const uint8_t* data,
                                                  size_t length) {
  return bssl::UniquePtr<CRYPTO_BUFFER>(
      CRYPTO_BUFFER_new(data, length, GetBufferPool()));
}

bssl::UniquePtr<CRYPTO_BUFFER> CreateCryptoBuffer(
    const base::StringPiece& data) {
  return bssl::UniquePtr<CRYPTO_BUFFER>(
      CRYPTO_BUFFER_new(reinterpret_cast<const uint8_t*>(data.data()),
                        data.size(), GetBufferPool()));
}

base::StringPiece CryptoBufferAsStringPiece(const CRYPTO_BUFFER* buffer) {
  return base::StringPiece(
      reinterpret_cast<const char*>(CRYPTO_BUFFER_data(buffer)),
      CRYPTO_BUFFER_len(buffer));
}

scoped_refptr<X509Certificate> CreateX509CertificateFromBuffers(
    STACK_OF(CRYPTO_BUFFER) * buffers) {
  if (sk_CRYPTO_BUFFER_num(buffers) == 0) {
    NOTREACHED();
    return nullptr;
  }

  std::vector<CRYPTO_BUFFER*> intermediate_chain;
  for (size_t i = 1; i < sk_CRYPTO_BUFFER_num(buffers); ++i)
    intermediate_chain.push_back(sk_CRYPTO_BUFFER_value(buffers, i));
  return X509Certificate::CreateFromHandle(sk_CRYPTO_BUFFER_value(buffers, 0),
                                           intermediate_chain);
}

ParseCertificateOptions DefaultParseCertificateOptions() {
  ParseCertificateOptions options;
  options.allow_invalid_serial_numbers = true;
  return options;
}

}  // namespace x509_util

}  // namespace net

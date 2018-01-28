// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/ssl_config.h"

#include "net/cert/cert_verifier.h"

namespace net {

const uint16_t kDefaultSSLVersionMin = SSL_PROTOCOL_VERSION_TLS1;

const uint16_t kDefaultSSLVersionMax = SSL_PROTOCOL_VERSION_TLS1_2;

const TLS13Variant kDefaultTLS13Variant = kTLS13VariantDraft;

SSLConfig::CertAndStatus::CertAndStatus() = default;
SSLConfig::CertAndStatus::CertAndStatus(scoped_refptr<X509Certificate> cert_arg,
                                        CertStatus status)
    : cert(std::move(cert_arg)), cert_status(status) {}
SSLConfig::CertAndStatus::CertAndStatus(const CertAndStatus& other)
    : cert(other.cert), cert_status(other.cert_status) {}
SSLConfig::CertAndStatus::~CertAndStatus() = default;

SSLConfig::SSLConfig()
    : rev_checking_enabled(false),
      rev_checking_required_local_anchors(false),
      sha1_local_anchors_enabled(true),
      common_name_fallback_local_anchors_enabled(true),
      version_min(kDefaultSSLVersionMin),
      version_max(kDefaultSSLVersionMax),
      tls13_variant(kDefaultTLS13Variant),
      version_interference_probe(false),
      channel_id_enabled(true),
      false_start_enabled(true),
      signed_cert_timestamps_enabled(true),
      require_ecdhe(false),
      send_client_cert(false),
      verify_ev_cert(false),
      cert_io_enabled(true),
      renego_allowed_default(false) {}

SSLConfig::SSLConfig(const SSLConfig& other) = default;

SSLConfig::~SSLConfig() {}

bool SSLConfig::IsAllowedBadCert(X509Certificate* cert,
                                 CertStatus* cert_status) const {
  for (const auto& allowed_bad_cert : allowed_bad_certs) {
    if (cert->Equals(allowed_bad_cert.cert.get())) {
      if (cert_status)
        *cert_status = allowed_bad_cert.cert_status;
      return true;
    }
  }
  return false;
}

int SSLConfig::GetCertVerifyFlags() const {
  int flags = 0;
  if (rev_checking_enabled)
    flags |= CertVerifier::VERIFY_REV_CHECKING_ENABLED;
  if (verify_ev_cert)
    flags |= CertVerifier::VERIFY_EV_CERT;
  if (cert_io_enabled)
    flags |= CertVerifier::VERIFY_CERT_IO_ENABLED;
  if (rev_checking_required_local_anchors)
    flags |= CertVerifier::VERIFY_REV_CHECKING_REQUIRED_LOCAL_ANCHORS;
  if (sha1_local_anchors_enabled)
    flags |= CertVerifier::VERIFY_ENABLE_SHA1_LOCAL_ANCHORS;
  if (common_name_fallback_local_anchors_enabled)
    flags |= CertVerifier::VERIFY_ENABLE_COMMON_NAME_FALLBACK_LOCAL_ANCHORS;
  return flags;
}

}  // namespace net

// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CERTIFICATE_UTIL_H_
#define QUICHE_QUIC_CORE_CRYPTO_CERTIFICATE_UTIL_H_

#include <string>

#include "absl/strings/string_view.h"
#include "openssl/evp.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

struct QUIC_NO_EXPORT CertificateTimestamp {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

struct QUIC_NO_EXPORT CertificateOptions {
  absl::string_view subject;
  uint64_t serial_number;
  CertificateTimestamp validity_start;  // a.k.a not_valid_before
  CertificateTimestamp validity_end;    // a.k.a not_valid_after
};

// Creates a ECDSA P-256 key pair.
QUIC_EXPORT_PRIVATE bssl::UniquePtr<EVP_PKEY>
MakeKeyPairForSelfSignedCertificate();

// Creates a self-signed, DER-encoded X.509 certificate.
// |key| must be a ECDSA P-256 key.
// This is mostly stolen from Chromium's net/cert/x509_util.h, with
// modifications to make it work in QUICHE.
QUIC_EXPORT_PRIVATE std::string CreateSelfSignedCertificate(
    EVP_PKEY& key, const CertificateOptions& options);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CERTIFICATE_UTIL_H_

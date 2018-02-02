// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_X509_UTIL_H_
#define NET_CERT_X509_UTIL_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "base/strings/string_piece.h"
#include "base/time/time.h"
#include "net/base/net_export.h"
#include "third_party/boringssl/src/include/openssl/pool.h"

namespace crypto {
class RSAPrivateKey;
}

namespace net {

struct ParseCertificateOptions;
class X509Certificate;

namespace x509_util {

// Supported digest algorithms for signing certificates.
enum DigestAlgorithm {
  DIGEST_SHA256
};

// Generate a 'tls-server-end-point' channel binding based on the specified
// certificate. Channel bindings are based on RFC 5929.
NET_EXPORT_PRIVATE bool GetTLSServerEndPointChannelBinding(
    const X509Certificate& certificate,
    std::string* token);

// Creates a public-private keypair and a self-signed certificate.
// Subject, serial number and validity period are given as parameters.
// The certificate is signed by the private key in |key|. The key length and
// signature algorithm may be updated periodically to match best practices.
//
// |subject| is a distinguished name defined in RFC4514 with _only_ a CN
// component, as in:
//   CN=Michael Wong
//
// SECURITY WARNING
//
// Using self-signed certificates has the following security risks:
// 1. Encryption without authentication and thus vulnerable to
//    man-in-the-middle attacks.
// 2. Self-signed certificates cannot be revoked.
//
// Use this certificate only after the above risks are acknowledged.
NET_EXPORT bool CreateKeyAndSelfSignedCert(
    const std::string& subject,
    uint32_t serial_number,
    base::Time not_valid_before,
    base::Time not_valid_after,
    std::unique_ptr<crypto::RSAPrivateKey>* key,
    std::string* der_cert);

// Creates a self-signed certificate from a provided key, using the specified
// hash algorithm.
NET_EXPORT bool CreateSelfSignedCert(crypto::RSAPrivateKey* key,
                                     DigestAlgorithm alg,
                                     const std::string& subject,
                                     uint32_t serial_number,
                                     base::Time not_valid_before,
                                     base::Time not_valid_after,
                                     std::string* der_cert);

// Provides a method to parse a DER-encoded X509 certificate without calling any
// OS primitives. This is useful in sandboxed processes.
NET_EXPORT bool ParseCertificateSandboxed(
    const base::StringPiece& certificate,
    std::string* subject,
    std::string* issuer,
    base::Time* not_before,
    base::Time* not_after,
    std::vector<std::string>* dns_names,
    std::vector<std::string>* ip_addresses);

// Returns a CRYPTO_BUFFER_POOL for deduplicating certificates.
NET_EXPORT CRYPTO_BUFFER_POOL* GetBufferPool();

// Creates a CRYPTO_BUFFER in the same pool returned by GetBufferPool.
NET_EXPORT bssl::UniquePtr<CRYPTO_BUFFER> CreateCryptoBuffer(
    const uint8_t* data,
    size_t length);

// Creates a CRYPTO_BUFFER in the same pool returned by GetBufferPool.
NET_EXPORT bssl::UniquePtr<CRYPTO_BUFFER> CreateCryptoBuffer(
    const base::StringPiece& data);

// Overload with no definition, to disallow creating a CRYPTO_BUFFER from a
// char* due to StringPiece implicit ctor.
NET_EXPORT bssl::UniquePtr<CRYPTO_BUFFER> CreateCryptoBuffer(
    const char* invalid_data);

// Returns a StringPiece pointing to the data in |buffer|.
NET_EXPORT base::StringPiece CryptoBufferAsStringPiece(
    const CRYPTO_BUFFER* buffer);

// Creates a new X509Certificate from the chain in |buffers|, which must have at
// least one element.
scoped_refptr<X509Certificate> CreateX509CertificateFromBuffers(
    STACK_OF(CRYPTO_BUFFER) * buffers);

// Returns the default ParseCertificateOptions for the net stack.
ParseCertificateOptions DefaultParseCertificateOptions();

} // namespace x509_util

} // namespace net

#endif  // NET_CERT_X509_UTIL_H_

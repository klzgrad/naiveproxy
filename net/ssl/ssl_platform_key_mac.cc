// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/ssl_platform_key_mac.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/SecBase.h>
#include <Security/SecCertificate.h>
#include <Security/SecIdentity.h>
#include <Security/SecKey.h>
#include <Security/cssm.h>

#include <memory>

#include "base/location.h"
#include "base/logging.h"
#include "base/mac/availability.h"
#include "base/mac/foundation_util.h"
#include "base/mac/mac_logging.h"
#include "base/mac/mac_util.h"
#include "base/mac/scoped_cftyperef.h"
#include "base/macros.h"
#include "base/memory/scoped_policy.h"
#include "base/numerics/safe_conversions.h"
#include "base/synchronization/lock.h"
#include "crypto/mac_security_services_lock.h"
#include "crypto/openssl_util.h"
#include "net/base/net_errors.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util_mac.h"
#include "net/ssl/ssl_platform_key_util.h"
#include "net/ssl/ssl_private_key.h"
#include "net/ssl/threaded_ssl_private_key.h"
#include "third_party/boringssl/src/include/openssl/ecdsa.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/mem.h"
#include "third_party/boringssl/src/include/openssl/nid.h"
#include "third_party/boringssl/src/include/openssl/rsa.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"

namespace net {

// CSSM functions are deprecated as of OSX 10.7, but have no replacement.
// https://bugs.chromium.org/p/chromium/issues/detail?id=590914#c1
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace {

// TODO(davidben): Remove this when we switch to building to the 10.13
// SDK. https://crbug.com/780980
#if !defined(MAC_OS_X_VERSION_10_13) || \
    MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_13
API_AVAILABLE(macosx(10.13))
const SecKeyAlgorithm kSecKeyAlgorithmRSASignatureDigestPSSSHA256 =
    CFSTR("algid:sign:RSA:digest-PSS:SHA256:SHA256:32");
API_AVAILABLE(macosx(10.13))
const SecKeyAlgorithm kSecKeyAlgorithmRSASignatureDigestPSSSHA384 =
    CFSTR("algid:sign:RSA:digest-PSS:SHA384:SHA384:48");
API_AVAILABLE(macosx(10.13))
const SecKeyAlgorithm kSecKeyAlgorithmRSASignatureDigestPSSSHA512 =
    CFSTR("algid:sign:RSA:digest-PSS:SHA512:SHA512:64");
#endif

class ScopedCSSM_CC_HANDLE {
 public:
  ScopedCSSM_CC_HANDLE() : handle_(0) {}
  explicit ScopedCSSM_CC_HANDLE(CSSM_CC_HANDLE handle) : handle_(handle) {}

  ~ScopedCSSM_CC_HANDLE() { reset(); }

  CSSM_CC_HANDLE get() const { return handle_; }

  void reset() {
    if (handle_)
      CSSM_DeleteContext(handle_);
    handle_ = 0;
  }

 private:
  CSSM_CC_HANDLE handle_;

  DISALLOW_COPY_AND_ASSIGN(ScopedCSSM_CC_HANDLE);
};

class SSLPlatformKeyCSSM : public ThreadedSSLPrivateKey::Delegate {
 public:
  SSLPlatformKeyCSSM(int type,
                     size_t max_length,
                     SecKeyRef key,
                     const CSSM_KEY* cssm_key)
      : type_(type),
        max_length_(max_length),
        key_(key, base::scoped_policy::RETAIN),
        cssm_key_(cssm_key) {}

  ~SSLPlatformKeyCSSM() override {}

  std::vector<uint16_t> GetAlgorithmPreferences() override {
    return SSLPrivateKey::DefaultAlgorithmPreferences(type_,
                                                      false /* no PSS */);
  }

  Error Sign(uint16_t algorithm,
             base::span<const uint8_t> input,
             std::vector<uint8_t>* signature) override {
    crypto::OpenSSLErrStackTracer tracer(FROM_HERE);

    CSSM_CSP_HANDLE csp_handle;
    OSStatus status = SecKeyGetCSPHandle(key_.get(), &csp_handle);
    if (status != noErr) {
      OSSTATUS_LOG(WARNING, status);
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }

    const CSSM_ACCESS_CREDENTIALS* cssm_creds = nullptr;
    status = SecKeyGetCredentials(key_.get(), CSSM_ACL_AUTHORIZATION_SIGN,
                                  kSecCredentialTypeDefault, &cssm_creds);
    if (status != noErr) {
      OSSTATUS_LOG(WARNING, status);
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }

    CSSM_CC_HANDLE cssm_signature_raw = 0;
    if (CSSM_CSP_CreateSignatureContext(
            csp_handle, cssm_key_->KeyHeader.AlgorithmId, cssm_creds, cssm_key_,
            &cssm_signature_raw) != CSSM_OK) {
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }
    ScopedCSSM_CC_HANDLE cssm_signature(cssm_signature_raw);

    // Hash the input.
    const EVP_MD* md = SSL_get_signature_algorithm_digest(algorithm);
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned digest_len;
    if (!md || !EVP_Digest(input.data(), input.size(), digest, &digest_len, md,
                           nullptr)) {
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }
    CSSM_DATA hash_data;
    hash_data.Length = digest_len;
    hash_data.Data = digest;

    bssl::UniquePtr<uint8_t> free_digest_info;
    if (cssm_key_->KeyHeader.AlgorithmId == CSSM_ALGID_RSA) {
      // CSSM expects the caller to prepend the DigestInfo.
      int hash_nid = EVP_MD_type(md);
      int is_alloced;
      if (!RSA_add_pkcs1_prefix(&hash_data.Data, &hash_data.Length, &is_alloced,
                                hash_nid, hash_data.Data, hash_data.Length)) {
        return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
      }
      if (is_alloced)
        free_digest_info.reset(hash_data.Data);

      // Set RSA blinding.
      CSSM_CONTEXT_ATTRIBUTE blinding_attr;
      blinding_attr.AttributeType = CSSM_ATTRIBUTE_RSA_BLINDING;
      blinding_attr.AttributeLength = sizeof(uint32_t);
      blinding_attr.Attribute.Uint32 = 1;
      if (CSSM_UpdateContextAttributes(cssm_signature.get(), 1,
                                       &blinding_attr) != CSSM_OK) {
        return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
      }
    }

    signature->resize(max_length_);
    CSSM_DATA signature_data;
    signature_data.Length = signature->size();
    signature_data.Data = signature->data();

    if (CSSM_SignData(cssm_signature.get(), &hash_data, 1, CSSM_ALGID_NONE,
                      &signature_data) != CSSM_OK) {
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }
    signature->resize(signature_data.Length);
    return OK;
  }

 private:
  int type_;
  size_t max_length_;
  base::ScopedCFTypeRef<SecKeyRef> key_;
  const CSSM_KEY* cssm_key_;

  DISALLOW_COPY_AND_ASSIGN(SSLPlatformKeyCSSM);
};

// Returns the corresponding SecKeyAlgorithm or nullptr if unrecognized.
API_AVAILABLE(macosx(10.12))
SecKeyAlgorithm GetSecKeyAlgorithm(uint16_t algorithm) {
  switch (algorithm) {
    case SSL_SIGN_RSA_PKCS1_SHA512:
      return kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA512;
    case SSL_SIGN_RSA_PKCS1_SHA384:
      return kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA384;
    case SSL_SIGN_RSA_PKCS1_SHA256:
      return kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256;
    case SSL_SIGN_RSA_PKCS1_SHA1:
      return kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA1;
    case SSL_SIGN_RSA_PKCS1_MD5_SHA1:
      return kSecKeyAlgorithmRSASignatureDigestPKCS1v15Raw;
    case SSL_SIGN_ECDSA_SECP521R1_SHA512:
      return kSecKeyAlgorithmECDSASignatureDigestX962SHA512;
    case SSL_SIGN_ECDSA_SECP384R1_SHA384:
      return kSecKeyAlgorithmECDSASignatureDigestX962SHA384;
    case SSL_SIGN_ECDSA_SECP256R1_SHA256:
      return kSecKeyAlgorithmECDSASignatureDigestX962SHA256;
    case SSL_SIGN_ECDSA_SHA1:
      return kSecKeyAlgorithmECDSASignatureDigestX962SHA1;
  }

  if (__builtin_available(macOS 10.13, *)) {
    switch (algorithm) {
      case SSL_SIGN_RSA_PSS_SHA512:
        return kSecKeyAlgorithmRSASignatureDigestPSSSHA512;
      case SSL_SIGN_RSA_PSS_SHA384:
        return kSecKeyAlgorithmRSASignatureDigestPSSSHA384;
      case SSL_SIGN_RSA_PSS_SHA256:
        return kSecKeyAlgorithmRSASignatureDigestPSSSHA256;
    }
  }

  return nullptr;
}

class API_AVAILABLE(macosx(10.12)) SSLPlatformKeySecKey
    : public ThreadedSSLPrivateKey::Delegate {
 public:
  SSLPlatformKeySecKey(int type, size_t max_length, SecKeyRef key)
      : key_(key, base::scoped_policy::RETAIN) {
    // Determine the algorithms supported by the key.
    for (uint16_t algorithm : SSLPrivateKey::DefaultAlgorithmPreferences(
             type, true /* include PSS */)) {
      SecKeyAlgorithm sec_algorithm = GetSecKeyAlgorithm(algorithm);
      if (sec_algorithm &&
          SecKeyIsAlgorithmSupported(key_.get(), kSecKeyOperationTypeSign,
                                     sec_algorithm)) {
        preferences_.push_back(algorithm);
      }
    }
  }

  ~SSLPlatformKeySecKey() override {}

  std::vector<uint16_t> GetAlgorithmPreferences() override {
    return preferences_;
  }

  Error Sign(uint16_t algorithm,
             base::span<const uint8_t> input,
             std::vector<uint8_t>* signature) override {
    SecKeyAlgorithm sec_algorithm = GetSecKeyAlgorithm(algorithm);
    if (!sec_algorithm) {
      NOTREACHED();
      return ERR_FAILED;
    }

    const EVP_MD* md = SSL_get_signature_algorithm_digest(algorithm);
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned digest_len;
    if (!md || !EVP_Digest(input.data(), input.size(), digest, &digest_len, md,
                           nullptr)) {
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }

    base::ScopedCFTypeRef<CFDataRef> digest_ref(CFDataCreateWithBytesNoCopy(
        kCFAllocatorDefault, digest, base::checked_cast<CFIndex>(digest_len),
        kCFAllocatorNull));

    base::ScopedCFTypeRef<CFErrorRef> error;
    base::ScopedCFTypeRef<CFDataRef> signature_ref(SecKeyCreateSignature(
        key_, sec_algorithm, digest_ref, error.InitializeInto()));
    if (!signature_ref) {
      LOG(ERROR) << error;
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }

    signature->assign(
        CFDataGetBytePtr(signature_ref),
        CFDataGetBytePtr(signature_ref) + CFDataGetLength(signature_ref));
    return OK;
  }

 private:
  std::vector<uint16_t> preferences_;
  base::ScopedCFTypeRef<SecKeyRef> key_;

  DISALLOW_COPY_AND_ASSIGN(SSLPlatformKeySecKey);
};

scoped_refptr<SSLPrivateKey> CreateSSLPrivateKeyForSecKey(
    const X509Certificate* certificate,
    SecKeyRef private_key) {
  int key_type;
  size_t max_length;
  if (!GetClientCertInfo(certificate, &key_type, &max_length))
    return nullptr;

  if (__builtin_available(macOS 10.12, *)) {
    return base::MakeRefCounted<ThreadedSSLPrivateKey>(
        std::make_unique<SSLPlatformKeySecKey>(key_type, max_length,
                                               private_key),
        GetSSLPlatformKeyTaskRunner());
  }

  const CSSM_KEY* cssm_key;
  OSStatus status = SecKeyGetCSSMKey(private_key, &cssm_key);
  if (status != noErr) {
    OSSTATUS_LOG(WARNING, status);
    return nullptr;
  }

  return base::MakeRefCounted<ThreadedSSLPrivateKey>(
      std::make_unique<SSLPlatformKeyCSSM>(key_type, max_length, private_key,
                                           cssm_key),
      GetSSLPlatformKeyTaskRunner());
}

}  // namespace

scoped_refptr<SSLPrivateKey> CreateSSLPrivateKeyForSecIdentity(
    const X509Certificate* certificate,
    SecIdentityRef identity) {
  base::ScopedCFTypeRef<SecKeyRef> private_key;
  OSStatus status =
      SecIdentityCopyPrivateKey(identity, private_key.InitializeInto());
  if (status != noErr) {
    OSSTATUS_LOG(WARNING, status);
    return nullptr;
  }

  return CreateSSLPrivateKeyForSecKey(certificate, private_key.get());
}

#pragma clang diagnostic pop  // "-Wdeprecated-declarations"

}  // namespace net

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
#include <dlfcn.h>

#include <memory>

#include "base/lazy_instance.h"
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

#if !defined(MAC_OS_X_VERSION_10_12) || \
    MAC_OS_X_VERSION_MIN_ALLOWED < MAC_OS_X_VERSION_10_12
// Redeclare typedefs that only exist in 10.12+ to suppress
// -Wpartial-availability warnings.
typedef CFStringRef SecKeyAlgorithm;
#endif

namespace net {

// CSSM functions are deprecated as of OSX 10.7, but have no replacement.
// https://bugs.chromium.org/p/chromium/issues/detail?id=590914#c1
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace {

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

// These symbols were added in the 10.12 SDK, but we currently use an older SDK,
// so look them up with dlsym.
//
// TODO(davidben): After https://crbug.com/669240 is fixed, use the APIs
// directly.

struct API_AVAILABLE(macosx(10.12)) SecKeyAPIs {
  SecKeyAPIs() { Init(); }

  void Init() {
    SecKeyCreateSignature = reinterpret_cast<SecKeyCreateSignatureFunc>(
        dlsym(RTLD_DEFAULT, "SecKeyCreateSignature"));
    if (!SecKeyCreateSignature) {
      NOTREACHED();
      return;
    }

#define LOOKUP_ALGORITHM(name)                                          \
  do {                                                                  \
    SecKeyAlgorithm* algorithm =                                        \
        reinterpret_cast<SecKeyAlgorithm*>(dlsym(RTLD_DEFAULT, #name)); \
    if (!algorithm) {                                                   \
      NOTREACHED();                                                     \
      return;                                                           \
    }                                                                   \
    name = *algorithm;                                                  \
  } while (0)

    LOOKUP_ALGORITHM(kSecKeyAlgorithmRSASignatureDigestPKCS1v15Raw);
    LOOKUP_ALGORITHM(kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA1);
    LOOKUP_ALGORITHM(kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256);
    LOOKUP_ALGORITHM(kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA384);
    LOOKUP_ALGORITHM(kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA512);
    LOOKUP_ALGORITHM(kSecKeyAlgorithmECDSASignatureDigestX962SHA1);
    LOOKUP_ALGORITHM(kSecKeyAlgorithmECDSASignatureDigestX962SHA256);
    LOOKUP_ALGORITHM(kSecKeyAlgorithmECDSASignatureDigestX962SHA384);
    LOOKUP_ALGORITHM(kSecKeyAlgorithmECDSASignatureDigestX962SHA512);

#undef LOOKUP_ALGORITHM

    valid = true;
  }

  using SecKeyCreateSignatureFunc = CFDataRef (*)(SecKeyRef key,
                                                  SecKeyAlgorithm algorithm,
                                                  CFDataRef dataToSign,
                                                  CFErrorRef* error);

  bool valid = false;
  SecKeyCreateSignatureFunc SecKeyCreateSignature = nullptr;
  SecKeyAlgorithm kSecKeyAlgorithmRSASignatureDigestPKCS1v15Raw = nullptr;
  SecKeyAlgorithm kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA1 = nullptr;
  SecKeyAlgorithm kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256 = nullptr;
  SecKeyAlgorithm kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA384 = nullptr;
  SecKeyAlgorithm kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA512 = nullptr;
  SecKeyAlgorithm kSecKeyAlgorithmECDSASignatureDigestX962SHA1 = nullptr;
  SecKeyAlgorithm kSecKeyAlgorithmECDSASignatureDigestX962SHA256 = nullptr;
  SecKeyAlgorithm kSecKeyAlgorithmECDSASignatureDigestX962SHA384 = nullptr;
  SecKeyAlgorithm kSecKeyAlgorithmECDSASignatureDigestX962SHA512 = nullptr;
};

base::LazyInstance<SecKeyAPIs>::Leaky API_AVAILABLE(macosx(10.12))
    g_sec_key_apis = LAZY_INSTANCE_INITIALIZER;

class SSLPlatformKeyCSSM : public ThreadedSSLPrivateKey::Delegate {
 public:
  SSLPlatformKeyCSSM(int type,
                     size_t max_length,
                     SecKeyRef key,
                     const CSSM_KEY* cssm_key)
      : max_length_(max_length),
        key_(key, base::scoped_policy::RETAIN),
        cssm_key_(cssm_key) {}

  ~SSLPlatformKeyCSSM() override {}

  std::vector<SSLPrivateKey::Hash> GetDigestPreferences() override {
    return std::vector<SSLPrivateKey::Hash>{
        SSLPrivateKey::Hash::SHA512, SSLPrivateKey::Hash::SHA384,
        SSLPrivateKey::Hash::SHA256, SSLPrivateKey::Hash::SHA1};
  }

  Error SignDigest(SSLPrivateKey::Hash hash,
                   const base::StringPiece& input,
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

    CSSM_DATA hash_data;
    hash_data.Length = input.size();
    hash_data.Data =
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(input.data()));

    bssl::UniquePtr<uint8_t> free_digest_info;
    if (cssm_key_->KeyHeader.AlgorithmId == CSSM_ALGID_RSA) {
      // CSSM expects the caller to prepend the DigestInfo.
      int hash_nid = NID_undef;
      switch (hash) {
        case SSLPrivateKey::Hash::MD5_SHA1:
          hash_nid = NID_md5_sha1;
          break;
        case SSLPrivateKey::Hash::SHA1:
          hash_nid = NID_sha1;
          break;
        case SSLPrivateKey::Hash::SHA256:
          hash_nid = NID_sha256;
          break;
        case SSLPrivateKey::Hash::SHA384:
          hash_nid = NID_sha384;
          break;
        case SSLPrivateKey::Hash::SHA512:
          hash_nid = NID_sha512;
          break;
      }
      DCHECK_NE(NID_undef, hash_nid);
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
  size_t max_length_;
  base::ScopedCFTypeRef<SecKeyRef> key_;
  const CSSM_KEY* cssm_key_;

  DISALLOW_COPY_AND_ASSIGN(SSLPlatformKeyCSSM);
};

class API_AVAILABLE(macosx(10.12)) SSLPlatformKeySecKey
    : public ThreadedSSLPrivateKey::Delegate {
 public:
  SSLPlatformKeySecKey(int type, size_t max_length, SecKeyRef key)
      : type_(type), key_(key, base::scoped_policy::RETAIN) {}

  ~SSLPlatformKeySecKey() override {}

  std::vector<SSLPrivateKey::Hash> GetDigestPreferences() override {
    return std::vector<SSLPrivateKey::Hash>{
        SSLPrivateKey::Hash::SHA512, SSLPrivateKey::Hash::SHA384,
        SSLPrivateKey::Hash::SHA256, SSLPrivateKey::Hash::SHA1};
  }

  Error SignDigest(SSLPrivateKey::Hash hash,
                   const base::StringPiece& input,
                   std::vector<uint8_t>* signature) override {
    const SecKeyAPIs& apis = g_sec_key_apis.Get();
    if (!apis.valid) {
      LOG(ERROR) << "SecKey APIs not found";
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }

    SecKeyAlgorithm algorithm = nullptr;
    if (type_ == EVP_PKEY_RSA) {
      switch (hash) {
        case SSLPrivateKey::Hash::SHA512:
          algorithm = apis.kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA512;
          break;
        case SSLPrivateKey::Hash::SHA384:
          algorithm = apis.kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA384;
          break;
        case SSLPrivateKey::Hash::SHA256:
          algorithm = apis.kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256;
          break;
        case SSLPrivateKey::Hash::SHA1:
          algorithm = apis.kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA1;
          break;
        case SSLPrivateKey::Hash::MD5_SHA1:
          algorithm = apis.kSecKeyAlgorithmRSASignatureDigestPKCS1v15Raw;
          break;
      }
    } else if (type_ == EVP_PKEY_EC) {
      switch (hash) {
        case SSLPrivateKey::Hash::SHA512:
          algorithm = apis.kSecKeyAlgorithmECDSASignatureDigestX962SHA512;
          break;
        case SSLPrivateKey::Hash::SHA384:
          algorithm = apis.kSecKeyAlgorithmECDSASignatureDigestX962SHA384;
          break;
        case SSLPrivateKey::Hash::SHA256:
          algorithm = apis.kSecKeyAlgorithmECDSASignatureDigestX962SHA256;
          break;
        case SSLPrivateKey::Hash::SHA1:
          algorithm = apis.kSecKeyAlgorithmECDSASignatureDigestX962SHA1;
          break;
        case SSLPrivateKey::Hash::MD5_SHA1:
          // MD5-SHA1 is not used with ECDSA.
          break;
      }
    }

    if (!algorithm) {
      NOTREACHED();
      return ERR_FAILED;
    }

    base::ScopedCFTypeRef<CFDataRef> input_ref(CFDataCreateWithBytesNoCopy(
        kCFAllocatorDefault, reinterpret_cast<const uint8_t*>(input.data()),
        base::checked_cast<CFIndex>(input.size()), kCFAllocatorNull));

    base::ScopedCFTypeRef<CFErrorRef> error;
    base::ScopedCFTypeRef<CFDataRef> signature_ref(apis.SecKeyCreateSignature(
        key_, algorithm, input_ref, error.InitializeInto()));
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
  int type_;
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

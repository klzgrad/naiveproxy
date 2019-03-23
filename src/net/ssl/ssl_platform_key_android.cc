// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/ssl_platform_key_android.h"

#include <strings.h>

#include <memory>
#include <utility>
#include <vector>

#include "base/android/build_info.h"
#include "base/android/scoped_java_ref.h"
#include "base/logging.h"
#include "base/macros.h"
#include "net/android/keystore.h"
#include "net/base/net_errors.h"
#include "net/ssl/ssl_platform_key_util.h"
#include "net/ssl/threaded_ssl_private_key.h"
#include "third_party/boringssl/src/include/openssl/ecdsa.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/mem.h"
#include "third_party/boringssl/src/include/openssl/nid.h"
#include "third_party/boringssl/src/include/openssl/rsa.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"

using base::android::JavaRef;
using base::android::ScopedJavaGlobalRef;

namespace net {

namespace {

const char* GetJavaAlgorithm(uint16_t algorithm) {
  switch (algorithm) {
    case SSL_SIGN_RSA_PKCS1_SHA1:
      return "SHA1withRSA";
    case SSL_SIGN_RSA_PKCS1_SHA256:
      return "SHA256withRSA";
    case SSL_SIGN_RSA_PKCS1_SHA384:
      return "SHA384withRSA";
    case SSL_SIGN_RSA_PKCS1_SHA512:
      return "SHA512withRSA";
    case SSL_SIGN_ECDSA_SHA1:
      return "SHA1withECDSA";
    case SSL_SIGN_ECDSA_SECP256R1_SHA256:
      return "SHA256withECDSA";
    case SSL_SIGN_ECDSA_SECP384R1_SHA384:
      return "SHA384withECDSA";
    case SSL_SIGN_ECDSA_SECP521R1_SHA512:
      return "SHA512withECDSA";
    case SSL_SIGN_RSA_PSS_SHA256:
      return "SHA256withRSA/PSS";
    case SSL_SIGN_RSA_PSS_SHA384:
      return "SHA384withRSA/PSS";
    case SSL_SIGN_RSA_PSS_SHA512:
      return "SHA512withRSA/PSS";
    default:
      return nullptr;
  }
}

class SSLPlatformKeyAndroid : public ThreadedSSLPrivateKey::Delegate {
 public:
  SSLPlatformKeyAndroid(int type, const JavaRef<jobject>& key)
      : type_(type), provider_name_(android::GetPrivateKeyClassName(key)) {
    key_.Reset(key);
  }

  ~SSLPlatformKeyAndroid() override {}

  std::string GetProviderName() override { return provider_name_; }

  std::vector<uint16_t> GetAlgorithmPreferences() override {
    bool supports_pss = base::android::BuildInfo::GetInstance()->sdk_int() >=
                        base::android::SDK_VERSION_NOUGAT;
    return SSLPrivateKey::DefaultAlgorithmPreferences(type_, supports_pss);
  }

  Error Sign(uint16_t algorithm,
             base::span<const uint8_t> input,
             std::vector<uint8_t>* signature) override {
    if (algorithm == SSL_SIGN_RSA_PKCS1_MD5_SHA1) {
      // SSL_SIGN_RSA_PKCS1_MD5_SHA1 cannot be implemented with the Java
      // signature API directly.
      return SignRSAWithMD5SHA1(input, signature);
    }

    const char* java_algorithm = GetJavaAlgorithm(algorithm);
    if (!java_algorithm) {
      LOG(ERROR) << "Unknown algorithm " << algorithm;
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }
    if (!android::SignWithPrivateKey(key_, java_algorithm, input, signature)) {
      LOG(ERROR) << "Could not sign message with private key!";
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }
    return OK;
  }

 private:
  Error SignRSAWithMD5SHA1(base::span<const uint8_t> input,
                           std::vector<uint8_t>* signature) {
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned digest_len;
    if (!EVP_Digest(input.data(), input.size(), digest, &digest_len,
                    EVP_md5_sha1(), nullptr)) {
      LOG(ERROR) << "Could not take digest.";
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }

    if (!android::SignWithPrivateKey(key_, "NONEwithRSA",
                                     base::make_span(digest, digest_len),
                                     signature)) {
      LOG(ERROR) << "Could not sign message with private key!";
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }
    return OK;
  }

  int type_;
  ScopedJavaGlobalRef<jobject> key_;
  std::string provider_name_;

  DISALLOW_COPY_AND_ASSIGN(SSLPlatformKeyAndroid);
};

}  // namespace

scoped_refptr<SSLPrivateKey> WrapJavaPrivateKey(
    const X509Certificate* certificate,
    const JavaRef<jobject>& key) {
  int type;
  size_t max_length;
  if (!GetClientCertInfo(certificate, &type, &max_length))
    return nullptr;

  return base::MakeRefCounted<ThreadedSSLPrivateKey>(
      std::make_unique<SSLPlatformKeyAndroid>(type, key),
      GetSSLPlatformKeyTaskRunner());
}

}  // namespace net

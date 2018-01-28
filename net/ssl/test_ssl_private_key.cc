// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/test_ssl_private_key.h"

#include <memory>
#include <utility>

#include "base/logging.h"
#include "base/macros.h"
#include "crypto/rsa_private_key.h"
#include "net/base/net_errors.h"
#include "net/ssl/ssl_platform_key_util.h"
#include "net/ssl/ssl_private_key.h"
#include "net/ssl/threaded_ssl_private_key.h"
#include "third_party/boringssl/src/include/openssl/digest.h"
#include "third_party/boringssl/src/include/openssl/ec.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/rsa.h"

namespace net {

namespace {

class TestSSLPlatformKey : public ThreadedSSLPrivateKey::Delegate {
 public:
  explicit TestSSLPlatformKey(bssl::UniquePtr<EVP_PKEY> key)
      : key_(std::move(key)) {}

  ~TestSSLPlatformKey() override {}

  std::vector<SSLPrivateKey::Hash> GetDigestPreferences() override {
    static const SSLPrivateKey::Hash kHashes[] = {
        SSLPrivateKey::Hash::SHA512, SSLPrivateKey::Hash::SHA384,
        SSLPrivateKey::Hash::SHA256, SSLPrivateKey::Hash::SHA1};
    return std::vector<SSLPrivateKey::Hash>(kHashes,
                                            kHashes + arraysize(kHashes));
  }

  Error SignDigest(SSLPrivateKey::Hash hash,
                   const base::StringPiece& input,
                   std::vector<uint8_t>* signature) override {
    bssl::UniquePtr<EVP_PKEY_CTX> ctx(EVP_PKEY_CTX_new(key_.get(), nullptr));
    if (!ctx)
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    if (!EVP_PKEY_sign_init(ctx.get()))
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;

    if (EVP_PKEY_id(key_.get()) == EVP_PKEY_RSA) {
      const EVP_MD* digest = nullptr;
      switch (hash) {
        case SSLPrivateKey::Hash::MD5_SHA1:
          digest = EVP_md5_sha1();
          break;
        case SSLPrivateKey::Hash::SHA1:
          digest = EVP_sha1();
          break;
        case SSLPrivateKey::Hash::SHA256:
          digest = EVP_sha256();
          break;
        case SSLPrivateKey::Hash::SHA384:
          digest = EVP_sha384();
          break;
        case SSLPrivateKey::Hash::SHA512:
          digest = EVP_sha512();
          break;
        default:
          return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
      }
      DCHECK(digest);
      if (!EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_PADDING))
        return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
      if (!EVP_PKEY_CTX_set_signature_md(ctx.get(), digest))
        return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }

    const uint8_t* input_ptr = reinterpret_cast<const uint8_t*>(input.data());
    size_t input_len = input.size();
    size_t sig_len = 0;
    if (!EVP_PKEY_sign(ctx.get(), NULL, &sig_len, input_ptr, input_len))
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    signature->resize(sig_len);
    if (!EVP_PKEY_sign(ctx.get(), signature->data(), &sig_len, input_ptr,
                       input_len)) {
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }

    signature->resize(sig_len);

    return OK;
  }

 private:
  bssl::UniquePtr<EVP_PKEY> key_;

  DISALLOW_COPY_AND_ASSIGN(TestSSLPlatformKey);
};

}  // namespace

scoped_refptr<SSLPrivateKey> WrapOpenSSLPrivateKey(
    bssl::UniquePtr<EVP_PKEY> key) {
  if (!key)
    return nullptr;

  return base::MakeRefCounted<ThreadedSSLPrivateKey>(
      std::make_unique<TestSSLPlatformKey>(std::move(key)),
      GetSSLPlatformKeyTaskRunner());
}

scoped_refptr<SSLPrivateKey> WrapRSAPrivateKey(
    crypto::RSAPrivateKey* rsa_private_key) {
  EVP_PKEY_up_ref(rsa_private_key->key());
  return net::WrapOpenSSLPrivateKey(
      bssl::UniquePtr<EVP_PKEY>(rsa_private_key->key()));
}

}  // namespace net

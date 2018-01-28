// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/ssl_private_key_test_util.h"

#include <stdint.h>

#include <vector>

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "crypto/openssl_util.h"
#include "net/base/net_errors.h"
#include "net/ssl/ssl_private_key.h"
#include "net/test/gtest_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "third_party/boringssl/src/include/openssl/digest.h"
#include "third_party/boringssl/src/include/openssl/ec.h"
#include "third_party/boringssl/src/include/openssl/ec_key.h"
#include "third_party/boringssl/src/include/openssl/evp.h"

using net::test::IsOk;

namespace net {

namespace {

const char* HashToString(SSLPrivateKey::Hash hash) {
  switch (hash) {
    case SSLPrivateKey::Hash::MD5_SHA1:
      return "MD5_SHA1";
    case SSLPrivateKey::Hash::SHA1:
      return "SHA1";
    case SSLPrivateKey::Hash::SHA256:
      return "SHA256";
    case SSLPrivateKey::Hash::SHA384:
      return "SHA384";
    case SSLPrivateKey::Hash::SHA512:
      return "SHA512";
  }

  NOTREACHED();
  return "";
}

const EVP_MD* HashToMD(SSLPrivateKey::Hash hash) {
  switch (hash) {
    case SSLPrivateKey::Hash::MD5_SHA1:
      return EVP_md5_sha1();
    case SSLPrivateKey::Hash::SHA1:
      return EVP_sha1();
    case SSLPrivateKey::Hash::SHA256:
      return EVP_sha256();
    case SSLPrivateKey::Hash::SHA384:
      return EVP_sha384();
    case SSLPrivateKey::Hash::SHA512:
      return EVP_sha512();
  }

  NOTREACHED();
  return nullptr;
}

// Resize a string to |size| bytes of data, then return its data buffer address
// cast as an 'uint8_t*', as expected by OpenSSL functions.
// |str| the target string.
// |size| the number of bytes to write into the string.
// Return the string's new buffer in memory, as an 'uint8_t*' pointer.
uint8_t* OpenSSLWriteInto(std::string* str, size_t size) {
  return reinterpret_cast<uint8_t*>(base::WriteInto(str, size + 1));
}

bool VerifyWithOpenSSL(const EVP_MD* md,
                       const base::StringPiece& digest,
                       EVP_PKEY* key,
                       const base::StringPiece& signature) {
  bssl::UniquePtr<EVP_PKEY_CTX> ctx(EVP_PKEY_CTX_new(key, nullptr));
  if (!ctx || !EVP_PKEY_verify_init(ctx.get()) ||
      !EVP_PKEY_CTX_set_signature_md(ctx.get(), md) ||
      !EVP_PKEY_verify(
          ctx.get(), reinterpret_cast<const uint8_t*>(signature.data()),
          signature.size(), reinterpret_cast<const uint8_t*>(digest.data()),
          digest.size())) {
    return false;
  }

  return true;
}

bool SignWithOpenSSL(const EVP_MD* md,
                     const base::StringPiece& digest,
                     EVP_PKEY* key,
                     std::string* result) {
  size_t sig_len = EVP_PKEY_size(key);
  bssl::UniquePtr<EVP_PKEY_CTX> ctx(EVP_PKEY_CTX_new(key, nullptr));
  if (!ctx || !EVP_PKEY_sign_init(ctx.get()) ||
      !EVP_PKEY_CTX_set_signature_md(ctx.get(), md) ||
      !EVP_PKEY_sign(ctx.get(), OpenSSLWriteInto(result, sig_len), &sig_len,
                     reinterpret_cast<const uint8_t*>(digest.data()),
                     digest.size())) {
    return false;
  }

  result->resize(sig_len);
  return true;
}

void OnSignComplete(base::RunLoop* loop,
                    Error* out_error,
                    std::string* out_signature,
                    Error error,
                    const std::vector<uint8_t>& signature) {
  *out_error = error;
  out_signature->assign(signature.begin(), signature.end());
  loop->Quit();
}

Error DoKeySigningWithWrapper(SSLPrivateKey* key,
                              SSLPrivateKey::Hash hash,
                              const base::StringPiece& message,
                              std::string* result) {
  Error error;
  base::RunLoop loop;
  key->SignDigest(
      hash, message,
      base::Bind(OnSignComplete, base::Unretained(&loop),
                 base::Unretained(&error), base::Unretained(result)));
  loop.Run();
  return error;
}

}  // namespace

void TestSSLPrivateKeyMatches(SSLPrivateKey* key, const std::string& pkcs8) {
  crypto::OpenSSLErrStackTracer err_tracer(FROM_HERE);

  // Create the equivalent OpenSSL key.
  CBS cbs;
  CBS_init(&cbs, reinterpret_cast<const uint8_t*>(pkcs8.data()), pkcs8.size());
  bssl::UniquePtr<EVP_PKEY> openssl_key(EVP_parse_private_key(&cbs));
  ASSERT_TRUE(openssl_key);
  EXPECT_EQ(0u, CBS_len(&cbs));

  // Test all supported hash algorithms.
  std::vector<SSLPrivateKey::Hash> hashes = key->GetDigestPreferences();

  // To support TLS 1.1 and earlier, RSA keys must implicitly support MD5-SHA1,
  // despite not being advertised.
  if (EVP_PKEY_id(openssl_key.get()) == EVP_PKEY_RSA)
    hashes.push_back(SSLPrivateKey::Hash::MD5_SHA1);

  for (SSLPrivateKey::Hash hash : hashes) {
    SCOPED_TRACE(HashToString(hash));
    const EVP_MD* md = HashToMD(hash);

    std::string digest(EVP_MD_size(md), 'a');

    // Test the key generates valid signatures.
    std::string signature;
    Error error = DoKeySigningWithWrapper(key, hash, digest, &signature);
    EXPECT_THAT(error, IsOk());
    EXPECT_TRUE(VerifyWithOpenSSL(md, digest, openssl_key.get(), signature));

    // RSA signing is deterministic, so further check the signature matches.
    if (EVP_PKEY_id(openssl_key.get()) == EVP_PKEY_RSA) {
      std::string openssl_signature;
      ASSERT_TRUE(
          SignWithOpenSSL(md, digest, openssl_key.get(), &openssl_signature));
      EXPECT_EQ(openssl_signature, signature);
    }
  }
}

}  // namespace net

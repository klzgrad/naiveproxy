// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/ssl_platform_key_win.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/macros.h"
#include "crypto/openssl_util.h"
#include "crypto/scoped_capi_types.h"
#include "net/base/net_errors.h"
#include "net/cert/x509_certificate.h"
#include "net/ssl/ssl_platform_key_util.h"
#include "net/ssl/ssl_private_key.h"
#include "net/ssl/threaded_ssl_private_key.h"
#include "third_party/boringssl/src/include/openssl/bn.h"
#include "third_party/boringssl/src/include/openssl/ecdsa.h"
#include "third_party/boringssl/src/include/openssl/evp.h"

namespace net {

namespace {

class SSLPlatformKeyCAPI : public ThreadedSSLPrivateKey::Delegate {
 public:
  // Takes ownership of |provider|.
  SSLPlatformKeyCAPI(HCRYPTPROV provider, DWORD key_spec)
      : provider_(provider), key_spec_(key_spec) {}

  ~SSLPlatformKeyCAPI() override {}

  std::vector<SSLPrivateKey::Hash> GetDigestPreferences() override {
    // If the key is in CAPI, assume conservatively that the CAPI service
    // provider may only be able to sign pre-TLS-1.2 and SHA-1 hashes.
    static const SSLPrivateKey::Hash kHashes[] = {
        SSLPrivateKey::Hash::SHA1, SSLPrivateKey::Hash::SHA512,
        SSLPrivateKey::Hash::SHA384, SSLPrivateKey::Hash::SHA256};
    return std::vector<SSLPrivateKey::Hash>(kHashes,
                                            kHashes + arraysize(kHashes));
  }

  Error SignDigest(SSLPrivateKey::Hash hash,
                   const base::StringPiece& input,
                   std::vector<uint8_t>* signature) override {
    ALG_ID hash_alg = 0;
    switch (hash) {
      case SSLPrivateKey::Hash::MD5_SHA1:
        hash_alg = CALG_SSL3_SHAMD5;
        break;
      case SSLPrivateKey::Hash::SHA1:
        hash_alg = CALG_SHA1;
        break;
      case SSLPrivateKey::Hash::SHA256:
        hash_alg = CALG_SHA_256;
        break;
      case SSLPrivateKey::Hash::SHA384:
        hash_alg = CALG_SHA_384;
        break;
      case SSLPrivateKey::Hash::SHA512:
        hash_alg = CALG_SHA_512;
        break;
    }
    DCHECK_NE(static_cast<ALG_ID>(0), hash_alg);

    crypto::ScopedHCRYPTHASH hash_handle;
    if (!CryptCreateHash(provider_, hash_alg, 0, 0, hash_handle.receive())) {
      PLOG(ERROR) << "CreateCreateHash failed";
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }
    DWORD hash_len;
    DWORD arg_len = sizeof(hash_len);
    if (!CryptGetHashParam(hash_handle.get(), HP_HASHSIZE,
                           reinterpret_cast<BYTE*>(&hash_len), &arg_len, 0)) {
      PLOG(ERROR) << "CryptGetHashParam HP_HASHSIZE failed";
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }
    if (hash_len != input.size())
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    if (!CryptSetHashParam(
            hash_handle.get(), HP_HASHVAL,
            const_cast<BYTE*>(reinterpret_cast<const BYTE*>(input.data())),
            0)) {
      PLOG(ERROR) << "CryptSetHashParam HP_HASHVAL failed";
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }
    DWORD signature_len = 0;
    if (!CryptSignHash(hash_handle.get(), key_spec_, nullptr, 0, nullptr,
                       &signature_len)) {
      PLOG(ERROR) << "CryptSignHash failed";
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }
    signature->resize(signature_len);
    if (!CryptSignHash(hash_handle.get(), key_spec_, nullptr, 0,
                       signature->data(), &signature_len)) {
      PLOG(ERROR) << "CryptSignHash failed";
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }
    signature->resize(signature_len);

    // CryptoAPI signs in little-endian, so reverse it.
    std::reverse(signature->begin(), signature->end());
    return OK;
  }

 private:
  crypto::ScopedHCRYPTPROV provider_;
  DWORD key_spec_;

  DISALLOW_COPY_AND_ASSIGN(SSLPlatformKeyCAPI);
};

class SSLPlatformKeyCNG : public ThreadedSSLPrivateKey::Delegate {
 public:
  // Takes ownership of |key|.
  SSLPlatformKeyCNG(NCRYPT_KEY_HANDLE key, int type, size_t max_length)
      : key_(key), type_(type), max_length_(max_length) {}

  ~SSLPlatformKeyCNG() override { NCryptFreeObject(key_); }

  std::vector<SSLPrivateKey::Hash> GetDigestPreferences() override {
    // If this is an under 1024-bit RSA key, conservatively prefer to sign
    // SHA-1 hashes. Older Estonian ID cards can only sign SHA-1 hashes.
    // However, if the server doesn't advertise SHA-1, the remaining hashes
    // might still be supported.
    if (type_ == EVP_PKEY_RSA && max_length_ <= 1024 / 8) {
      static const SSLPrivateKey::Hash kHashesSpecial[] = {
          SSLPrivateKey::Hash::SHA1, SSLPrivateKey::Hash::SHA512,
          SSLPrivateKey::Hash::SHA384, SSLPrivateKey::Hash::SHA256};
      return std::vector<SSLPrivateKey::Hash>(
          kHashesSpecial, kHashesSpecial + arraysize(kHashesSpecial));
    }
    static const SSLPrivateKey::Hash kHashes[] = {
        SSLPrivateKey::Hash::SHA512, SSLPrivateKey::Hash::SHA384,
        SSLPrivateKey::Hash::SHA256, SSLPrivateKey::Hash::SHA1};
    return std::vector<SSLPrivateKey::Hash>(kHashes,
                                            kHashes + arraysize(kHashes));
  }

  Error SignDigest(SSLPrivateKey::Hash hash,
                   const base::StringPiece& input,
                   std::vector<uint8_t>* signature) override {
    crypto::OpenSSLErrStackTracer tracer(FROM_HERE);

    BCRYPT_PKCS1_PADDING_INFO rsa_padding_info = {0};
    void* padding_info = nullptr;
    DWORD flags = 0;
    if (type_ == EVP_PKEY_RSA) {
      switch (hash) {
        case SSLPrivateKey::Hash::MD5_SHA1:
          rsa_padding_info.pszAlgId = nullptr;
          break;
        case SSLPrivateKey::Hash::SHA1:
          rsa_padding_info.pszAlgId = BCRYPT_SHA1_ALGORITHM;
          break;
        case SSLPrivateKey::Hash::SHA256:
          rsa_padding_info.pszAlgId = BCRYPT_SHA256_ALGORITHM;
          break;
        case SSLPrivateKey::Hash::SHA384:
          rsa_padding_info.pszAlgId = BCRYPT_SHA384_ALGORITHM;
          break;
        case SSLPrivateKey::Hash::SHA512:
          rsa_padding_info.pszAlgId = BCRYPT_SHA512_ALGORITHM;
          break;
      }
      padding_info = &rsa_padding_info;
      flags |= BCRYPT_PAD_PKCS1;
    }

    DWORD signature_len;
    SECURITY_STATUS status = NCryptSignHash(
        key_, padding_info,
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(input.data())),
        input.size(), nullptr, 0, &signature_len, flags);
    if (FAILED(status)) {
      LOG(ERROR) << "NCryptSignHash failed: " << status;
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }
    signature->resize(signature_len);
    status = NCryptSignHash(
        key_, padding_info,
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(input.data())),
        input.size(), signature->data(), signature_len, &signature_len, flags);
    if (FAILED(status)) {
      LOG(ERROR) << "NCryptSignHash failed: " << status;
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }
    signature->resize(signature_len);

    // CNG emits raw ECDSA signatures, but BoringSSL expects a DER-encoded
    // ECDSA-Sig-Value.
    if (type_ == EVP_PKEY_EC) {
      if (signature->size() % 2 != 0) {
        LOG(ERROR) << "Bad signature length";
        return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
      }
      size_t order_len = signature->size() / 2;

      // Convert the RAW ECDSA signature to a DER-encoded ECDSA-Sig-Value.
      bssl::UniquePtr<ECDSA_SIG> sig(ECDSA_SIG_new());
      if (!sig || !BN_bin2bn(signature->data(), order_len, sig->r) ||
          !BN_bin2bn(signature->data() + order_len, order_len, sig->s)) {
        return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
      }

      int len = i2d_ECDSA_SIG(sig.get(), nullptr);
      if (len <= 0)
        return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
      signature->resize(len);
      uint8_t* ptr = signature->data();
      len = i2d_ECDSA_SIG(sig.get(), &ptr);
      if (len <= 0)
        return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
      signature->resize(len);
    }

    return OK;
  }

 private:
  NCRYPT_KEY_HANDLE key_;
  int type_;
  size_t max_length_;

  DISALLOW_COPY_AND_ASSIGN(SSLPlatformKeyCNG);
};

}  // namespace

scoped_refptr<SSLPrivateKey> WrapCAPIPrivateKey(
    const X509Certificate* certificate,
    HCRYPTPROV prov,
    DWORD key_spec) {
  return base::MakeRefCounted<ThreadedSSLPrivateKey>(
      std::make_unique<SSLPlatformKeyCAPI>(prov, key_spec),
      GetSSLPlatformKeyTaskRunner());
}

scoped_refptr<SSLPrivateKey> WrapCNGPrivateKey(
    const X509Certificate* certificate,
    NCRYPT_KEY_HANDLE key) {
  // Rather than query the private key for metadata, extract the public key from
  // the certificate without using Windows APIs. CNG does not consistently work
  // depending on the system. See https://crbug.com/468345.
  int key_type;
  size_t max_length;
  if (!GetClientCertInfo(certificate, &key_type, &max_length)) {
    NCryptFreeObject(key);
    return nullptr;
  }

  return base::MakeRefCounted<ThreadedSSLPrivateKey>(
      std::make_unique<SSLPlatformKeyCNG>(key, key_type, max_length),
      GetSSLPlatformKeyTaskRunner());
}

scoped_refptr<SSLPrivateKey> FetchClientCertPrivateKey(
    const X509Certificate* certificate,
    PCCERT_CONTEXT cert_context) {
  HCRYPTPROV_OR_NCRYPT_KEY_HANDLE prov_or_key = 0;
  DWORD key_spec = 0;
  BOOL must_free = FALSE;
  DWORD flags = CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG;

  if (!CryptAcquireCertificatePrivateKey(cert_context, flags, nullptr,
                                         &prov_or_key, &key_spec, &must_free)) {
    PLOG(WARNING) << "Could not acquire private key";
    return nullptr;
  }

  // Should never get a cached handle back - ownership must always be
  // transferred.
  CHECK_EQ(must_free, TRUE);

  if (key_spec == CERT_NCRYPT_KEY_SPEC) {
    return WrapCNGPrivateKey(certificate, prov_or_key);
  } else {
    return WrapCAPIPrivateKey(certificate, prov_or_key, key_spec);
  }
}

}  // namespace net

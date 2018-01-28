// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/ssl_platform_key_nss.h"

#include <cert.h>
#include <keyhi.h>
#include <pk11pub.h>
#include <prerror.h>

#include <memory>
#include <utility>

#include "base/logging.h"
#include "base/macros.h"
#include "base/threading/scoped_blocking_call.h"
#include "crypto/nss_crypto_module_delegate.h"
#include "crypto/scoped_nss_types.h"
#include "net/cert/x509_certificate.h"
#include "net/ssl/ssl_platform_key_util.h"
#include "net/ssl/ssl_private_key.h"
#include "net/ssl/threaded_ssl_private_key.h"
#include "third_party/boringssl/src/include/openssl/bn.h"
#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "third_party/boringssl/src/include/openssl/ec.h"
#include "third_party/boringssl/src/include/openssl/ec_key.h"
#include "third_party/boringssl/src/include/openssl/ecdsa.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/mem.h"
#include "third_party/boringssl/src/include/openssl/nid.h"
#include "third_party/boringssl/src/include/openssl/rsa.h"

namespace net {

namespace {

void LogPRError(const char* message) {
  PRErrorCode err = PR_GetError();
  const char* err_name = PR_ErrorToName(err);
  if (err_name == nullptr)
    err_name = "";
  LOG(ERROR) << message << ": " << err << " (" << err_name << ")";
}

class SSLPlatformKeyNSS : public ThreadedSSLPrivateKey::Delegate {
 public:
  SSLPlatformKeyNSS(int type,
                    scoped_refptr<crypto::CryptoModuleBlockingPasswordDelegate>
                        password_delegate,
                    crypto::ScopedSECKEYPrivateKey key)
      : type_(type),
        password_delegate_(std::move(password_delegate)),
        key_(std::move(key)) {}
  ~SSLPlatformKeyNSS() override {}

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
    SECItem digest_item;
    digest_item.data =
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(input.data()));
    digest_item.len = input.size();

    bssl::UniquePtr<uint8_t> free_digest_info;
    if (type_ == EVP_PKEY_RSA) {
      // PK11_Sign expects the caller to prepend the DigestInfo.
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
      size_t prefix_len;
      if (!RSA_add_pkcs1_prefix(&digest_item.data, &prefix_len, &is_alloced,
                                hash_nid, digest_item.data, digest_item.len)) {
        return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
      }
      digest_item.len = prefix_len;
      if (is_alloced)
        free_digest_info.reset(digest_item.data);
    }

    int len = PK11_SignatureLen(key_.get());
    if (len <= 0) {
      LogPRError("PK11_SignatureLen failed");
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }
    signature->resize(len);
    SECItem signature_item;
    signature_item.data = signature->data();
    signature_item.len = signature->size();

    SECStatus rv = PK11_Sign(key_.get(), &signature_item, &digest_item);
    if (rv != SECSuccess) {
      LogPRError("PK11_Sign failed");
      return ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED;
    }
    signature->resize(signature_item.len);

    // NSS emits raw ECDSA signatures, but BoringSSL expects a DER-encoded
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
  int type_;
  // NSS retains a pointer to the password delegate, so retain a reference here
  // to ensure the lifetimes are correct.
  scoped_refptr<crypto::CryptoModuleBlockingPasswordDelegate>
      password_delegate_;
  crypto::ScopedSECKEYPrivateKey key_;

  DISALLOW_COPY_AND_ASSIGN(SSLPlatformKeyNSS);
};

}  // namespace

scoped_refptr<SSLPrivateKey> FetchClientCertPrivateKey(
    const X509Certificate* certificate,
    CERTCertificate* cert_certificate,
    scoped_refptr<crypto::CryptoModuleBlockingPasswordDelegate>
        password_delegate) {
  // This function may acquire the NSS lock or reenter this code via extension
  // hooks (such as smart card UI). To ensure threads are not starved or
  // deadlocked, the base::ScopedBlockingCall below increments the thread pool
  // capacity if this method takes too much time to run.
  base::ScopedBlockingCall scoped_blocking_call(base::BlockingType::MAY_BLOCK);

  void* wincx = password_delegate ? password_delegate->wincx() : nullptr;
  crypto::ScopedSECKEYPrivateKey key(
      PK11_FindKeyByAnyCert(cert_certificate, wincx));
  if (!key)
    return nullptr;

  int type;
  size_t max_length;
  if (!GetClientCertInfo(certificate, &type, &max_length))
    return nullptr;

  // Note that key contains a reference to password_delegate->wincx() and may
  // use it in PK11_Sign. Thus password_delegate must outlive key. We pass it
  // into SSLPlatformKeyNSS to tie the lifetimes together. See
  // https://crbug.com/779090.
  return base::MakeRefCounted<ThreadedSSLPrivateKey>(
      std::make_unique<SSLPlatformKeyNSS>(type, std::move(password_delegate),
                                          std::move(key)),
      GetSSLPlatformKeyTaskRunner());
}

}  // namespace net

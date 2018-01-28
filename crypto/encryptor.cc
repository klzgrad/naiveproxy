// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crypto/encryptor.h"

#include <stddef.h>
#include <stdint.h>

#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/sys_byteorder.h"
#include "crypto/openssl_util.h"
#include "crypto/symmetric_key.h"
#include "third_party/boringssl/src/include/openssl/aes.h"
#include "third_party/boringssl/src/include/openssl/evp.h"

namespace crypto {

namespace {

const EVP_CIPHER* GetCipherForKey(const SymmetricKey* key) {
  switch (key->key().length()) {
    case 16: return EVP_aes_128_cbc();
    case 32: return EVP_aes_256_cbc();
    default:
      return nullptr;
  }
}

// On destruction this class will cleanup the ctx, and also clear the OpenSSL
// ERR stack as a convenience.
class ScopedCipherCTX {
 public:
  ScopedCipherCTX() {
    EVP_CIPHER_CTX_init(&ctx_);
  }
  ~ScopedCipherCTX() {
    EVP_CIPHER_CTX_cleanup(&ctx_);
    ClearOpenSSLERRStack(FROM_HERE);
  }
  EVP_CIPHER_CTX* get() { return &ctx_; }

 private:
  EVP_CIPHER_CTX ctx_;
};

}  // namespace

/////////////////////////////////////////////////////////////////////////////
// Encyptor::Counter Implementation.
Encryptor::Counter::Counter(base::StringPiece counter) {
  CHECK(sizeof(counter_) == counter.length());

  memcpy(&counter_, counter.data(), sizeof(counter_));
}

Encryptor::Counter::~Counter() = default;

bool Encryptor::Counter::Increment() {
  uint64_t low_num = base::NetToHost64(counter_.components64[1]);
  uint64_t new_low_num = low_num + 1;
  counter_.components64[1] = base::HostToNet64(new_low_num);

  // If overflow occured then increment the most significant component.
  if (new_low_num < low_num) {
    counter_.components64[0] =
        base::HostToNet64(base::NetToHost64(counter_.components64[0]) + 1);
  }

  // TODO(hclam): Return false if counter value overflows.
  return true;
}

void Encryptor::Counter::Write(void* buf) {
  uint8_t* buf_ptr = reinterpret_cast<uint8_t*>(buf);
  memcpy(buf_ptr, &counter_, sizeof(counter_));
}

size_t Encryptor::Counter::GetLengthInBytes() const {
  return sizeof(counter_);
}

/////////////////////////////////////////////////////////////////////////////
// Encryptor Implementation.

Encryptor::Encryptor() : key_(nullptr), mode_(CBC) {}

Encryptor::~Encryptor() = default;

bool Encryptor::Init(const SymmetricKey* key, Mode mode, base::StringPiece iv) {
  DCHECK(key);
  DCHECK(mode == CBC || mode == CTR);

  EnsureOpenSSLInit();
  if (mode == CBC && iv.size() != AES_BLOCK_SIZE)
    return false;

  if (GetCipherForKey(key) == nullptr)
    return false;

  key_ = key;
  mode_ = mode;
  iv.CopyToString(&iv_);
  return true;
}

bool Encryptor::Encrypt(base::StringPiece plaintext, std::string* ciphertext) {
  CHECK(!plaintext.empty() || (mode_ == CBC));
  return (mode_ == CTR) ?
      CryptCTR(true, plaintext, ciphertext) :
      Crypt(true, plaintext, ciphertext);
}

bool Encryptor::Decrypt(base::StringPiece ciphertext, std::string* plaintext) {
  CHECK(!ciphertext.empty());
  return (mode_ == CTR) ?
      CryptCTR(false, ciphertext, plaintext) :
      Crypt(false, ciphertext, plaintext);
}

bool Encryptor::SetCounter(base::StringPiece counter) {
  if (mode_ != CTR)
    return false;
  if (counter.length() != 16u)
    return false;

  counter_.reset(new Counter(counter));
  return true;
}

bool Encryptor::Crypt(bool do_encrypt,
                      base::StringPiece input,
                      std::string* output) {
  DCHECK(key_);  // Must call Init() before En/De-crypt.
  // Work on the result in a local variable, and then only transfer it to
  // |output| on success to ensure no partial data is returned.
  std::string result;
  output->clear();

  const EVP_CIPHER* cipher = GetCipherForKey(key_);
  DCHECK(cipher);  // Already handled in Init();

  const std::string& key = key_->key();
  DCHECK_EQ(EVP_CIPHER_iv_length(cipher), iv_.length());
  DCHECK_EQ(EVP_CIPHER_key_length(cipher), key.length());

  ScopedCipherCTX ctx;
  if (!EVP_CipherInit_ex(ctx.get(), cipher, nullptr,
                         reinterpret_cast<const uint8_t*>(key.data()),
                         reinterpret_cast<const uint8_t*>(iv_.data()),
                         do_encrypt))
    return false;

  // When encrypting, add another block size of space to allow for any padding.
  const size_t output_size = input.size() + (do_encrypt ? iv_.size() : 0);
  CHECK_GT(output_size, 0u);
  CHECK_GT(output_size + 1, input.size());
  uint8_t* out_ptr =
      reinterpret_cast<uint8_t*>(base::WriteInto(&result, output_size + 1));
  int out_len;
  if (!EVP_CipherUpdate(ctx.get(), out_ptr, &out_len,
                        reinterpret_cast<const uint8_t*>(input.data()),
                        input.length()))
    return false;

  // Write out the final block plus padding (if any) to the end of the data
  // just written.
  int tail_len;
  if (!EVP_CipherFinal_ex(ctx.get(), out_ptr + out_len, &tail_len))
    return false;

  out_len += tail_len;
  DCHECK_LE(out_len, static_cast<int>(output_size));
  result.resize(out_len);

  output->swap(result);
  return true;
}

bool Encryptor::CryptCTR(bool do_encrypt,
                         base::StringPiece input,
                         std::string* output) {
  if (!counter_.get()) {
    LOG(ERROR) << "Counter value not set in CTR mode.";
    return false;
  }

  AES_KEY aes_key;
  if (AES_set_encrypt_key(reinterpret_cast<const uint8_t*>(key_->key().data()),
                          key_->key().size() * 8, &aes_key) != 0) {
    return false;
  }

  const size_t out_size = input.size();
  CHECK_GT(out_size, 0u);
  CHECK_GT(out_size + 1, input.size());

  std::string result;
  uint8_t* out_ptr =
      reinterpret_cast<uint8_t*>(base::WriteInto(&result, out_size + 1));

  uint8_t ivec[AES_BLOCK_SIZE] = { 0 };
  uint8_t ecount_buf[AES_BLOCK_SIZE] = { 0 };
  unsigned int block_offset = 0;

  counter_->Write(ivec);

  AES_ctr128_encrypt(reinterpret_cast<const uint8_t*>(input.data()), out_ptr,
                     input.size(), &aes_key, ivec, ecount_buf, &block_offset);

  // AES_ctr128_encrypt() updates |ivec|. Update the |counter_| here.
  SetCounter(base::StringPiece(reinterpret_cast<const char*>(ivec),
                               AES_BLOCK_SIZE));

  output->swap(result);
  return true;
}

}  // namespace crypto

// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTO_ENCRYPTOR_H_
#define CRYPTO_ENCRYPTOR_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>

#include "base/strings/string_piece.h"
#include "build/build_config.h"
#include "crypto/crypto_export.h"

namespace crypto {

class SymmetricKey;

class CRYPTO_EXPORT Encryptor {
 public:
  enum Mode {
    CBC,
    CTR,
  };

  // This class implements a 128-bits counter to be used in AES-CTR encryption.
  // Only 128-bits counter is supported in this class.
  class CRYPTO_EXPORT Counter {
   public:
    explicit Counter(base::StringPiece counter);
    ~Counter();

    // Increment the counter value.
    bool Increment();

    // Write the content of the counter to |buf|. |buf| should have enough
    // space for |GetLengthInBytes()|.
    void Write(void* buf);

    // Return the length of this counter.
    size_t GetLengthInBytes() const;

   private:
    union {
      uint32_t components32[4];
      uint64_t components64[2];
    } counter_;
  };

  Encryptor();
  ~Encryptor();

  // Initializes the encryptor using |key| and |iv|. Returns false if either the
  // key or the initialization vector cannot be used.
  //
  // If |mode| is CBC, |iv| must not be empty; if it is CTR, then |iv| must be
  // empty.
  bool Init(const SymmetricKey* key, Mode mode, base::StringPiece iv);

  // Encrypts |plaintext| into |ciphertext|.  |plaintext| may only be empty if
  // the mode is CBC.
  bool Encrypt(base::StringPiece plaintext, std::string* ciphertext);

  // Decrypts |ciphertext| into |plaintext|.  |ciphertext| must not be empty.
  //
  // WARNING: In CBC mode, Decrypt() returns false if it detects the padding
  // in the decrypted plaintext is wrong. Padding errors can result from
  // tampered ciphertext or a wrong decryption key. But successful decryption
  // does not imply the authenticity of the data. The caller of Decrypt()
  // must either authenticate the ciphertext before decrypting it, or take
  // care to not report decryption failure. Otherwise it could inadvertently
  // be used as a padding oracle to attack the cryptosystem.
  bool Decrypt(base::StringPiece ciphertext, std::string* plaintext);

  // Sets the counter value when in CTR mode. Currently only 128-bits
  // counter value is supported.
  //
  // Returns true only if update was successful.
  bool SetCounter(base::StringPiece counter);

  // TODO(albertb): Support streaming encryption.

 private:
  const SymmetricKey* key_;
  Mode mode_;
  std::unique_ptr<Counter> counter_;

  bool Crypt(bool do_encrypt,  // Pass true to encrypt, false to decrypt.
             base::StringPiece input,
             std::string* output);
  bool CryptCTR(bool do_encrypt, base::StringPiece input, std::string* output);
  std::string iv_;
};

}  // namespace crypto

#endif  // CRYPTO_ENCRYPTOR_H_

/*
 * Copyright 2002-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/aes.h>

#include <assert.h>

#include "../aes/internal.h"
#include "../modes/internal.h"
#include "../service_indicator/internal.h"


void AES_ctr128_encrypt(const uint8_t *in, uint8_t *out, size_t len,
                        const AES_KEY *key, uint8_t ivec[AES_BLOCK_SIZE],
                        uint8_t ecount_buf[AES_BLOCK_SIZE], unsigned int *num) {
  if (hwaes_capable()) {
    CRYPTO_ctr128_encrypt_ctr32(in, out, len, key, ivec, ecount_buf, num,
                                aes_hw_ctr32_encrypt_blocks);
  } else if (vpaes_capable()) {
    // TODO(davidben): On ARM, where |BSAES| is additionally defined, this could
    // use |vpaes_ctr32_encrypt_blocks_with_bsaes|.
    CRYPTO_ctr128_encrypt_ctr32(in, out, len, key, ivec, ecount_buf, num,
                                vpaes_ctr32_encrypt_blocks);
  } else {
    CRYPTO_ctr128_encrypt_ctr32(in, out, len, key, ivec, ecount_buf, num,
                                aes_nohw_ctr32_encrypt_blocks);
  }

  FIPS_service_indicator_update_state();
}

void AES_ecb_encrypt(const uint8_t *in, uint8_t *out, const AES_KEY *key,
                     const int enc) {
  assert(in && out && key);
  assert((AES_ENCRYPT == enc) || (AES_DECRYPT == enc));

  if (AES_ENCRYPT == enc) {
    AES_encrypt(in, out, key);
  } else {
    AES_decrypt(in, out, key);
  }

  FIPS_service_indicator_update_state();
}

void AES_cbc_encrypt(const uint8_t *in, uint8_t *out, size_t len,
                     const AES_KEY *key, uint8_t *ivec, const int enc) {
  if (hwaes_capable()) {
    aes_hw_cbc_encrypt(in, out, len, key, ivec, enc);
  } else if (!vpaes_capable()) {
    aes_nohw_cbc_encrypt(in, out, len, key, ivec, enc);
  } else if (enc) {
    CRYPTO_cbc128_encrypt(in, out, len, key, ivec, AES_encrypt);
  } else {
    CRYPTO_cbc128_decrypt(in, out, len, key, ivec, AES_decrypt);
  }

  FIPS_service_indicator_update_state();
}

void AES_ofb128_encrypt(const uint8_t *in, uint8_t *out, size_t length,
                        const AES_KEY *key, uint8_t *ivec, int *num) {
  unsigned num_u = (unsigned)(*num);
  CRYPTO_ofb128_encrypt(in, out, length, key, ivec, &num_u, AES_encrypt);
  *num = (int)num_u;
}

void AES_cfb128_encrypt(const uint8_t *in, uint8_t *out, size_t length,
                        const AES_KEY *key, uint8_t *ivec, int *num,
                        int enc) {
  unsigned num_u = (unsigned)(*num);
  CRYPTO_cfb128_encrypt(in, out, length, key, ivec, &num_u, enc, AES_encrypt);
  *num = (int)num_u;
}

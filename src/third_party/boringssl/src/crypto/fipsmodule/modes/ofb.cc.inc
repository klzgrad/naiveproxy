/*
 * Copyright 2008-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <assert.h>
#include <string.h>

#include "internal.h"


static_assert(16 % sizeof(size_t) == 0, "block cannot be divided into size_t");

void CRYPTO_ofb128_encrypt(const uint8_t *in, uint8_t *out, size_t len,
                           const AES_KEY *key, uint8_t ivec[16], unsigned *num,
                           block128_f block) {
  assert(key != NULL && ivec != NULL && num != NULL);
  assert(len == 0 || (in != NULL && out != NULL));

  unsigned n = *num;

  while (n && len) {
    *(out++) = *(in++) ^ ivec[n];
    --len;
    n = (n + 1) % 16;
  }

  while (len >= 16) {
    (*block)(ivec, ivec, key);
    CRYPTO_xor16(out, in, ivec);
    len -= 16;
    out += 16;
    in += 16;
    n = 0;
  }
  if (len) {
    (*block)(ivec, ivec, key);
    while (len--) {
      out[n] = in[n] ^ ivec[n];
      ++n;
    }
  }
  *num = n;
}

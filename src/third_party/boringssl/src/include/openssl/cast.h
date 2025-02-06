/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef OPENSSL_HEADER_CAST_H
#define OPENSSL_HEADER_CAST_H

#include <openssl/base.h>

#ifdef  __cplusplus
extern "C" {
#endif


#define CAST_ENCRYPT 1
#define CAST_DECRYPT 0

#define CAST_BLOCK 8
#define CAST_KEY_LENGTH 16

typedef struct cast_key_st {
  uint32_t data[32];
  int short_key;  // Use reduced rounds for short key
} CAST_KEY;

OPENSSL_EXPORT void CAST_set_key(CAST_KEY *key, size_t len,
                                 const uint8_t *data);
OPENSSL_EXPORT void CAST_ecb_encrypt(const uint8_t *in, uint8_t *out,
                                     const CAST_KEY *key, int enc);
OPENSSL_EXPORT void CAST_encrypt(uint32_t *data, const CAST_KEY *key);
OPENSSL_EXPORT void CAST_decrypt(uint32_t *data, const CAST_KEY *key);
OPENSSL_EXPORT void CAST_cbc_encrypt(const uint8_t *in, uint8_t *out,
                                     size_t length, const CAST_KEY *ks,
                                     uint8_t *iv, int enc);

OPENSSL_EXPORT void CAST_cfb64_encrypt(const uint8_t *in, uint8_t *out,
                                       size_t length, const CAST_KEY *schedule,
                                       uint8_t *ivec, int *num, int enc);

#ifdef  __cplusplus
}
#endif

#endif  // OPENSSL_HEADER_CAST_H

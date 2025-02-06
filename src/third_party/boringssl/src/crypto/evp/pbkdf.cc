/*
 * Copyright 1999-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/evp.h>

#include <string.h>

#include <openssl/hmac.h>

#include "../internal.h"


int PKCS5_PBKDF2_HMAC(const char *password, size_t password_len,
                      const uint8_t *salt, size_t salt_len, uint32_t iterations,
                      const EVP_MD *digest, size_t key_len, uint8_t *out_key) {
  // See RFC 8018, section 5.2.
  int ret = 0;
  size_t md_len = EVP_MD_size(digest);
  uint32_t i = 1;
  HMAC_CTX hctx;
  HMAC_CTX_init(&hctx);

  if (!HMAC_Init_ex(&hctx, password, password_len, digest, NULL)) {
    goto err;
  }

  while (key_len > 0) {
    size_t todo = md_len;
    if (todo > key_len) {
      todo = key_len;
    }

    uint8_t i_buf[4];
    i_buf[0] = (uint8_t)((i >> 24) & 0xff);
    i_buf[1] = (uint8_t)((i >> 16) & 0xff);
    i_buf[2] = (uint8_t)((i >> 8) & 0xff);
    i_buf[3] = (uint8_t)(i & 0xff);

    // Compute U_1.
    uint8_t digest_tmp[EVP_MAX_MD_SIZE];
    if (!HMAC_Init_ex(&hctx, NULL, 0, NULL, NULL) ||
        !HMAC_Update(&hctx, salt, salt_len) ||
        !HMAC_Update(&hctx, i_buf, 4) ||
        !HMAC_Final(&hctx, digest_tmp, NULL)) {
      goto err;
    }

    OPENSSL_memcpy(out_key, digest_tmp, todo);
    for (uint32_t j = 1; j < iterations; j++) {
      // Compute the remaining U_* values and XOR.
      if (!HMAC_Init_ex(&hctx, NULL, 0, NULL, NULL) ||
          !HMAC_Update(&hctx, digest_tmp, md_len) ||
          !HMAC_Final(&hctx, digest_tmp, NULL)) {
        goto err;
      }
      for (size_t k = 0; k < todo; k++) {
        out_key[k] ^= digest_tmp[k];
      }
    }

    key_len -= todo;
    out_key += todo;
    i++;
  }

  // RFC 8018 describes iterations (c) as being a "positive integer", so a
  // value of 0 is an error.
  //
  // Unfortunately not all consumers of PKCS5_PBKDF2_HMAC() check their return
  // value, expecting it to succeed and unconditionally using |out_key|.  As a
  // precaution for such callsites in external code, the old behavior of
  // iterations < 1 being treated as iterations == 1 is preserved, but
  // additionally an error result is returned.
  //
  // TODO(eroman): Figure out how to remove this compatibility hack, or change
  // the default to something more sensible like 2048.
  if (iterations == 0) {
    goto err;
  }

  ret = 1;

err:
  HMAC_CTX_cleanup(&hctx);
  return ret;
}

int PKCS5_PBKDF2_HMAC_SHA1(const char *password, size_t password_len,
                           const uint8_t *salt, size_t salt_len,
                           uint32_t iterations, size_t key_len,
                           uint8_t *out_key) {
  return PKCS5_PBKDF2_HMAC(password, password_len, salt, salt_len, iterations,
                           EVP_sha1(), key_len, out_key);
}

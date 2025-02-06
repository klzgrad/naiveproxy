/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef OPENSSL_HEADER_RSA_EXTRA_INTERNAL_H
#define OPENSSL_HEADER_RSA_EXTRA_INTERNAL_H

#include <openssl/base.h>

#if defined(__cplusplus)
extern "C" {
#endif


int RSA_padding_check_PKCS1_OAEP_mgf1(uint8_t *out, size_t *out_len,
                                      size_t max_out, const uint8_t *from,
                                      size_t from_len, const uint8_t *param,
                                      size_t param_len, const EVP_MD *md,
                                      const EVP_MD *mgf1md);


#if defined(__cplusplus)
}  // extern C
#endif

#endif  // OPENSSL_HEADER_RSA_EXTRA_INTERNAL_H

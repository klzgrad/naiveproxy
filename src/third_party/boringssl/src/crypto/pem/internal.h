/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifndef OPENSSL_HEADER_PEM_INTERNAL_H
#define OPENSSL_HEADER_PEM_INTERNAL_H

#include <openssl/pem.h>

#ifdef __cplusplus
extern "C" {
#endif


// PEM_get_EVP_CIPHER_INFO decodes |header| as a PEM header block and writes the
// specified cipher and IV to |cipher|. It returns one on success and zero on
// error. |header| must be a NUL-terminated string. If |header| does not
// specify encryption, this function will return success and set
// |cipher->cipher| to NULL.
int PEM_get_EVP_CIPHER_INFO(const char *header, EVP_CIPHER_INFO *cipher);

// PEM_do_header decrypts |*len| bytes from |data| in-place according to the
// information in |cipher|. On success, it returns one and sets |*len| to the
// length of the plaintext. Otherwise, it returns zero. If |cipher| specifies
// encryption, the key is derived from a password returned from |callback|.
int PEM_do_header(const EVP_CIPHER_INFO *cipher, uint8_t *data, long *len,
                  pem_password_cb *callback, void *u);


#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // OPENSSL_HEADER_PEM_INTERNAL_H

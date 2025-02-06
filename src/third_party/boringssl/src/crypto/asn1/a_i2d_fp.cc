/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/asn1.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/mem.h>


int ASN1_item_i2d_fp(const ASN1_ITEM *it, FILE *out, void *x) {
  BIO *b = BIO_new_fp(out, BIO_NOCLOSE);
  if (b == NULL) {
    OPENSSL_PUT_ERROR(ASN1, ERR_R_BUF_LIB);
    return 0;
  }
  int ret = ASN1_item_i2d_bio(it, b, x);
  BIO_free(b);
  return ret;
}

int ASN1_item_i2d_bio(const ASN1_ITEM *it, BIO *out, void *x) {
  unsigned char *b = NULL;
  int n = ASN1_item_i2d(reinterpret_cast<ASN1_VALUE *>(x), &b, it);
  if (b == NULL) {
    return 0;
  }

  int ret = BIO_write_all(out, b, n);
  OPENSSL_free(b);
  return ret;
}

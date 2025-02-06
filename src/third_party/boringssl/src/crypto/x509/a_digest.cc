/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/x509.h>

#include <openssl/asn1.h>
#include <openssl/digest.h>
#include <openssl/err.h>
#include <openssl/mem.h>

int ASN1_digest(i2d_of_void *i2d, const EVP_MD *type, char *data,
                unsigned char *md, unsigned int *len) {
  int i, ret;
  unsigned char *str, *p;

  i = i2d(data, NULL);
  if ((str = (unsigned char *)OPENSSL_malloc(i)) == NULL) {
    return 0;
  }
  p = str;
  i2d(data, &p);

  ret = EVP_Digest(str, i, md, len, type, NULL);
  OPENSSL_free(str);
  return ret;
}

int ASN1_item_digest(const ASN1_ITEM *it, const EVP_MD *type, void *asn,
                     unsigned char *md, unsigned int *len) {
  int i, ret;
  unsigned char *str = NULL;

  i = ASN1_item_i2d(reinterpret_cast<ASN1_VALUE *>(asn), &str, it);
  if (!str) {
    return 0;
  }

  ret = EVP_Digest(str, i, md, len, type, NULL);
  OPENSSL_free(str);
  return ret;
}

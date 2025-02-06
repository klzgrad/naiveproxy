/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/asn1.h>

#include <openssl/err.h>
#include <openssl/mem.h>

// ASN1_ITEM version of dup: this follows the model above except we don't
// need to allocate the buffer. At some point this could be rewritten to
// directly dup the underlying structure instead of doing and encode and
// decode.
void *ASN1_item_dup(const ASN1_ITEM *it, void *x) {
  unsigned char *b = NULL;
  const unsigned char *p;
  long i;
  void *ret;

  if (x == NULL) {
    return NULL;
  }

  i = ASN1_item_i2d(reinterpret_cast<ASN1_VALUE *>(x), &b, it);
  if (b == NULL) {
    return NULL;
  }
  p = b;
  ret = ASN1_item_d2i(NULL, &p, i, it);
  OPENSSL_free(b);
  return ret;
}

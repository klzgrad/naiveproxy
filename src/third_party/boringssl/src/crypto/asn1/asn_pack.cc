/*
 * Copyright 1999-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/asn1.h>

#include <openssl/err.h>
#include <openssl/mem.h>


ASN1_STRING *ASN1_item_pack(void *obj, const ASN1_ITEM *it, ASN1_STRING **out) {
  uint8_t *new_data = NULL;
  int len = ASN1_item_i2d(reinterpret_cast<ASN1_VALUE *>(obj), &new_data, it);
  if (len <= 0) {
    OPENSSL_PUT_ERROR(ASN1, ASN1_R_ENCODE_ERROR);
    return NULL;
  }

  ASN1_STRING *ret = NULL;
  if (out == NULL || *out == NULL) {
    ret = ASN1_STRING_new();
    if (ret == NULL) {
      OPENSSL_free(new_data);
      return NULL;
    }
  } else {
    ret = *out;
  }

  ASN1_STRING_set0(ret, new_data, len);
  if (out != NULL) {
    *out = ret;
  }
  return ret;
}

void *ASN1_item_unpack(const ASN1_STRING *oct, const ASN1_ITEM *it) {
  const unsigned char *p = oct->data;
  void *ret = ASN1_item_d2i(NULL, &p, oct->length, it);
  if (ret == NULL || p != oct->data + oct->length) {
    OPENSSL_PUT_ERROR(ASN1, ASN1_R_DECODE_ERROR);
    ASN1_item_free(reinterpret_cast<ASN1_VALUE *>(ret), it);
    return NULL;
  }
  return ret;
}

/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/asn1.h>

#include <openssl/bytestring.h>
#include <openssl/err.h>

#include "../bytestring/internal.h"


int i2d_ASN1_BOOLEAN(ASN1_BOOLEAN a, unsigned char **outp) {
  CBB cbb;
  if (!CBB_init(&cbb, 3) ||  //
      !CBB_add_asn1_bool(&cbb, a != ASN1_BOOLEAN_FALSE)) {
    CBB_cleanup(&cbb);
    return -1;
  }
  return CBB_finish_i2d(&cbb, outp);
}

ASN1_BOOLEAN d2i_ASN1_BOOLEAN(ASN1_BOOLEAN *out, const unsigned char **inp,
                              long len) {
  if (len < 0) {
    return ASN1_BOOLEAN_NONE;
  }

  CBS cbs;
  CBS_init(&cbs, *inp, (size_t)len);
  int val;
  if (!CBS_get_asn1_bool(&cbs, &val)) {
    OPENSSL_PUT_ERROR(ASN1, ASN1_R_DECODE_ERROR);
    return ASN1_BOOLEAN_NONE;
  }

  ASN1_BOOLEAN ret = val ? ASN1_BOOLEAN_TRUE : ASN1_BOOLEAN_FALSE;
  if (out != NULL) {
    *out = ret;
  }
  *inp = CBS_data(&cbs);
  return ret;
}

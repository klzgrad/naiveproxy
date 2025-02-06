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

int i2a_ASN1_STRING(BIO *bp, const ASN1_STRING *a, int type) {
  int i, n = 0;
  static const char *h = "0123456789ABCDEF";
  char buf[2];

  if (a == NULL) {
    return 0;
  }

  if (a->length == 0) {
    if (BIO_write(bp, "0", 1) != 1) {
      goto err;
    }
    n = 1;
  } else {
    for (i = 0; i < a->length; i++) {
      if ((i != 0) && (i % 35 == 0)) {
        if (BIO_write(bp, "\\\n", 2) != 2) {
          goto err;
        }
        n += 2;
      }
      buf[0] = h[((unsigned char)a->data[i] >> 4) & 0x0f];
      buf[1] = h[((unsigned char)a->data[i]) & 0x0f];
      if (BIO_write(bp, buf, 2) != 2) {
        goto err;
      }
      n += 2;
    }
  }
  return n;
err:
  return -1;
}

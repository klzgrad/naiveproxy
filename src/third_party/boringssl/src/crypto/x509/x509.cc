/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/x509.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/mem.h>


// |X509_R_UNSUPPORTED_ALGORITHM| is no longer emitted, but continue to define
// it to avoid downstream churn.
OPENSSL_DECLARE_ERROR_REASON(X509, UNSUPPORTED_ALGORITHM)

int X509_signature_dump(BIO *bp, const ASN1_STRING *sig, int indent) {
  const uint8_t *s;
  int i, n;

  n = sig->length;
  s = sig->data;
  for (i = 0; i < n; i++) {
    if ((i % 18) == 0) {
      if (BIO_write(bp, "\n", 1) <= 0 || BIO_indent(bp, indent, indent) <= 0) {
        return 0;
      }
    }
    if (BIO_printf(bp, "%02x%s", s[i], ((i + 1) == n) ? "" : ":") <= 0) {
      return 0;
    }
  }
  if (BIO_write(bp, "\n", 1) != 1) {
    return 0;
  }

  return 1;
}

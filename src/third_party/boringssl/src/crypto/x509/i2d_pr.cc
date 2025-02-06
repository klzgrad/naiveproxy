/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/asn1.h>
#include <openssl/dsa.h>
#include <openssl/ec_key.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>


int i2d_PrivateKey(const EVP_PKEY *a, uint8_t **pp) {
  switch (EVP_PKEY_id(a)) {
    case EVP_PKEY_RSA:
      return i2d_RSAPrivateKey(EVP_PKEY_get0_RSA(a), pp);
    case EVP_PKEY_EC:
      return i2d_ECPrivateKey(EVP_PKEY_get0_EC_KEY(a), pp);
    case EVP_PKEY_DSA:
      return i2d_DSAPrivateKey(EVP_PKEY_get0_DSA(a), pp);
    default:
      // Although this file is in crypto/x509 for layering reasons, it emits
      // an error code from ASN1 for OpenSSL compatibility.
      OPENSSL_PUT_ERROR(ASN1, ASN1_R_UNSUPPORTED_PUBLIC_KEY_TYPE);
      return -1;
  }
}

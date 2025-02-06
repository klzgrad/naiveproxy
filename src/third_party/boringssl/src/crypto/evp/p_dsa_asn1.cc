/*
 * Copyright 2006-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/evp.h>

#include <openssl/bn.h>
#include <openssl/bytestring.h>
#include <openssl/digest.h>
#include <openssl/dsa.h>
#include <openssl/err.h>

#include "../dsa/internal.h"
#include "internal.h"


static int dsa_pub_decode(EVP_PKEY *out, CBS *params, CBS *key) {
  // See RFC 3279, section 2.3.2.

  // Parameters may or may not be present.
  DSA *dsa;
  if (CBS_len(params) == 0) {
    dsa = DSA_new();
    if (dsa == NULL) {
      return 0;
    }
  } else {
    dsa = DSA_parse_parameters(params);
    if (dsa == NULL || CBS_len(params) != 0) {
      OPENSSL_PUT_ERROR(EVP, EVP_R_DECODE_ERROR);
      goto err;
    }
  }

  dsa->pub_key = BN_new();
  if (dsa->pub_key == NULL) {
    goto err;
  }

  if (!BN_parse_asn1_unsigned(key, dsa->pub_key) || CBS_len(key) != 0) {
    OPENSSL_PUT_ERROR(EVP, EVP_R_DECODE_ERROR);
    goto err;
  }

  EVP_PKEY_assign_DSA(out, dsa);
  return 1;

err:
  DSA_free(dsa);
  return 0;
}

static int dsa_pub_encode(CBB *out, const EVP_PKEY *key) {
  const DSA *dsa = reinterpret_cast<const DSA *>(key->pkey);
  const int has_params = dsa->p != NULL && dsa->q != NULL && dsa->g != NULL;

  // See RFC 5480, section 2.
  CBB spki, algorithm, oid, key_bitstring;
  if (!CBB_add_asn1(out, &spki, CBS_ASN1_SEQUENCE) ||
      !CBB_add_asn1(&spki, &algorithm, CBS_ASN1_SEQUENCE) ||
      !CBB_add_asn1(&algorithm, &oid, CBS_ASN1_OBJECT) ||
      !CBB_add_bytes(&oid, dsa_asn1_meth.oid, dsa_asn1_meth.oid_len) ||
      (has_params && !DSA_marshal_parameters(&algorithm, dsa)) ||
      !CBB_add_asn1(&spki, &key_bitstring, CBS_ASN1_BITSTRING) ||
      !CBB_add_u8(&key_bitstring, 0 /* padding */) ||
      !BN_marshal_asn1(&key_bitstring, dsa->pub_key) || !CBB_flush(out)) {
    OPENSSL_PUT_ERROR(EVP, EVP_R_ENCODE_ERROR);
    return 0;
  }

  return 1;
}

static int dsa_priv_decode(EVP_PKEY *out, CBS *params, CBS *key) {
  // See PKCS#11, v2.40, section 2.5.

  // Decode parameters.
  BN_CTX *ctx = NULL;
  DSA *dsa = DSA_parse_parameters(params);
  if (dsa == NULL || CBS_len(params) != 0) {
    OPENSSL_PUT_ERROR(EVP, EVP_R_DECODE_ERROR);
    goto err;
  }

  dsa->priv_key = BN_new();
  if (dsa->priv_key == NULL) {
    goto err;
  }
  if (!BN_parse_asn1_unsigned(key, dsa->priv_key) || CBS_len(key) != 0) {
    OPENSSL_PUT_ERROR(EVP, EVP_R_DECODE_ERROR);
    goto err;
  }

  // To avoid DoS attacks when importing private keys, check bounds on |dsa|.
  // This bounds |dsa->priv_key| against |dsa->q| and bounds |dsa->q|'s bit
  // width.
  if (!dsa_check_key(dsa)) {
    OPENSSL_PUT_ERROR(EVP, EVP_R_DECODE_ERROR);
    goto err;
  }

  // Calculate the public key.
  ctx = BN_CTX_new();
  dsa->pub_key = BN_new();
  if (ctx == NULL || dsa->pub_key == NULL ||
      !BN_mod_exp_mont_consttime(dsa->pub_key, dsa->g, dsa->priv_key, dsa->p,
                                 ctx, NULL)) {
    goto err;
  }

  BN_CTX_free(ctx);
  EVP_PKEY_assign_DSA(out, dsa);
  return 1;

err:
  BN_CTX_free(ctx);
  DSA_free(dsa);
  return 0;
}

static int dsa_priv_encode(CBB *out, const EVP_PKEY *key) {
  const DSA *dsa = reinterpret_cast<const DSA *>(key->pkey);
  if (dsa == NULL || dsa->priv_key == NULL) {
    OPENSSL_PUT_ERROR(EVP, EVP_R_MISSING_PARAMETERS);
    return 0;
  }

  // See PKCS#11, v2.40, section 2.5.
  CBB pkcs8, algorithm, oid, private_key;
  if (!CBB_add_asn1(out, &pkcs8, CBS_ASN1_SEQUENCE) ||
      !CBB_add_asn1_uint64(&pkcs8, 0 /* version */) ||
      !CBB_add_asn1(&pkcs8, &algorithm, CBS_ASN1_SEQUENCE) ||
      !CBB_add_asn1(&algorithm, &oid, CBS_ASN1_OBJECT) ||
      !CBB_add_bytes(&oid, dsa_asn1_meth.oid, dsa_asn1_meth.oid_len) ||
      !DSA_marshal_parameters(&algorithm, dsa) ||
      !CBB_add_asn1(&pkcs8, &private_key, CBS_ASN1_OCTETSTRING) ||
      !BN_marshal_asn1(&private_key, dsa->priv_key) || !CBB_flush(out)) {
    OPENSSL_PUT_ERROR(EVP, EVP_R_ENCODE_ERROR);
    return 0;
  }

  return 1;
}

static int int_dsa_size(const EVP_PKEY *pkey) {
  const DSA *dsa = reinterpret_cast<const DSA *>(pkey->pkey);
  return DSA_size(dsa);
}

static int dsa_bits(const EVP_PKEY *pkey) {
  const DSA *dsa = reinterpret_cast<const DSA *>(pkey->pkey);
  return BN_num_bits(DSA_get0_p(dsa));
}

static int dsa_missing_parameters(const EVP_PKEY *pkey) {
  const DSA *dsa = reinterpret_cast<const DSA *>(pkey->pkey);
  if (DSA_get0_p(dsa) == NULL || DSA_get0_q(dsa) == NULL ||
      DSA_get0_g(dsa) == NULL) {
    return 1;
  }
  return 0;
}

static int dup_bn_into(BIGNUM **out, BIGNUM *src) {
  BIGNUM *a;

  a = BN_dup(src);
  if (a == NULL) {
    return 0;
  }
  BN_free(*out);
  *out = a;

  return 1;
}

static int dsa_copy_parameters(EVP_PKEY *to, const EVP_PKEY *from) {
  DSA *to_dsa = reinterpret_cast<DSA *>(to->pkey);
  const DSA *from_dsa = reinterpret_cast<const DSA *>(from->pkey);
  if (!dup_bn_into(&to_dsa->p, from_dsa->p) ||
      !dup_bn_into(&to_dsa->q, from_dsa->q) ||
      !dup_bn_into(&to_dsa->g, from_dsa->g)) {
    return 0;
  }

  return 1;
}

static int dsa_cmp_parameters(const EVP_PKEY *a, const EVP_PKEY *b) {
  const DSA *a_dsa = reinterpret_cast<const DSA *>(a->pkey);
  const DSA *b_dsa = reinterpret_cast<const DSA *>(b->pkey);
  return BN_cmp(DSA_get0_p(a_dsa), DSA_get0_p(b_dsa)) == 0 &&
         BN_cmp(DSA_get0_q(a_dsa), DSA_get0_q(b_dsa)) == 0 &&
         BN_cmp(DSA_get0_g(a_dsa), DSA_get0_g(b_dsa)) == 0;
}

static int dsa_pub_cmp(const EVP_PKEY *a, const EVP_PKEY *b) {
  const DSA *a_dsa = reinterpret_cast<const DSA *>(a->pkey);
  const DSA *b_dsa = reinterpret_cast<const DSA *>(b->pkey);
  return BN_cmp(DSA_get0_pub_key(b_dsa), DSA_get0_pub_key(a_dsa)) == 0;
}

static void int_dsa_free(EVP_PKEY *pkey) {
  DSA_free(reinterpret_cast<DSA *>(pkey->pkey));
  pkey->pkey = NULL;
}

const EVP_PKEY_ASN1_METHOD dsa_asn1_meth = {
    EVP_PKEY_DSA,
    // 1.2.840.10040.4.1
    {0x2a, 0x86, 0x48, 0xce, 0x38, 0x04, 0x01},
    7,

    /*pkey_method=*/NULL,

    dsa_pub_decode,
    dsa_pub_encode,
    dsa_pub_cmp,

    dsa_priv_decode,
    dsa_priv_encode,

    /*set_priv_raw=*/NULL,
    /*set_pub_raw=*/NULL,
    /*get_priv_raw=*/NULL,
    /*get_pub_raw=*/NULL,
    /*set1_tls_encodedpoint=*/NULL,
    /*get1_tls_encodedpoint=*/NULL,

    /*pkey_opaque=*/NULL,

    int_dsa_size,
    dsa_bits,

    dsa_missing_parameters,
    dsa_copy_parameters,
    dsa_cmp_parameters,

    int_dsa_free,
};

int EVP_PKEY_CTX_set_dsa_paramgen_bits(EVP_PKEY_CTX *ctx, int nbits) {
  // BoringSSL does not support DSA in |EVP_PKEY_CTX|.
  OPENSSL_PUT_ERROR(EVP, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
  return 0;
}

int EVP_PKEY_CTX_set_dsa_paramgen_q_bits(EVP_PKEY_CTX *ctx, int qbits) {
  // BoringSSL does not support DSA in |EVP_PKEY_CTX|.
  OPENSSL_PUT_ERROR(EVP, ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
  return 0;
}

int EVP_PKEY_set1_DSA(EVP_PKEY *pkey, DSA *key) {
  if (EVP_PKEY_assign_DSA(pkey, key)) {
    DSA_up_ref(key);
    return 1;
  }
  return 0;
}

int EVP_PKEY_assign_DSA(EVP_PKEY *pkey, DSA *key) {
  evp_pkey_set_method(pkey, &dsa_asn1_meth);
  pkey->pkey = key;
  return key != NULL;
}

DSA *EVP_PKEY_get0_DSA(const EVP_PKEY *pkey) {
  if (pkey->type != EVP_PKEY_DSA) {
    OPENSSL_PUT_ERROR(EVP, EVP_R_EXPECTING_A_DSA_KEY);
    return NULL;
  }
  return reinterpret_cast<DSA *>(pkey->pkey);
}

DSA *EVP_PKEY_get1_DSA(const EVP_PKEY *pkey) {
  DSA *dsa = EVP_PKEY_get0_DSA(pkey);
  if (dsa != NULL) {
    DSA_up_ref(dsa);
  }
  return dsa;
}

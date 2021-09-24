/*
 * Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL project
 * 2013.
 */
/* ====================================================================
 * Copyright (c) 2013 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

#ifndef OPENSSL_HEADER_X509_INTERNAL_H
#define OPENSSL_HEADER_X509_INTERNAL_H

#include <openssl/base.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#if defined(__cplusplus)
extern "C" {
#endif


/* Internal structures. */

struct X509_val_st {
  ASN1_TIME *notBefore;
  ASN1_TIME *notAfter;
} /* X509_VAL */;

struct X509_pubkey_st {
  X509_ALGOR *algor;
  ASN1_BIT_STRING *public_key;
  EVP_PKEY *pkey;
} /* X509_PUBKEY */;

struct x509_attributes_st {
  ASN1_OBJECT *object;
  STACK_OF(ASN1_TYPE) *set;
} /* X509_ATTRIBUTE */;

struct x509_cert_aux_st {
  STACK_OF(ASN1_OBJECT) *trust;   // trusted uses
  STACK_OF(ASN1_OBJECT) *reject;  // rejected uses
  ASN1_UTF8STRING *alias;         // "friendly name"
  ASN1_OCTET_STRING *keyid;       // key id of private key
  STACK_OF(X509_ALGOR) *other;    // other unspecified info
} /* X509_CERT_AUX */;

struct X509_extension_st {
  ASN1_OBJECT *object;
  ASN1_BOOLEAN critical;
  ASN1_OCTET_STRING *value;
} /* X509_EXTENSION */;

typedef struct {
  ASN1_ENCODING enc;
  ASN1_INTEGER *version;
  X509_NAME *subject;
  X509_PUBKEY *pubkey;
  //  d=2 hl=2 l=  0 cons: cont: 00
  STACK_OF(X509_ATTRIBUTE) *attributes;  // [ 0 ]
} X509_REQ_INFO;

DECLARE_ASN1_FUNCTIONS(X509_REQ_INFO)

struct X509_req_st {
  X509_REQ_INFO *req_info;
  X509_ALGOR *sig_alg;
  ASN1_BIT_STRING *signature;
  CRYPTO_refcount_t references;
} /* X509_REQ */;

typedef struct {
  ASN1_INTEGER *version;
  X509_ALGOR *sig_alg;
  X509_NAME *issuer;
  ASN1_TIME *lastUpdate;
  ASN1_TIME *nextUpdate;
  STACK_OF(X509_REVOKED) *revoked;
  STACK_OF(X509_EXTENSION) /* [0] */ *extensions;
  ASN1_ENCODING enc;
} X509_CRL_INFO;

DECLARE_ASN1_FUNCTIONS(X509_CRL_INFO)

struct X509_crl_st {
  // actual signature
  X509_CRL_INFO *crl;
  X509_ALGOR *sig_alg;
  ASN1_BIT_STRING *signature;
  CRYPTO_refcount_t references;
  int flags;
  // Copies of various extensions
  AUTHORITY_KEYID *akid;
  ISSUING_DIST_POINT *idp;
  // Convenient breakdown of IDP
  int idp_flags;
  int idp_reasons;
  // CRL and base CRL numbers for delta processing
  ASN1_INTEGER *crl_number;
  ASN1_INTEGER *base_crl_number;
  unsigned char sha1_hash[SHA_DIGEST_LENGTH];
  STACK_OF(GENERAL_NAMES) *issuers;
  const X509_CRL_METHOD *meth;
  void *meth_data;
} /* X509_CRL */;


struct X509_VERIFY_PARAM_st {
  char *name;
  time_t check_time;                // Time to use
  unsigned long inh_flags;          // Inheritance flags
  unsigned long flags;              // Various verify flags
  int purpose;                      // purpose to check untrusted certificates
  int trust;                        // trust setting to check
  int depth;                        // Verify depth
  STACK_OF(ASN1_OBJECT) *policies;  // Permissible policies
  // The following fields specify acceptable peer identities.
  STACK_OF(OPENSSL_STRING) *hosts;  // Set of acceptable names
  unsigned int hostflags;           // Flags to control matching features
  char *peername;                   // Matching hostname in peer certificate
  char *email;                      // If not NULL email address to match
  size_t emaillen;
  unsigned char *ip;     // If not NULL IP address to match
  size_t iplen;          // Length of IP address
  unsigned char poison;  // Fail all verifications at name checking
} /* X509_VERIFY_PARAM */;


/* RSA-PSS functions. */

/* x509_rsa_pss_to_ctx configures |ctx| for an RSA-PSS operation based on
 * signature algorithm parameters in |sigalg| (which must have type
 * |NID_rsassaPss|) and key |pkey|. It returns one on success and zero on
 * error. */
int x509_rsa_pss_to_ctx(EVP_MD_CTX *ctx, const X509_ALGOR *sigalg,
                        EVP_PKEY *pkey);

/* x509_rsa_pss_to_ctx sets |algor| to the signature algorithm parameters for
 * |ctx|, which must have been configured for an RSA-PSS signing operation. It
 * returns one on success and zero on error. */
int x509_rsa_ctx_to_pss(EVP_MD_CTX *ctx, X509_ALGOR *algor);

/* x509_print_rsa_pss_params prints a human-readable representation of RSA-PSS
 * parameters in |sigalg| to |bp|. It returns one on success and zero on
 * error. */
int x509_print_rsa_pss_params(BIO *bp, const X509_ALGOR *sigalg, int indent,
                              ASN1_PCTX *pctx);


/* Signature algorithm functions. */

/* x509_digest_sign_algorithm encodes the signing parameters of |ctx| as an
 * AlgorithmIdentifer and saves the result in |algor|. It returns one on
 * success, or zero on error. */
int x509_digest_sign_algorithm(EVP_MD_CTX *ctx, X509_ALGOR *algor);

/* x509_digest_verify_init sets up |ctx| for a signature verification operation
 * with public key |pkey| and parameters from |algor|. The |ctx| argument must
 * have been initialised with |EVP_MD_CTX_init|. It returns one on success, or
 * zero on error. */
int x509_digest_verify_init(EVP_MD_CTX *ctx, const X509_ALGOR *sigalg,
                            EVP_PKEY *pkey);


#if defined(__cplusplus)
}  /* extern C */
#endif

#endif  /* OPENSSL_HEADER_X509_INTERNAL_H */

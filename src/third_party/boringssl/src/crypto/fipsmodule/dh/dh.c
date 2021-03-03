/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.] */

#include <openssl/dh.h>

#include <string.h>

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/digest.h>
#include <openssl/mem.h>
#include <openssl/thread.h>

#include "../../internal.h"


#define OPENSSL_DH_MAX_MODULUS_BITS 10000

DH *DH_new(void) {
  DH *dh = OPENSSL_malloc(sizeof(DH));
  if (dh == NULL) {
    OPENSSL_PUT_ERROR(DH, ERR_R_MALLOC_FAILURE);
    return NULL;
  }

  OPENSSL_memset(dh, 0, sizeof(DH));

  CRYPTO_MUTEX_init(&dh->method_mont_p_lock);

  dh->references = 1;

  return dh;
}

void DH_free(DH *dh) {
  if (dh == NULL) {
    return;
  }

  if (!CRYPTO_refcount_dec_and_test_zero(&dh->references)) {
    return;
  }

  BN_MONT_CTX_free(dh->method_mont_p);
  BN_clear_free(dh->p);
  BN_clear_free(dh->g);
  BN_clear_free(dh->q);
  BN_clear_free(dh->j);
  OPENSSL_free(dh->seed);
  BN_clear_free(dh->counter);
  BN_clear_free(dh->pub_key);
  BN_clear_free(dh->priv_key);
  CRYPTO_MUTEX_cleanup(&dh->method_mont_p_lock);

  OPENSSL_free(dh);
}

const BIGNUM *DH_get0_pub_key(const DH *dh) { return dh->pub_key; }

const BIGNUM *DH_get0_priv_key(const DH *dh) { return dh->priv_key; }

const BIGNUM *DH_get0_p(const DH *dh) { return dh->p; }

const BIGNUM *DH_get0_q(const DH *dh) { return dh->q; }

const BIGNUM *DH_get0_g(const DH *dh) { return dh->g; }

void DH_get0_key(const DH *dh, const BIGNUM **out_pub_key,
                 const BIGNUM **out_priv_key) {
  if (out_pub_key != NULL) {
    *out_pub_key = dh->pub_key;
  }
  if (out_priv_key != NULL) {
    *out_priv_key = dh->priv_key;
  }
}

int DH_set0_key(DH *dh, BIGNUM *pub_key, BIGNUM *priv_key) {
  if (pub_key != NULL) {
    BN_free(dh->pub_key);
    dh->pub_key = pub_key;
  }

  if (priv_key != NULL) {
    BN_free(dh->priv_key);
    dh->priv_key = priv_key;
  }

  return 1;
}

void DH_get0_pqg(const DH *dh, const BIGNUM **out_p, const BIGNUM **out_q,
                 const BIGNUM **out_g) {
  if (out_p != NULL) {
    *out_p = dh->p;
  }
  if (out_q != NULL) {
    *out_q = dh->q;
  }
  if (out_g != NULL) {
    *out_g = dh->g;
  }
}

int DH_set0_pqg(DH *dh, BIGNUM *p, BIGNUM *q, BIGNUM *g) {
  if ((dh->p == NULL && p == NULL) ||
      (dh->g == NULL && g == NULL)) {
    return 0;
  }

  if (p != NULL) {
    BN_free(dh->p);
    dh->p = p;
  }

  if (q != NULL) {
    BN_free(dh->q);
    dh->q = q;
  }

  if (g != NULL) {
    BN_free(dh->g);
    dh->g = g;
  }

  return 1;
}

int DH_set_length(DH *dh, unsigned priv_length) {
  dh->priv_length = priv_length;
  return 1;
}

int DH_generate_key(DH *dh) {
  int ok = 0;
  int generate_new_key = 0;
  BN_CTX *ctx = NULL;
  BIGNUM *pub_key = NULL, *priv_key = NULL;

  if (BN_num_bits(dh->p) > OPENSSL_DH_MAX_MODULUS_BITS) {
    OPENSSL_PUT_ERROR(DH, DH_R_MODULUS_TOO_LARGE);
    goto err;
  }

  ctx = BN_CTX_new();
  if (ctx == NULL) {
    goto err;
  }

  if (dh->priv_key == NULL) {
    priv_key = BN_new();
    if (priv_key == NULL) {
      goto err;
    }
    generate_new_key = 1;
  } else {
    priv_key = dh->priv_key;
  }

  if (dh->pub_key == NULL) {
    pub_key = BN_new();
    if (pub_key == NULL) {
      goto err;
    }
  } else {
    pub_key = dh->pub_key;
  }

  if (!BN_MONT_CTX_set_locked(&dh->method_mont_p, &dh->method_mont_p_lock,
                              dh->p, ctx)) {
    goto err;
  }

  if (generate_new_key) {
    if (dh->q) {
      if (!BN_rand_range_ex(priv_key, 2, dh->q)) {
        goto err;
      }
    } else {
      // secret exponent length
      unsigned priv_bits = dh->priv_length;
      if (priv_bits == 0) {
        const unsigned p_bits = BN_num_bits(dh->p);
        if (p_bits == 0) {
          goto err;
        }

        priv_bits = p_bits - 1;
      }

      if (!BN_rand(priv_key, priv_bits, BN_RAND_TOP_ONE, BN_RAND_BOTTOM_ANY)) {
        goto err;
      }
    }
  }

  if (!BN_mod_exp_mont_consttime(pub_key, dh->g, priv_key, dh->p, ctx,
                                 dh->method_mont_p)) {
    goto err;
  }

  dh->pub_key = pub_key;
  dh->priv_key = priv_key;
  ok = 1;

err:
  if (ok != 1) {
    OPENSSL_PUT_ERROR(DH, ERR_R_BN_LIB);
  }

  if (dh->pub_key == NULL) {
    BN_free(pub_key);
  }
  if (dh->priv_key == NULL) {
    BN_free(priv_key);
  }
  BN_CTX_free(ctx);
  return ok;
}

static int dh_compute_key(DH *dh, BIGNUM *out_shared_key,
                          const BIGNUM *peers_key, BN_CTX *ctx) {
  if (BN_num_bits(dh->p) > OPENSSL_DH_MAX_MODULUS_BITS) {
    OPENSSL_PUT_ERROR(DH, DH_R_MODULUS_TOO_LARGE);
    return 0;
  }

  if (dh->priv_key == NULL) {
    OPENSSL_PUT_ERROR(DH, DH_R_NO_PRIVATE_VALUE);
    return 0;
  }

  int check_result;
  if (!DH_check_pub_key(dh, peers_key, &check_result) || check_result) {
    OPENSSL_PUT_ERROR(DH, DH_R_INVALID_PUBKEY);
    return 0;
  }

  int ret = 0;
  BN_CTX_start(ctx);
  BIGNUM *p_minus_1 = BN_CTX_get(ctx);

  if (!p_minus_1 ||
      !BN_MONT_CTX_set_locked(&dh->method_mont_p, &dh->method_mont_p_lock,
                              dh->p, ctx)) {
    goto err;
  }

  if (!BN_mod_exp_mont_consttime(out_shared_key, peers_key, dh->priv_key, dh->p,
                                 ctx, dh->method_mont_p) ||
      !BN_copy(p_minus_1, dh->p) ||
      !BN_sub_word(p_minus_1, 1)) {
    OPENSSL_PUT_ERROR(DH, ERR_R_BN_LIB);
    goto err;
  }

  // This performs the check required by SP 800-56Ar3 section 5.7.1.1 step two.
  if (BN_cmp_word(out_shared_key, 1) <= 0 ||
      BN_cmp(out_shared_key, p_minus_1) == 0) {
    OPENSSL_PUT_ERROR(DH, DH_R_INVALID_PUBKEY);
    goto err;
  }

  ret = 1;

 err:
  BN_CTX_end(ctx);
  return ret;
}

int DH_compute_key(unsigned char *out, const BIGNUM *peers_key, DH *dh) {
  BN_CTX *ctx = BN_CTX_new();
  if (ctx == NULL) {
    return -1;
  }
  BN_CTX_start(ctx);

  int ret = -1;
  BIGNUM *shared_key = BN_CTX_get(ctx);
  if (shared_key && dh_compute_key(dh, shared_key, peers_key, ctx)) {
    ret = BN_bn2bin(shared_key, out);
  }

  BN_CTX_end(ctx);
  BN_CTX_free(ctx);
  return ret;
}

int DH_compute_key_hashed(DH *dh, uint8_t *out, size_t *out_len,
                          size_t max_out_len, const BIGNUM *peers_key,
                          const EVP_MD *digest) {
  *out_len = (size_t)-1;

  const size_t digest_len = EVP_MD_size(digest);
  if (digest_len > max_out_len) {
    return 0;
  }

  BN_CTX *ctx = BN_CTX_new();
  if (ctx == NULL) {
    return 0;
  }
  BN_CTX_start(ctx);

  int ret = 0;
  BIGNUM *shared_key = BN_CTX_get(ctx);
  const size_t p_len = BN_num_bytes(dh->p);
  uint8_t *shared_bytes = OPENSSL_malloc(p_len);
  unsigned out_len_unsigned;
  if (!shared_key ||
      !shared_bytes ||
      !dh_compute_key(dh, shared_key, peers_key, ctx) ||
      // |DH_compute_key| doesn't pad the output. SP 800-56A is ambiguous about
      // whether the output should be padded prior to revision three. But
      // revision three, section C.1, awkwardly specifies padding to the length
      // of p.
      //
      // Also, padded output avoids side-channels, so is always strongly
      // advisable.
      !BN_bn2bin_padded(shared_bytes, p_len, shared_key) ||
      !EVP_Digest(shared_bytes, p_len, out, &out_len_unsigned, digest, NULL) ||
      out_len_unsigned != digest_len) {
    goto err;
  }

  *out_len = digest_len;
  ret = 1;

 err:
  BN_CTX_end(ctx);
  BN_CTX_free(ctx);
  OPENSSL_free(shared_bytes);
  return ret;
}

int DH_size(const DH *dh) { return BN_num_bytes(dh->p); }

unsigned DH_num_bits(const DH *dh) { return BN_num_bits(dh->p); }

int DH_up_ref(DH *dh) {
  CRYPTO_refcount_inc(&dh->references);
  return 1;
}

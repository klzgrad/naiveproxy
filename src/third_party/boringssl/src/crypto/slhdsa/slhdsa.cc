/* Copyright 2024 The BoringSSL Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#include <openssl/slhdsa.h>

#include <openssl/obj.h>

#include "../fipsmodule/bcm_interface.h"


static_assert(SLHDSA_SHA2_128S_PUBLIC_KEY_BYTES ==
                  BCM_SLHDSA_SHA2_128S_PUBLIC_KEY_BYTES,
              "");
static_assert(SLHDSA_SHA2_128S_PRIVATE_KEY_BYTES ==
                  BCM_SLHDSA_SHA2_128S_PRIVATE_KEY_BYTES,
              "");
static_assert(SLHDSA_SHA2_128S_SIGNATURE_BYTES ==
                  BCM_SLHDSA_SHA2_128S_SIGNATURE_BYTES,
              "");

void SLHDSA_SHA2_128S_generate_key(
    uint8_t out_public_key[SLHDSA_SHA2_128S_PUBLIC_KEY_BYTES],
    uint8_t out_private_key[SLHDSA_SHA2_128S_PRIVATE_KEY_BYTES]) {
  BCM_slhdsa_sha2_128s_generate_key(out_public_key, out_private_key);
}

void SLHDSA_SHA2_128S_public_from_private(
    uint8_t out_public_key[SLHDSA_SHA2_128S_PUBLIC_KEY_BYTES],
    const uint8_t private_key[SLHDSA_SHA2_128S_PRIVATE_KEY_BYTES]) {
  BCM_slhdsa_sha2_128s_public_from_private(out_public_key, private_key);
}

int SLHDSA_SHA2_128S_sign(
    uint8_t out_signature[SLHDSA_SHA2_128S_SIGNATURE_BYTES],
    const uint8_t private_key[SLHDSA_SHA2_128S_PRIVATE_KEY_BYTES],
    const uint8_t *msg, size_t msg_len, const uint8_t *context,
    size_t context_len) {
  return bcm_success(BCM_slhdsa_sha2_128s_sign(out_signature, private_key, msg,
                                               msg_len, context, context_len));
}

int SLHDSA_SHA2_128S_verify(
    const uint8_t *signature, size_t signature_len,
    const uint8_t public_key[SLHDSA_SHA2_128S_PUBLIC_KEY_BYTES],
    const uint8_t *msg, size_t msg_len, const uint8_t *context,
    size_t context_len) {
  return bcm_success(BCM_slhdsa_sha2_128s_verify(signature, signature_len,
                                                 public_key, msg, msg_len,
                                                 context, context_len));
}

int SLHDSA_SHA2_128S_prehash_sign(
    uint8_t out_signature[SLHDSA_SHA2_128S_SIGNATURE_BYTES],
    const uint8_t private_key[SLHDSA_SHA2_128S_PRIVATE_KEY_BYTES],
    const uint8_t *hashed_msg, size_t hashed_msg_len, int hash_nid,
    const uint8_t *context, size_t context_len) {
  if (hash_nid != NID_sha256) {
    return 0;
  }
  return bcm_success(BCM_slhdsa_sha2_128s_prehash_sign(
      out_signature, private_key, hashed_msg, hashed_msg_len, hash_nid, context,
      context_len));
}

int SLHDSA_SHA2_128S_prehash_verify(
    const uint8_t *signature, size_t signature_len,
    const uint8_t public_key[SLHDSA_SHA2_128S_PUBLIC_KEY_BYTES],
    const uint8_t *hashed_msg, size_t hashed_msg_len, int hash_nid,
    const uint8_t *context, size_t context_len) {
  if (hash_nid != NID_sha256) {
    return 0;
  }
  return bcm_success(BCM_slhdsa_sha2_128s_prehash_verify(
      signature, signature_len, public_key, hashed_msg, hashed_msg_len,
      hash_nid, context, context_len));
}

int SLHDSA_SHA2_128S_prehash_warning_nonstandard_sign(
    uint8_t out_signature[SLHDSA_SHA2_128S_SIGNATURE_BYTES],
    const uint8_t private_key[SLHDSA_SHA2_128S_PRIVATE_KEY_BYTES],
    const uint8_t *hashed_msg, size_t hashed_msg_len, int hash_nid,
    const uint8_t *context, size_t context_len) {
  if (hash_nid != NID_sha384) {
    return 0;
  }
  return bcm_success(BCM_slhdsa_sha2_128s_prehash_sign(
      out_signature, private_key, hashed_msg, hashed_msg_len, hash_nid, context,
      context_len));
}

int SLHDSA_SHA2_128S_prehash_warning_nonstandard_verify(
    const uint8_t *signature, size_t signature_len,
    const uint8_t public_key[SLHDSA_SHA2_128S_PUBLIC_KEY_BYTES],
    const uint8_t *hashed_msg, size_t hashed_msg_len, int hash_nid,
    const uint8_t *context, size_t context_len) {
  if (hash_nid != NID_sha384) {
    return 0;
  }
  return bcm_success(BCM_slhdsa_sha2_128s_prehash_verify(
      signature, signature_len, public_key, hashed_msg, hashed_msg_len,
      hash_nid, context, context_len));
}

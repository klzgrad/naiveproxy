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

#include <openssl/mldsa.h>

#include "../fipsmodule/bcm_interface.h"

static_assert(sizeof(BCM_mldsa65_private_key) == sizeof(MLDSA65_private_key),
              "");
static_assert(alignof(BCM_mldsa65_private_key) == alignof(MLDSA65_private_key),
              "");
static_assert(sizeof(BCM_mldsa65_public_key) == sizeof(MLDSA65_public_key), "");
static_assert(alignof(BCM_mldsa65_public_key) == alignof(MLDSA65_public_key),
              "");
static_assert(MLDSA_SEED_BYTES == BCM_MLDSA_SEED_BYTES, "");
static_assert(MLDSA65_PRIVATE_KEY_BYTES == BCM_MLDSA65_PRIVATE_KEY_BYTES, "");
static_assert(MLDSA65_PUBLIC_KEY_BYTES == BCM_MLDSA65_PUBLIC_KEY_BYTES, "");
static_assert(MLDSA65_SIGNATURE_BYTES == BCM_MLDSA65_SIGNATURE_BYTES, "");

int MLDSA65_generate_key(
    uint8_t out_encoded_public_key[MLDSA65_PUBLIC_KEY_BYTES],
    uint8_t out_seed[MLDSA_SEED_BYTES],
    struct MLDSA65_private_key *out_private_key) {
  return bcm_success(BCM_mldsa65_generate_key(
      out_encoded_public_key, out_seed,
      reinterpret_cast<BCM_mldsa65_private_key *>(out_private_key)));
}

int MLDSA65_private_key_from_seed(struct MLDSA65_private_key *out_private_key,
                                  const uint8_t *seed, size_t seed_len) {
  if (seed_len != BCM_MLDSA_SEED_BYTES) {
    return 0;
  }
  return bcm_success(BCM_mldsa65_private_key_from_seed(
      reinterpret_cast<BCM_mldsa65_private_key *>(out_private_key), seed));
}

int MLDSA65_public_from_private(struct MLDSA65_public_key *out_public_key,
                                const struct MLDSA65_private_key *private_key) {
  return bcm_success(BCM_mldsa65_public_from_private(
      reinterpret_cast<BCM_mldsa65_public_key *>(out_public_key),
      reinterpret_cast<const BCM_mldsa65_private_key *>(private_key)));
}

int MLDSA65_sign(uint8_t out_encoded_signature[MLDSA65_SIGNATURE_BYTES],
                 const struct MLDSA65_private_key *private_key,
                 const uint8_t *msg, size_t msg_len, const uint8_t *context,
                 size_t context_len) {
  if (context_len > 255) {
    return 0;
  }
  return bcm_success(BCM_mldsa65_sign(
      out_encoded_signature,
      reinterpret_cast<const BCM_mldsa65_private_key *>(private_key), msg,
      msg_len, context, context_len));
}

int MLDSA65_verify(const struct MLDSA65_public_key *public_key,
                   const uint8_t *signature, size_t signature_len,
                   const uint8_t *msg, size_t msg_len, const uint8_t *context,
                   size_t context_len) {
  if (context_len > 255 || signature_len != BCM_MLDSA65_SIGNATURE_BYTES) {
    return 0;
  }
  return bcm_success(BCM_mldsa65_verify(
      reinterpret_cast<const BCM_mldsa65_public_key *>(public_key), signature,
      msg, msg_len, context, context_len));
}

int MLDSA65_marshal_public_key(CBB *out,
                               const struct MLDSA65_public_key *public_key) {
  return bcm_success(BCM_mldsa65_marshal_public_key(
      out, reinterpret_cast<const BCM_mldsa65_public_key *>(public_key)));
}

int MLDSA65_parse_public_key(struct MLDSA65_public_key *public_key, CBS *in) {
  return bcm_success(BCM_mldsa65_parse_public_key(
      reinterpret_cast<BCM_mldsa65_public_key *>(public_key), in));
}

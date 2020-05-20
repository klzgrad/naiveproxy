/* Copyright (c) 2019, Google Inc.
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

#ifndef OPENSSL_HEADER_TRUST_TOKEN_H
#define OPENSSL_HEADER_TRUST_TOKEN_H

#include <openssl/base.h>
#include <openssl/stack.h>

#if defined(__cplusplus)
extern "C" {
#endif


// Trust Token implementation.
//
// Trust Token is an implementation of an experimental mechanism similar to
// Privacy Pass which allows issuance and redemption of anonymized tokens with
// limited private metadata.
//
// References:
// https://eprint.iacr.org/2020/072.pdf
// https://github.com/alxdavids/privacy-pass-ietf/tree/master/drafts
// https://github.com/WICG/trust-token-api/blob/master/README.md
//
// WARNING: This API is unstable and subject to change.

#define TRUST_TOKEN_MAX_PRIVATE_KEY_SIZE 512
#define TRUST_TOKEN_MAX_PUBLIC_KEY_SIZE 512

// TRUST_TOKEN_generate_key creates a new Trust Token keypair labeled with |id|
// and serializes the private and public keys, writing the private key to
// |out_priv_key| and setting |*out_priv_key_len| to the number of bytes
// written, and writing the public key to |out_pub_key| and setting
// |*out_pub_key_len| to the number of bytes written.
//
// At most |max_priv_key_len| and |max_pub_key_len| bytes are written. In order
// to ensure success, these should be at least
// |TRUST_TOKEN_MAX_PRIVATE_KEY_SIZE| and |TRUST_TOKEN_MAX_PUBLIC_KEY_SIZE|.
//
// WARNING: This API is unstable and the serializations of these keys are
// subject to change. Keys generated with this function may not be persisted.
//
// This function returns one on success or zero on error.
OPENSSL_EXPORT int TRUST_TOKEN_generate_key(
    uint8_t *out_priv_key, size_t *out_priv_key_len, size_t max_priv_key_len,
    uint8_t *out_pub_key, size_t *out_pub_key_len, size_t max_pub_key_len,
    uint32_t id);


#if defined(__cplusplus)
}  // extern C
#endif

#define TRUST_TOKEN_R_KEYGEN_FAILURE 100
#define TRUST_TOKEN_R_BUFFER_TOO_SMALL 101

#endif  // OPENSSL_HEADER_TRUST_TOKEN_H

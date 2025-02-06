/* Copyright 2017 The BoringSSL Authors
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

#ifndef OPENSSL_HEADER_AES_INTERNAL_H
#define OPENSSL_HEADER_AES_INTERNAL_H

#include <stdlib.h>

#include <openssl/aes.h>

#include "../../internal.h"

extern "C" {


// block128_f is the type of an AES block cipher implementation.
//
// Unlike upstream OpenSSL, it and the other functions in this file hard-code
// |AES_KEY|. It is undefined in C to call a function pointer with anything
// other than the original type. Thus we either must match |block128_f| to the
// type signature of |AES_encrypt| and friends or pass in |void*| wrapper
// functions.
//
// These functions are called exclusively with AES, so we use the former.
typedef void (*block128_f)(const uint8_t in[16], uint8_t out[16],
                           const AES_KEY *key);

// ctr128_f is the type of a function that performs CTR-mode encryption.
typedef void (*ctr128_f)(const uint8_t *in, uint8_t *out, size_t blocks,
                         const AES_KEY *key, const uint8_t ivec[16]);

// aes_ctr_set_key initialises |*aes_key| using |key_bytes| bytes from |key|,
// where |key_bytes| must either be 16, 24 or 32. If not NULL, |*out_block| is
// set to a function that encrypts single blocks. If not NULL, |*out_is_hwaes|
// is set to whether the hardware AES implementation was used. It returns a
// function for optimised CTR-mode.
ctr128_f aes_ctr_set_key(AES_KEY *aes_key, int *out_is_hwaes,
                         block128_f *out_block, const uint8_t *key,
                         size_t key_bytes);


// AES implementations.

#if !defined(OPENSSL_NO_ASM)

#if defined(OPENSSL_X86) || defined(OPENSSL_X86_64)
#define HWAES
#define HWAES_ECB

inline int hwaes_capable(void) { return CRYPTO_is_AESNI_capable(); }

#define VPAES
#define VPAES_CBC
inline int vpaes_capable(void) { return CRYPTO_is_SSSE3_capable(); }

#elif defined(OPENSSL_ARM) || defined(OPENSSL_AARCH64)
#define HWAES

inline int hwaes_capable(void) { return CRYPTO_is_ARMv8_AES_capable(); }

#if defined(OPENSSL_ARM)
#define BSAES
#define VPAES
inline int bsaes_capable(void) { return CRYPTO_is_NEON_capable(); }
inline int vpaes_capable(void) { return CRYPTO_is_NEON_capable(); }
#endif

#if defined(OPENSSL_AARCH64)
#define VPAES
#define VPAES_CBC
inline int vpaes_capable(void) { return CRYPTO_is_NEON_capable(); }
#endif

#endif

#endif  // !NO_ASM


#if defined(HWAES)

int aes_hw_set_encrypt_key(const uint8_t *user_key, int bits, AES_KEY *key);
int aes_hw_set_decrypt_key(const uint8_t *user_key, int bits, AES_KEY *key);
void aes_hw_encrypt(const uint8_t *in, uint8_t *out, const AES_KEY *key);
void aes_hw_decrypt(const uint8_t *in, uint8_t *out, const AES_KEY *key);
void aes_hw_cbc_encrypt(const uint8_t *in, uint8_t *out, size_t length,
                        const AES_KEY *key, uint8_t *ivec, int enc);
void aes_hw_ctr32_encrypt_blocks(const uint8_t *in, uint8_t *out, size_t len,
                                 const AES_KEY *key, const uint8_t ivec[16]);

#if defined(OPENSSL_X86) || defined(OPENSSL_X86_64)
// On x86 and x86_64, |aes_hw_set_decrypt_key| is implemented in terms of
// |aes_hw_set_encrypt_key| and a conversion function.
void aes_hw_encrypt_key_to_decrypt_key(AES_KEY *key);

// There are two variants of this function, one which uses aeskeygenassist
// ("base") and one which uses aesenclast + pshufb ("alt"). aesenclast is
// overall faster but is slower on some older processors. It doesn't use AVX,
// but AVX is used as a proxy to detecting this. See
// https://groups.google.com/g/mailing.openssl.dev/c/OuFXwW4NfO8/m/7d2ZXVjkxVkJ
//
// TODO(davidben): It is unclear if the aeskeygenassist version is still
// worthwhile. However, the aesenclast version requires SSSE3. SSSE3 long
// predates AES-NI, but it's not clear if AES-NI implies SSSE3. In OpenSSL, the
// CCM AES-NI assembly seems to assume it does.
inline int aes_hw_set_encrypt_key_alt_capable(void) {
  return hwaes_capable() && CRYPTO_is_SSSE3_capable();
}
inline int aes_hw_set_encrypt_key_alt_preferred(void) {
  return hwaes_capable() && CRYPTO_is_AVX_capable();
}
int aes_hw_set_encrypt_key_base(const uint8_t *user_key, int bits,
                                AES_KEY *key);
int aes_hw_set_encrypt_key_alt(const uint8_t *user_key, int bits, AES_KEY *key);
#endif  // OPENSSL_X86 || OPENSSL_X86_64

#else

// If HWAES isn't defined then we provide dummy functions for each of the hwaes
// functions.
inline int hwaes_capable(void) { return 0; }

inline int aes_hw_set_encrypt_key(const uint8_t *user_key, int bits,
                                  AES_KEY *key) {
  abort();
}

inline int aes_hw_set_decrypt_key(const uint8_t *user_key, int bits,
                                  AES_KEY *key) {
  abort();
}

inline void aes_hw_encrypt(const uint8_t *in, uint8_t *out,
                           const AES_KEY *key) {
  abort();
}

inline void aes_hw_decrypt(const uint8_t *in, uint8_t *out,
                           const AES_KEY *key) {
  abort();
}

inline void aes_hw_cbc_encrypt(const uint8_t *in, uint8_t *out, size_t length,
                               const AES_KEY *key, uint8_t *ivec, int enc) {
  abort();
}

inline void aes_hw_ctr32_encrypt_blocks(const uint8_t *in, uint8_t *out,
                                        size_t len, const AES_KEY *key,
                                        const uint8_t ivec[16]) {
  abort();
}

#endif  // !HWAES


#if defined(HWAES_ECB)
void aes_hw_ecb_encrypt(const uint8_t *in, uint8_t *out, size_t length,
                        const AES_KEY *key, int enc);
#endif  // HWAES_ECB


#if defined(BSAES)
// Note |bsaes_cbc_encrypt| requires |enc| to be zero.
void bsaes_cbc_encrypt(const uint8_t *in, uint8_t *out, size_t length,
                       const AES_KEY *key, uint8_t ivec[16], int enc);
void bsaes_ctr32_encrypt_blocks(const uint8_t *in, uint8_t *out, size_t len,
                                const AES_KEY *key, const uint8_t ivec[16]);
// VPAES to BSAES conversions are available on all BSAES platforms.
void vpaes_encrypt_key_to_bsaes(AES_KEY *out_bsaes, const AES_KEY *vpaes);
void vpaes_decrypt_key_to_bsaes(AES_KEY *out_bsaes, const AES_KEY *vpaes);
void vpaes_ctr32_encrypt_blocks_with_bsaes(const uint8_t *in, uint8_t *out,
                                           size_t blocks, const AES_KEY *key,
                                           const uint8_t ivec[16]);
#else
inline int bsaes_capable(void) { return 0; }

// On other platforms, bsaes_capable() will always return false and so the
// following will never be called.
inline void bsaes_cbc_encrypt(const uint8_t *in, uint8_t *out, size_t length,
                              const AES_KEY *key, uint8_t ivec[16], int enc) {
  abort();
}

inline void bsaes_ctr32_encrypt_blocks(const uint8_t *in, uint8_t *out,
                                       size_t len, const AES_KEY *key,
                                       const uint8_t ivec[16]) {
  abort();
}

inline void vpaes_encrypt_key_to_bsaes(AES_KEY *out_bsaes,
                                       const AES_KEY *vpaes) {
  abort();
}

inline void vpaes_decrypt_key_to_bsaes(AES_KEY *out_bsaes,
                                       const AES_KEY *vpaes) {
  abort();
}
#endif  // !BSAES


#if defined(VPAES)
// On platforms where VPAES gets defined (just above), then these functions are
// provided by asm.
int vpaes_set_encrypt_key(const uint8_t *userKey, int bits, AES_KEY *key);
int vpaes_set_decrypt_key(const uint8_t *userKey, int bits, AES_KEY *key);

void vpaes_encrypt(const uint8_t *in, uint8_t *out, const AES_KEY *key);
void vpaes_decrypt(const uint8_t *in, uint8_t *out, const AES_KEY *key);

#if defined(VPAES_CBC)
void vpaes_cbc_encrypt(const uint8_t *in, uint8_t *out, size_t length,
                       const AES_KEY *key, uint8_t *ivec, int enc);
#endif
void vpaes_ctr32_encrypt_blocks(const uint8_t *in, uint8_t *out, size_t len,
                                const AES_KEY *key, const uint8_t ivec[16]);
#else
inline int vpaes_capable(void) { return 0; }

// On other platforms, vpaes_capable() will always return false and so the
// following will never be called.
inline int vpaes_set_encrypt_key(const uint8_t *userKey, int bits,
                                 AES_KEY *key) {
  abort();
}
inline int vpaes_set_decrypt_key(const uint8_t *userKey, int bits,
                                 AES_KEY *key) {
  abort();
}
inline void vpaes_encrypt(const uint8_t *in, uint8_t *out, const AES_KEY *key) {
  abort();
}
inline void vpaes_decrypt(const uint8_t *in, uint8_t *out, const AES_KEY *key) {
  abort();
}
inline void vpaes_cbc_encrypt(const uint8_t *in, uint8_t *out, size_t length,
                              const AES_KEY *key, uint8_t *ivec, int enc) {
  abort();
}
inline void vpaes_ctr32_encrypt_blocks(const uint8_t *in, uint8_t *out,
                                       size_t len, const AES_KEY *key,
                                       const uint8_t ivec[16]) {
  abort();
}
#endif  // !VPAES


int aes_nohw_set_encrypt_key(const uint8_t *key, unsigned bits,
                             AES_KEY *aeskey);
int aes_nohw_set_decrypt_key(const uint8_t *key, unsigned bits,
                             AES_KEY *aeskey);
void aes_nohw_encrypt(const uint8_t *in, uint8_t *out, const AES_KEY *key);
void aes_nohw_decrypt(const uint8_t *in, uint8_t *out, const AES_KEY *key);
void aes_nohw_ctr32_encrypt_blocks(const uint8_t *in, uint8_t *out,
                                   size_t blocks, const AES_KEY *key,
                                   const uint8_t ivec[16]);
void aes_nohw_cbc_encrypt(const uint8_t *in, uint8_t *out, size_t len,
                          const AES_KEY *key, uint8_t *ivec, int enc);


}  // extern C

#endif  // OPENSSL_HEADER_AES_INTERNAL_H

/* Copyright (c) 2021, Google Inc.
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

#include <openssl/ssl.h>

#include <assert.h>
#include <string.h>

#include <algorithm>
#include <utility>

#include <openssl/aead.h>
#include <openssl/bytestring.h>
#include <openssl/curve25519.h>
#include <openssl/err.h>
#include <openssl/hkdf.h>
#include <openssl/hpke.h>
#include <openssl/rand.h>

#include "internal.h"


#if defined(OPENSSL_MSAN)
#define NO_SANITIZE_MEMORY __attribute__((no_sanitize("memory")))
#else
#define NO_SANITIZE_MEMORY
#endif

BSSL_NAMESPACE_BEGIN

// ECH reuses the extension code point for the version number.
static const uint16_t kECHConfigVersion = TLSEXT_TYPE_encrypted_client_hello;

static const decltype(&EVP_hpke_aes_128_gcm) kSupportedAEADs[] = {
    &EVP_hpke_aes_128_gcm,
    &EVP_hpke_aes_256_gcm,
    &EVP_hpke_chacha20_poly1305,
};

static const EVP_HPKE_AEAD *get_ech_aead(uint16_t aead_id) {
  for (const auto aead_func : kSupportedAEADs) {
    const EVP_HPKE_AEAD *aead = aead_func();
    if (aead_id == EVP_HPKE_AEAD_id(aead)) {
      return aead;
    }
  }
  return nullptr;
}

// ssl_client_hello_write_without_extensions serializes |client_hello| into
// |out|, omitting the length-prefixed extensions. It serializes individual
// fields, starting with |client_hello->version|, and ignores the
// |client_hello->client_hello| field. It returns true on success and false on
// failure.
static bool ssl_client_hello_write_without_extensions(
    const SSL_CLIENT_HELLO *client_hello, CBB *out) {
  CBB cbb;
  if (!CBB_add_u16(out, client_hello->version) ||
      !CBB_add_bytes(out, client_hello->random, client_hello->random_len) ||
      !CBB_add_u8_length_prefixed(out, &cbb) ||
      !CBB_add_bytes(&cbb, client_hello->session_id,
                     client_hello->session_id_len) ||
      !CBB_add_u16_length_prefixed(out, &cbb) ||
      !CBB_add_bytes(&cbb, client_hello->cipher_suites,
                     client_hello->cipher_suites_len) ||
      !CBB_add_u8_length_prefixed(out, &cbb) ||
      !CBB_add_bytes(&cbb, client_hello->compression_methods,
                     client_hello->compression_methods_len) ||
      !CBB_flush(out)) {
    return false;
  }
  return true;
}

bool ssl_decode_client_hello_inner(
    SSL *ssl, uint8_t *out_alert, Array<uint8_t> *out_client_hello_inner,
    Span<const uint8_t> encoded_client_hello_inner,
    const SSL_CLIENT_HELLO *client_hello_outer) {
  SSL_CLIENT_HELLO client_hello_inner;
  if (!ssl_client_hello_init(ssl, &client_hello_inner,
                             encoded_client_hello_inner)) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
    return false;
  }
  // TLS 1.3 ClientHellos must have extensions, and EncodedClientHelloInners use
  // ClientHelloOuter's session_id.
  if (client_hello_inner.extensions_len == 0 ||
      client_hello_inner.session_id_len != 0) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
    return false;
  }
  client_hello_inner.session_id = client_hello_outer->session_id;
  client_hello_inner.session_id_len = client_hello_outer->session_id_len;

  // Begin serializing a message containing the ClientHelloInner in |cbb|.
  ScopedCBB cbb;
  CBB body, extensions;
  if (!ssl->method->init_message(ssl, cbb.get(), &body, SSL3_MT_CLIENT_HELLO) ||
      !ssl_client_hello_write_without_extensions(&client_hello_inner, &body) ||
      !CBB_add_u16_length_prefixed(&body, &extensions)) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
    return false;
  }

  // Sort the extensions in ClientHelloOuter, so ech_outer_extensions may be
  // processed in O(n*log(n)) time, rather than O(n^2).
  struct Extension {
    uint16_t extension = 0;
    Span<const uint8_t> body;
    bool copied = false;
  };

  // MSan's libc interceptors do not handle |bsearch|. See b/182583130.
  auto compare_extension = [](const void *a, const void *b)
                               NO_SANITIZE_MEMORY -> int {
    const Extension *extension_a = reinterpret_cast<const Extension *>(a);
    const Extension *extension_b = reinterpret_cast<const Extension *>(b);
    if (extension_a->extension < extension_b->extension) {
      return -1;
    } else if (extension_a->extension > extension_b->extension) {
      return 1;
    }
    return 0;
  };
  GrowableArray<Extension> sorted_extensions;
  CBS unsorted_extensions(MakeConstSpan(client_hello_outer->extensions,
                                        client_hello_outer->extensions_len));
  while (CBS_len(&unsorted_extensions) > 0) {
    Extension extension;
    CBS extension_body;
    if (!CBS_get_u16(&unsorted_extensions, &extension.extension) ||
        !CBS_get_u16_length_prefixed(&unsorted_extensions, &extension_body)) {
      OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
      return false;
    }
    extension.body = extension_body;
    if (!sorted_extensions.Push(extension)) {
      return false;
    }
  }
  qsort(sorted_extensions.data(), sorted_extensions.size(), sizeof(Extension),
        compare_extension);

  // Copy extensions from |client_hello_inner|, expanding ech_outer_extensions.
  CBS inner_extensions(MakeConstSpan(client_hello_inner.extensions,
                                     client_hello_inner.extensions_len));
  while (CBS_len(&inner_extensions) > 0) {
    uint16_t extension_id;
    CBS extension_body;
    if (!CBS_get_u16(&inner_extensions, &extension_id) ||
        !CBS_get_u16_length_prefixed(&inner_extensions, &extension_body)) {
      OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
      return false;
    }
    if (extension_id != TLSEXT_TYPE_ech_outer_extensions) {
      if (!CBB_add_u16(&extensions, extension_id) ||
          !CBB_add_u16(&extensions, CBS_len(&extension_body)) ||
          !CBB_add_bytes(&extensions, CBS_data(&extension_body),
                         CBS_len(&extension_body))) {
        OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
        return false;
      }
      continue;
    }

    // Replace ech_outer_extensions with the corresponding outer extensions.
    CBS outer_extensions;
    if (!CBS_get_u8_length_prefixed(&extension_body, &outer_extensions) ||
        CBS_len(&extension_body) != 0) {
      OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
      return false;
    }
    while (CBS_len(&outer_extensions) > 0) {
      uint16_t extension_needed;
      if (!CBS_get_u16(&outer_extensions, &extension_needed)) {
        OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
        return false;
      }
      if (extension_needed == TLSEXT_TYPE_encrypted_client_hello) {
        *out_alert = SSL_AD_ILLEGAL_PARAMETER;
        OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
        return false;
      }
      // Find the referenced extension.
      Extension key;
      key.extension = extension_needed;
      Extension *result = reinterpret_cast<Extension *>(
          bsearch(&key, sorted_extensions.data(), sorted_extensions.size(),
                  sizeof(Extension), compare_extension));
      if (result == nullptr) {
        *out_alert = SSL_AD_ILLEGAL_PARAMETER;
        OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
        return false;
      }

      // Extensions may be referenced at most once, to bound the result size.
      if (result->copied) {
        *out_alert = SSL_AD_ILLEGAL_PARAMETER;
        OPENSSL_PUT_ERROR(SSL, SSL_R_DUPLICATE_EXTENSION);
        return false;
      }
      result->copied = true;

      if (!CBB_add_u16(&extensions, extension_needed) ||
          !CBB_add_u16(&extensions, result->body.size()) ||
          !CBB_add_bytes(&extensions, result->body.data(),
                         result->body.size())) {
        OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
        return false;
      }
    }
  }
  if (!CBB_flush(&body)) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
    return false;
  }

  // See https://github.com/tlswg/draft-ietf-tls-esni/pull/411
  CBS extension;
  if (!ssl_client_hello_init(ssl, &client_hello_inner,
                             MakeConstSpan(CBB_data(&body), CBB_len(&body))) ||
      !ssl_client_hello_get_extension(&client_hello_inner, &extension,
                                      TLSEXT_TYPE_ech_is_inner) ||
      CBS_len(&extension) != 0 ||
      ssl_client_hello_get_extension(&client_hello_inner, &extension,
                                     TLSEXT_TYPE_encrypted_client_hello) ||
      !ssl_client_hello_get_extension(&client_hello_inner, &extension,
                                      TLSEXT_TYPE_supported_versions)) {
    *out_alert = SSL_AD_ILLEGAL_PARAMETER;
    OPENSSL_PUT_ERROR(SSL, SSL_R_INVALID_CLIENT_HELLO_INNER);
    return false;
  }
  // Parse supported_versions and reject TLS versions prior to TLS 1.3. Older
  // versions are incompatible with ECH.
  CBS versions;
  if (!CBS_get_u8_length_prefixed(&extension, &versions) ||
      CBS_len(&extension) != 0 ||  //
      CBS_len(&versions) == 0) {
    *out_alert = SSL_AD_DECODE_ERROR;
    OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
    return false;
  }
  while (CBS_len(&versions) != 0) {
    uint16_t version;
    if (!CBS_get_u16(&versions, &version)) {
      *out_alert = SSL_AD_DECODE_ERROR;
      OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
      return false;
    }
    if (version == SSL3_VERSION || version == TLS1_VERSION ||
        version == TLS1_1_VERSION || version == TLS1_2_VERSION ||
        version == DTLS1_VERSION || version == DTLS1_2_VERSION) {
      *out_alert = SSL_AD_ILLEGAL_PARAMETER;
      OPENSSL_PUT_ERROR(SSL, SSL_R_INVALID_CLIENT_HELLO_INNER);
      return false;
    }
  }

  if (!ssl->method->finish_message(ssl, cbb.get(), out_client_hello_inner)) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
    return false;
  }
  return true;
}

bool ssl_client_hello_decrypt(
    EVP_HPKE_CTX *hpke_ctx, Array<uint8_t> *out_encoded_client_hello_inner,
    bool *out_is_decrypt_error, const SSL_CLIENT_HELLO *client_hello_outer,
    uint16_t kdf_id, uint16_t aead_id, const uint8_t config_id,
    Span<const uint8_t> enc, Span<const uint8_t> payload) {
  *out_is_decrypt_error = false;

  // Compute the ClientHello portion of the ClientHelloOuterAAD value. See
  // draft-ietf-tls-esni-10, section 5.2.
  ScopedCBB aad;
  CBB enc_cbb, outer_hello_cbb, extensions_cbb;
  if (!CBB_init(aad.get(), 256) ||
      !CBB_add_u16(aad.get(), kdf_id) ||
      !CBB_add_u16(aad.get(), aead_id) ||
      !CBB_add_u8(aad.get(), config_id) ||
      !CBB_add_u16_length_prefixed(aad.get(), &enc_cbb) ||
      !CBB_add_bytes(&enc_cbb, enc.data(), enc.size()) ||
      !CBB_add_u24_length_prefixed(aad.get(), &outer_hello_cbb) ||
      !ssl_client_hello_write_without_extensions(client_hello_outer,
                                                 &outer_hello_cbb) ||
      !CBB_add_u16_length_prefixed(&outer_hello_cbb, &extensions_cbb)) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
    return false;
  }

  CBS extensions(MakeConstSpan(client_hello_outer->extensions,
                               client_hello_outer->extensions_len));
  while (CBS_len(&extensions) > 0) {
    uint16_t extension_id;
    CBS extension_body;
    if (!CBS_get_u16(&extensions, &extension_id) ||
        !CBS_get_u16_length_prefixed(&extensions, &extension_body)) {
      OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
      return false;
    }
    if (extension_id == TLSEXT_TYPE_encrypted_client_hello) {
      continue;
    }
    if (!CBB_add_u16(&extensions_cbb, extension_id) ||
        !CBB_add_u16(&extensions_cbb, CBS_len(&extension_body)) ||
        !CBB_add_bytes(&extensions_cbb, CBS_data(&extension_body),
                       CBS_len(&extension_body))) {
      OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
      return false;
    }
  }
  if (!CBB_flush(aad.get())) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
    return false;
  }

#if defined(BORINGSSL_UNSAFE_FUZZER_MODE)
  // In fuzzer mode, disable encryption to improve coverage. We reserve a short
  // input to signal decryption failure, so the fuzzer can explore fallback to
  // ClientHelloOuter.
  const uint8_t kBadPayload[] = {0xff};
  if (payload == kBadPayload) {
    *out_is_decrypt_error = true;
    OPENSSL_PUT_ERROR(SSL, SSL_R_DECRYPTION_FAILED);
    return false;
  }
  if (!out_encoded_client_hello_inner->CopyFrom(payload)) {
    return false;
  }
#else
  // Attempt to decrypt into |out_encoded_client_hello_inner|.
  if (!out_encoded_client_hello_inner->Init(payload.size())) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
    return false;
  }
  size_t encoded_client_hello_inner_len;
  if (!EVP_HPKE_CTX_open(hpke_ctx, out_encoded_client_hello_inner->data(),
                         &encoded_client_hello_inner_len,
                         out_encoded_client_hello_inner->size(), payload.data(),
                         payload.size(), CBB_data(aad.get()),
                         CBB_len(aad.get()))) {
    *out_is_decrypt_error = true;
    OPENSSL_PUT_ERROR(SSL, SSL_R_DECRYPTION_FAILED);
    return false;
  }
  out_encoded_client_hello_inner->Shrink(encoded_client_hello_inner_len);
#endif
  return true;
}

static bool parse_ipv4_number(Span<const uint8_t> in, uint32_t *out) {
  // See https://url.spec.whatwg.org/#ipv4-number-parser.
  uint32_t base = 10;
  if (in.size() >= 2 && in[0] == '0' && (in[1] == 'x' || in[1] == 'X')) {
    in = in.subspan(2);
    base = 16;
  } else if (in.size() >= 1 && in[0] == '0') {
    in = in.subspan(1);
    base = 8;
  }
  *out = 0;
  for (uint8_t c : in) {
    uint32_t d;
    if ('0' <= c && c <= '9') {
      d = c - '0';
    } else if ('a' <= c && c <= 'f') {
      d = c - 'a' + 10;
    } else if ('A' <= c && c <= 'F') {
      d = c - 'A' + 10;
    } else {
      return false;
    }
    if (d >= base ||
        *out > UINT32_MAX / base) {
      return false;
    }
    *out *= base;
    if (*out > UINT32_MAX - d) {
      return false;
    }
    *out += d;
  }
  return true;
}

static bool is_ipv4_address(Span<const uint8_t> in) {
  // See https://url.spec.whatwg.org/#concept-ipv4-parser
  uint32_t numbers[4];
  size_t num_numbers = 0;
  while (!in.empty()) {
    if (num_numbers == 4) {
      // Too many components.
      return false;
    }
    // Find the next dot-separated component.
    auto dot = std::find(in.begin(), in.end(), '.');
    if (dot == in.begin()) {
      // Empty components are not allowed.
      return false;
    }
    Span<const uint8_t> component;
    if (dot == in.end()) {
      component = in;
      in = Span<const uint8_t>();
    } else {
      component = in.subspan(0, dot - in.begin());
      in = in.subspan(dot - in.begin() + 1);  // Skip the dot.
    }
    if (!parse_ipv4_number(component, &numbers[num_numbers])) {
      return false;
    }
    num_numbers++;
  }
  if (num_numbers == 0) {
    return false;
  }
  for (size_t i = 0; i < num_numbers - 1; i++) {
    if (numbers[i] > 255) {
      return false;
    }
  }
  return num_numbers == 1 ||
         numbers[num_numbers - 1] < 1u << (8 * (5 - num_numbers));
}

bool ssl_is_valid_ech_public_name(Span<const uint8_t> public_name) {
  // See draft-ietf-tls-esni-11, Section 4 and RFC5890, Section 2.3.1. The
  // public name must be a dot-separated sequence of LDH labels and not begin or
  // end with a dot.
  auto copy = public_name;
  if (copy.empty()) {
    return false;
  }
  while (!copy.empty()) {
    // Find the next dot-separated component.
    auto dot = std::find(copy.begin(), copy.end(), '.');
    Span<const uint8_t> component;
    if (dot == copy.end()) {
      component = copy;
      copy = Span<const uint8_t>();
    } else {
      component = copy.subspan(0, dot - copy.begin());
      copy = copy.subspan(dot - copy.begin() + 1);  // Skip the dot.
      if (copy.empty()) {
        // Trailing dots are not allowed.
        return false;
      }
    }
    // |component| must be a valid LDH label. Checking for empty components also
    // rejects leading dots.
    if (component.empty() || component.size() > 63 ||
        component.front() == '-' || component.back() == '-') {
      return false;
    }
    for (uint8_t c : component) {
      if (!('a' <= c && c <= 'z') && !('A' <= c && c <= 'Z') &&
          !('0' <= c && c <= '9') && c != '-') {
        return false;
      }
    }
  }

  return !is_ipv4_address(public_name);
}

static bool parse_ech_config(CBS *cbs, ECHConfig *out, bool *out_supported,
                             bool all_extensions_mandatory) {
  uint16_t version;
  CBS orig = *cbs;
  CBS contents;
  if (!CBS_get_u16(cbs, &version) ||
      !CBS_get_u16_length_prefixed(cbs, &contents)) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
    return false;
  }

  if (version != kECHConfigVersion) {
    *out_supported = false;
    return true;
  }

  // Make a copy of the ECHConfig and parse from it, so the results alias into
  // the saved copy.
  if (!out->raw.CopyFrom(
          MakeConstSpan(CBS_data(&orig), CBS_len(&orig) - CBS_len(cbs)))) {
    return false;
  }

  CBS ech_config(out->raw);
  CBS public_name, public_key, cipher_suites, extensions;
  if (!CBS_skip(&ech_config, 2) || // version
      !CBS_get_u16_length_prefixed(&ech_config, &contents) ||
      !CBS_get_u8(&contents, &out->config_id) ||
      !CBS_get_u16(&contents, &out->kem_id) ||
      !CBS_get_u16_length_prefixed(&contents, &public_key) ||
      CBS_len(&public_key) == 0 ||
      !CBS_get_u16_length_prefixed(&contents, &cipher_suites) ||
      CBS_len(&cipher_suites) == 0 || CBS_len(&cipher_suites) % 4 != 0 ||
      !CBS_get_u16(&contents, &out->maximum_name_length) ||
      !CBS_get_u16_length_prefixed(&contents, &public_name) ||
      CBS_len(&public_name) == 0 ||
      !CBS_get_u16_length_prefixed(&contents, &extensions) ||
      CBS_len(&contents) != 0) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
    return false;
  }

  if (!ssl_is_valid_ech_public_name(public_name)) {
    // TODO(https://crbug.com/boringssl/275): The draft says ECHConfigs with
    // invalid public names should be ignored, but LDH syntax failures are
    // unambiguously invalid.
    *out_supported = false;
    return true;
  }

  out->public_key = public_key;
  out->public_name = public_name;
  // This function does not ensure |out->kem_id| and |out->cipher_suites| use
  // supported algorithms. The caller must do this.
  out->cipher_suites = cipher_suites;

  bool has_unknown_mandatory_extension = false;
  while (CBS_len(&extensions) != 0) {
    uint16_t type;
    CBS body;
    if (!CBS_get_u16(&extensions, &type) ||
        !CBS_get_u16_length_prefixed(&extensions, &body)) {
      OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
      return false;
    }
    // We currently do not support any extensions.
    if (type & 0x8000 || all_extensions_mandatory) {
      // Extension numbers with the high bit set are mandatory. Continue parsing
      // to enforce syntax, but we will ultimately ignore this ECHConfig as a
      // client and reject it as a server.
      has_unknown_mandatory_extension = true;
    }
  }

  *out_supported = !has_unknown_mandatory_extension;
  return true;
}

bool ECHServerConfig::Init(Span<const uint8_t> ech_config,
                           const EVP_HPKE_KEY *key, bool is_retry_config) {
  is_retry_config_ = is_retry_config;

  // Parse the ECHConfig, rejecting all unsupported parameters and extensions.
  // Unlike most server options, ECH's server configuration is serialized and
  // configured in both the server and DNS. If the caller configures an
  // unsupported parameter, this is a deployment error. To catch these errors,
  // we fail early.
  CBS cbs = ech_config;
  bool supported;
  if (!parse_ech_config(&cbs, &ech_config_, &supported,
                        /*all_extensions_mandatory=*/true)) {
    return false;
  }
  if (CBS_len(&cbs) != 0) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
    return false;
  }
  if (!supported) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_UNSUPPORTED_ECH_SERVER_CONFIG);
    return false;
  }

  CBS cipher_suites = ech_config_.cipher_suites;
  while (CBS_len(&cipher_suites) > 0) {
    uint16_t kdf_id, aead_id;
    if (!CBS_get_u16(&cipher_suites, &kdf_id) ||
        !CBS_get_u16(&cipher_suites, &aead_id)) {
      OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
      return false;
    }
    // The server promises to support every option in the ECHConfig, so reject
    // any unsupported cipher suites.
    if (kdf_id != EVP_HPKE_HKDF_SHA256 || get_ech_aead(aead_id) == nullptr) {
      OPENSSL_PUT_ERROR(SSL, SSL_R_UNSUPPORTED_ECH_SERVER_CONFIG);
      return false;
    }
  }

  // Check the public key in the ECHConfig matches |key|.
  uint8_t expected_public_key[EVP_HPKE_MAX_PUBLIC_KEY_LENGTH];
  size_t expected_public_key_len;
  if (!EVP_HPKE_KEY_public_key(key, expected_public_key,
                               &expected_public_key_len,
                               sizeof(expected_public_key))) {
    return false;
  }
  if (ech_config_.kem_id != EVP_HPKE_KEM_id(EVP_HPKE_KEY_kem(key)) ||
      MakeConstSpan(expected_public_key, expected_public_key_len) !=
          ech_config_.public_key) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_ECH_SERVER_CONFIG_AND_PRIVATE_KEY_MISMATCH);
    return false;
  }

  if (!EVP_HPKE_KEY_copy(key_.get(), key)) {
    return false;
  }

  return true;
}

bool ECHServerConfig::SetupContext(EVP_HPKE_CTX *ctx, uint16_t kdf_id,
                                   uint16_t aead_id,
                                   Span<const uint8_t> enc) const {
  // Check the cipher suite is supported by this ECHServerConfig.
  CBS cbs(ech_config_.cipher_suites);
  bool cipher_ok = false;
  while (CBS_len(&cbs) != 0) {
    uint16_t supported_kdf_id, supported_aead_id;
    if (!CBS_get_u16(&cbs, &supported_kdf_id) ||
        !CBS_get_u16(&cbs, &supported_aead_id)) {
      return false;
    }
    if (kdf_id == supported_kdf_id && aead_id == supported_aead_id) {
      cipher_ok = true;
      break;
    }
  }
  if (!cipher_ok) {
    return false;
  }

  static const uint8_t kInfoLabel[] = "tls ech";
  ScopedCBB info_cbb;
  if (!CBB_init(info_cbb.get(), sizeof(kInfoLabel) + ech_config_.raw.size()) ||
      !CBB_add_bytes(info_cbb.get(), kInfoLabel,
                     sizeof(kInfoLabel) /* includes trailing NUL */) ||
      !CBB_add_bytes(info_cbb.get(), ech_config_.raw.data(),
                     ech_config_.raw.size())) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
    return false;
  }

  assert(kdf_id == EVP_HPKE_HKDF_SHA256);
  assert(get_ech_aead(aead_id) != NULL);
  return EVP_HPKE_CTX_setup_recipient(
      ctx, key_.get(), EVP_hpke_hkdf_sha256(), get_ech_aead(aead_id), enc.data(),
      enc.size(), CBB_data(info_cbb.get()), CBB_len(info_cbb.get()));
}

bool ssl_is_valid_ech_config_list(Span<const uint8_t> ech_config_list) {
  CBS cbs = ech_config_list, child;
  if (!CBS_get_u16_length_prefixed(&cbs, &child) ||  //
      CBS_len(&child) == 0 ||                        //
      CBS_len(&cbs) > 0) {
    return false;
  }
  while (CBS_len(&child) > 0) {
    ECHConfig ech_config;
    bool supported;
    if (!parse_ech_config(&child, &ech_config, &supported,
                          /*all_extensions_mandatory=*/false)) {
      return false;
    }
  }
  return true;
}

static bool select_ech_cipher_suite(const EVP_HPKE_KDF **out_kdf,
                                    const EVP_HPKE_AEAD **out_aead,
                                    Span<const uint8_t> cipher_suites) {
  const bool has_aes_hardware = EVP_has_aes_hardware();
  const EVP_HPKE_AEAD *aead = nullptr;
  CBS cbs = cipher_suites;
  while (CBS_len(&cbs) != 0) {
    uint16_t kdf_id, aead_id;
    if (!CBS_get_u16(&cbs, &kdf_id) ||  //
        !CBS_get_u16(&cbs, &aead_id)) {
      return false;
    }
    // Pick the first common cipher suite, but prefer ChaCha20-Poly1305 if we
    // don't have AES hardware.
    const EVP_HPKE_AEAD *candidate = get_ech_aead(aead_id);
    if (kdf_id != EVP_HPKE_HKDF_SHA256 || candidate == nullptr) {
      continue;
    }
    if (aead == nullptr ||
        (!has_aes_hardware && aead_id == EVP_HPKE_CHACHA20_POLY1305)) {
      aead = candidate;
    }
  }
  if (aead == nullptr) {
    return false;
  }

  *out_kdf = EVP_hpke_hkdf_sha256();
  *out_aead = aead;
  return true;
}

bool ssl_select_ech_config(SSL_HANDSHAKE *hs, Span<uint8_t> out_enc,
                           size_t *out_enc_len) {
  *out_enc_len = 0;
  if (hs->max_version < TLS1_3_VERSION) {
    // ECH requires TLS 1.3.
    return true;
  }

  if (!hs->config->client_ech_config_list.empty()) {
    CBS cbs = MakeConstSpan(hs->config->client_ech_config_list);
    CBS child;
    if (!CBS_get_u16_length_prefixed(&cbs, &child) ||  //
        CBS_len(&child) == 0 ||                        //
        CBS_len(&cbs) > 0) {
      return false;
    }
    // Look for the first ECHConfig with supported parameters.
    while (CBS_len(&child) > 0) {
      ECHConfig ech_config;
      bool supported;
      if (!parse_ech_config(&child, &ech_config, &supported,
                            /*all_extensions_mandatory=*/false)) {
        return false;
      }
      const EVP_HPKE_KEM *kem = EVP_hpke_x25519_hkdf_sha256();
      const EVP_HPKE_KDF *kdf;
      const EVP_HPKE_AEAD *aead;
      if (supported &&  //
          ech_config.kem_id == EVP_HPKE_DHKEM_X25519_HKDF_SHA256 &&
          select_ech_cipher_suite(&kdf, &aead, ech_config.cipher_suites)) {
        ScopedCBB info;
        static const uint8_t kInfoLabel[] = "tls ech";  // includes trailing NUL
        if (!CBB_init(info.get(), sizeof(kInfoLabel) + ech_config.raw.size()) ||
            !CBB_add_bytes(info.get(), kInfoLabel, sizeof(kInfoLabel)) ||
            !CBB_add_bytes(info.get(), ech_config.raw.data(),
                           ech_config.raw.size())) {
          OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
          return false;
        }

        if (!EVP_HPKE_CTX_setup_sender(
                hs->ech_hpke_ctx.get(), out_enc.data(), out_enc_len,
                out_enc.size(), kem, kdf, aead, ech_config.public_key.data(),
                ech_config.public_key.size(), CBB_data(info.get()),
                CBB_len(info.get())) ||
            !hs->inner_transcript.Init()) {
          return false;
        }

        hs->selected_ech_config = MakeUnique<ECHConfig>(std::move(ech_config));
        return hs->selected_ech_config != nullptr;
      }
    }
  }

  return true;
}

static size_t aead_overhead(const EVP_HPKE_AEAD *aead) {
#if defined(BORINGSSL_UNSAFE_FUZZER_MODE)
  // TODO(https://crbug.com/boringssl/275): Having to adjust the overhead
  // everywhere is tedious. Change fuzzer mode to append a fake tag but still
  // otherwise be cleartext, refresh corpora, and then inline this function.
  return 0;
#else
  return EVP_AEAD_max_overhead(EVP_HPKE_AEAD_aead(aead));
#endif
}

static size_t compute_extension_length(const EVP_HPKE_AEAD *aead,
                                       size_t enc_len, size_t in_len) {
  size_t ret = 4;      // HpkeSymmetricCipherSuite cipher_suite
  ret++;               // uint8 config_id
  ret += 2 + enc_len;  // opaque enc<1..2^16-1>
  ret += 2 + in_len + aead_overhead(aead);  // opaque payload<1..2^16-1>
  return ret;
}

// random_size returns a random value between |min| and |max|, inclusive.
static size_t random_size(size_t min, size_t max) {
  assert(min < max);
  size_t value;
  RAND_bytes(reinterpret_cast<uint8_t *>(&value), sizeof(value));
  return value % (max - min + 1) + min;
}

static bool setup_ech_grease(SSL_HANDSHAKE *hs) {
  assert(!hs->selected_ech_config);
  if (hs->max_version < TLS1_3_VERSION || !hs->config->ech_grease_enabled) {
    return true;
  }

  const uint16_t kdf_id = EVP_HPKE_HKDF_SHA256;
  const EVP_HPKE_AEAD *aead = EVP_has_aes_hardware()
                                  ? EVP_hpke_aes_128_gcm()
                                  : EVP_hpke_chacha20_poly1305();
  static_assert(ssl_grease_ech_config_id < sizeof(hs->grease_seed),
                "hs->grease_seed is too small");
  uint8_t config_id = hs->grease_seed[ssl_grease_ech_config_id];

  uint8_t enc[X25519_PUBLIC_VALUE_LEN];
  uint8_t private_key_unused[X25519_PRIVATE_KEY_LEN];
  X25519_keypair(enc, private_key_unused);

  // To determine a plausible length for the payload, we estimate the size of a
  // typical EncodedClientHelloInner without resumption:
  //
  //   2+32+1+2   version, random, legacy_session_id, legacy_compression_methods
  //   2+4*2      cipher_suites (three TLS 1.3 ciphers, GREASE)
  //   2          extensions prefix
  //   4          ech_is_inner
  //   4+1+2*2    supported_versions (TLS 1.3, GREASE)
  //   4+1+10*2   outer_extensions (key_share, sigalgs, sct, alpn,
  //              supported_groups, status_request, psk_key_exchange_modes,
  //              compress_certificate, GREASE x2)
  //
  // The server_name extension has an overhead of 9 bytes. For now, arbitrarily
  // estimate maximum_name_length to be between 32 and 100 bytes.
  //
  // TODO(https://crbug.com/boringssl/275): If the padding scheme changes to
  // also round the entire payload, adjust this to match. See
  // https://github.com/tlswg/draft-ietf-tls-esni/issues/433
  const size_t overhead = aead_overhead(aead);
  const size_t in_len = random_size(128, 196);
  const size_t extension_len =
      compute_extension_length(aead, sizeof(enc), in_len);
  bssl::ScopedCBB cbb;
  CBB enc_cbb, payload_cbb;
  uint8_t *payload;
  if (!CBB_init(cbb.get(), extension_len) ||
      !CBB_add_u16(cbb.get(), kdf_id) ||
      !CBB_add_u16(cbb.get(), EVP_HPKE_AEAD_id(aead)) ||
      !CBB_add_u8(cbb.get(), config_id) ||
      !CBB_add_u16_length_prefixed(cbb.get(), &enc_cbb) ||
      !CBB_add_bytes(&enc_cbb, enc, sizeof(enc)) ||
      !CBB_add_u16_length_prefixed(cbb.get(), &payload_cbb) ||
      !CBB_add_space(&payload_cbb, &payload, in_len + overhead) ||
      !RAND_bytes(payload, in_len + overhead) ||
      !CBBFinishArray(cbb.get(), &hs->ech_client_bytes)) {
    return false;
  }
  assert(hs->ech_client_bytes.size() == extension_len);
  return true;
}

bool ssl_encrypt_client_hello(SSL_HANDSHAKE *hs, Span<const uint8_t> enc) {
  SSL *const ssl = hs->ssl;
  if (!hs->selected_ech_config) {
    return setup_ech_grease(hs);
  }

  // Construct ClientHelloInner and EncodedClientHelloInner. See
  // draft-ietf-tls-esni-10, sections 5.1 and 6.1.
  bssl::ScopedCBB cbb, encoded;
  CBB body;
  bool needs_psk_binder;
  bssl::Array<uint8_t> hello_inner;
  if (!ssl->method->init_message(ssl, cbb.get(), &body, SSL3_MT_CLIENT_HELLO) ||
      !CBB_init(encoded.get(), 256) ||
      !ssl_write_client_hello_without_extensions(hs, &body,
                                                 ssl_client_hello_inner,
                                                 /*empty_session_id=*/false) ||
      !ssl_write_client_hello_without_extensions(hs, encoded.get(),
                                                 ssl_client_hello_inner,
                                                 /*empty_session_id=*/true) ||
      !ssl_add_clienthello_tlsext(hs, &body, encoded.get(), &needs_psk_binder,
                                  ssl_client_hello_inner, CBB_len(&body),
                                  /*omit_ech_len=*/0) ||
      !ssl->method->finish_message(ssl, cbb.get(), &hello_inner)) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
    return false;
  }

  if (needs_psk_binder) {
    size_t binder_len;
    if (!tls13_write_psk_binder(hs, hs->inner_transcript, MakeSpan(hello_inner),
                                &binder_len)) {
      return false;
    }
    // Also update the EncodedClientHelloInner.
    if (CBB_len(encoded.get()) < binder_len) {
      OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
      return false;
    }
    OPENSSL_memcpy(const_cast<uint8_t *>(CBB_data(encoded.get())) +
                       CBB_len(encoded.get()) - binder_len,
                   hello_inner.data() + hello_inner.size() - binder_len,
                   binder_len);
  }

  if (!hs->inner_transcript.Update(hello_inner)) {
    return false;
  }

  // Construct ClientHelloOuterAAD. See draft-ietf-tls-esni-10, section 5.2.
  // TODO(https://crbug.com/boringssl/275): This ends up constructing the
  // ClientHelloOuter twice. Revisit this in the next draft, which uses a more
  // forgiving construction.
  const EVP_HPKE_KDF *kdf = EVP_HPKE_CTX_kdf(hs->ech_hpke_ctx.get());
  const EVP_HPKE_AEAD *aead = EVP_HPKE_CTX_aead(hs->ech_hpke_ctx.get());
  const size_t extension_len =
      compute_extension_length(aead, enc.size(), CBB_len(encoded.get()));
  bssl::ScopedCBB aad;
  CBB outer_hello;
  CBB enc_cbb;
  if (!CBB_init(aad.get(), 256) ||
      !CBB_add_u16(aad.get(), EVP_HPKE_KDF_id(kdf)) ||
      !CBB_add_u16(aad.get(), EVP_HPKE_AEAD_id(aead)) ||
      !CBB_add_u8(aad.get(), hs->selected_ech_config->config_id) ||
      !CBB_add_u16_length_prefixed(aad.get(), &enc_cbb) ||
      !CBB_add_bytes(&enc_cbb, enc.data(), enc.size()) ||
      !CBB_add_u24_length_prefixed(aad.get(), &outer_hello) ||
      !ssl_write_client_hello_without_extensions(hs, &outer_hello,
                                                 ssl_client_hello_outer,
                                                 /*empty_session_id=*/false) ||
      !ssl_add_clienthello_tlsext(hs, &outer_hello, /*out_encoded=*/nullptr,
                                  &needs_psk_binder, ssl_client_hello_outer,
                                  CBB_len(&outer_hello),
                                  /*omit_ech_len=*/4 + extension_len) ||
      !CBB_flush(aad.get())) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
    return false;
  }
  // ClientHelloOuter may not require a PSK binder. Otherwise, we have a
  // circular dependency.
  assert(!needs_psk_binder);

  CBB payload_cbb;
  if (!CBB_init(cbb.get(), extension_len) ||
      !CBB_add_u16(cbb.get(), EVP_HPKE_KDF_id(kdf)) ||
      !CBB_add_u16(cbb.get(), EVP_HPKE_AEAD_id(aead)) ||
      !CBB_add_u8(cbb.get(), hs->selected_ech_config->config_id) ||
      !CBB_add_u16_length_prefixed(cbb.get(), &enc_cbb) ||
      !CBB_add_bytes(&enc_cbb, enc.data(), enc.size()) ||
      !CBB_add_u16_length_prefixed(cbb.get(), &payload_cbb)) {
    return false;
  }
#if defined(BORINGSSL_UNSAFE_FUZZER_MODE)
  // In fuzzer mode, the server expects a cleartext payload.
  if (!CBB_add_bytes(&payload_cbb, CBB_data(encoded.get()),
                     CBB_len(encoded.get()))) {
    return false;
  }
#else
  uint8_t *payload;
  size_t payload_len =
      CBB_len(encoded.get()) + EVP_AEAD_max_overhead(EVP_HPKE_AEAD_aead(aead));
  if (!CBB_reserve(&payload_cbb, &payload, payload_len) ||
      !EVP_HPKE_CTX_seal(hs->ech_hpke_ctx.get(), payload, &payload_len,
                         payload_len, CBB_data(encoded.get()),
                         CBB_len(encoded.get()), CBB_data(aad.get()),
                         CBB_len(aad.get())) ||
      !CBB_did_write(&payload_cbb, payload_len)) {
    return false;
  }
#endif // BORINGSSL_UNSAFE_FUZZER_MODE
  if (!CBBFinishArray(cbb.get(), &hs->ech_client_bytes)) {
    return false;
  }

  // The |aad| calculation relies on |extension_length| being correct.
  assert(hs->ech_client_bytes.size() == extension_len);
  return true;
}

BSSL_NAMESPACE_END

using namespace bssl;

void SSL_set_enable_ech_grease(SSL *ssl, int enable) {
  if (!ssl->config) {
    return;
  }
  ssl->config->ech_grease_enabled = !!enable;
}

int SSL_set1_ech_config_list(SSL *ssl, const uint8_t *ech_config_list,
                             size_t ech_config_list_len) {
  if (!ssl->config) {
    return 0;
  }

  auto span = MakeConstSpan(ech_config_list, ech_config_list_len);
  if (!ssl_is_valid_ech_config_list(span)) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_INVALID_ECH_CONFIG_LIST);
    return 0;
  }
  return ssl->config->client_ech_config_list.CopyFrom(span);
}

int SSL_marshal_ech_config(uint8_t **out, size_t *out_len, uint8_t config_id,
                           const EVP_HPKE_KEY *key, const char *public_name,
                           size_t max_name_len) {
  Span<const uint8_t> public_name_u8 = MakeConstSpan(
      reinterpret_cast<const uint8_t *>(public_name), strlen(public_name));
  if (!ssl_is_valid_ech_public_name(public_name_u8)) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_INVALID_ECH_PUBLIC_NAME);
    return 0;
  }

  // See draft-ietf-tls-esni-10, section 4.
  ScopedCBB cbb;
  CBB contents, child;
  uint8_t *public_key;
  size_t public_key_len;
  if (!CBB_init(cbb.get(), 128) ||  //
      !CBB_add_u16(cbb.get(), kECHConfigVersion) ||
      !CBB_add_u16_length_prefixed(cbb.get(), &contents) ||
      !CBB_add_u8(&contents, config_id) ||
      !CBB_add_u16(&contents, EVP_HPKE_KEM_id(EVP_HPKE_KEY_kem(key))) ||
      !CBB_add_u16_length_prefixed(&contents, &child) ||
      !CBB_reserve(&child, &public_key, EVP_HPKE_MAX_PUBLIC_KEY_LENGTH) ||
      !EVP_HPKE_KEY_public_key(key, public_key, &public_key_len,
                               EVP_HPKE_MAX_PUBLIC_KEY_LENGTH) ||
      !CBB_did_write(&child, public_key_len) ||
      !CBB_add_u16_length_prefixed(&contents, &child) ||
      // Write a default cipher suite configuration.
      !CBB_add_u16(&child, EVP_HPKE_HKDF_SHA256) ||
      !CBB_add_u16(&child, EVP_HPKE_AES_128_GCM) ||
      !CBB_add_u16(&child, EVP_HPKE_HKDF_SHA256) ||
      !CBB_add_u16(&child, EVP_HPKE_CHACHA20_POLY1305) ||
      !CBB_add_u16(&contents, max_name_len) ||
      !CBB_add_u16_length_prefixed(&contents, &child) ||
      !CBB_add_bytes(&child, public_name_u8.data(), public_name_u8.size()) ||
      // TODO(https://crbug.com/boringssl/275): Reserve some GREASE extensions
      // and include some.
      !CBB_add_u16(&contents, 0 /* no extensions */) ||
      !CBB_finish(cbb.get(), out, out_len)) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_INTERNAL_ERROR);
    return 0;
  }
  return 1;
}

SSL_ECH_KEYS *SSL_ECH_KEYS_new() { return New<SSL_ECH_KEYS>(); }

void SSL_ECH_KEYS_up_ref(SSL_ECH_KEYS *keys) {
  CRYPTO_refcount_inc(&keys->references);
}

void SSL_ECH_KEYS_free(SSL_ECH_KEYS *keys) {
  if (keys == nullptr ||
      !CRYPTO_refcount_dec_and_test_zero(&keys->references)) {
    return;
  }

  keys->~ssl_ech_keys_st();
  OPENSSL_free(keys);
}

int SSL_ECH_KEYS_add(SSL_ECH_KEYS *configs, int is_retry_config,
                     const uint8_t *ech_config, size_t ech_config_len,
                     const EVP_HPKE_KEY *key) {
  UniquePtr<ECHServerConfig> parsed_config = MakeUnique<ECHServerConfig>();
  if (!parsed_config) {
    return 0;
  }
  if (!parsed_config->Init(MakeConstSpan(ech_config, ech_config_len), key,
                           !!is_retry_config)) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_DECODE_ERROR);
    return 0;
  }
  if (!configs->configs.Push(std::move(parsed_config))) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
    return 0;
  }
  return 1;
}

int SSL_ECH_KEYS_has_duplicate_config_id(const SSL_ECH_KEYS *keys) {
  bool seen[256] = {false};
  for (const auto &config : keys->configs) {
    if (seen[config->ech_config().config_id]) {
      return 1;
    }
    seen[config->ech_config().config_id] = true;
  }
  return 0;
}

int SSL_ECH_KEYS_marshal_retry_configs(const SSL_ECH_KEYS *keys, uint8_t **out,
                                       size_t *out_len) {
  ScopedCBB cbb;
  CBB child;
  if (!CBB_init(cbb.get(), 128) ||
      !CBB_add_u16_length_prefixed(cbb.get(), &child)) {
    OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
    return false;
  }
  for (const auto &config : keys->configs) {
    if (config->is_retry_config() &&
        !CBB_add_bytes(&child, config->ech_config().raw.data(),
                       config->ech_config().raw.size())) {
      OPENSSL_PUT_ERROR(SSL, ERR_R_MALLOC_FAILURE);
      return false;
    }
  }
  return CBB_finish(cbb.get(), out, out_len);
}

int SSL_CTX_set1_ech_keys(SSL_CTX *ctx, SSL_ECH_KEYS *keys) {
  bool has_retry_config = false;
  for (const auto &config : keys->configs) {
    if (config->is_retry_config()) {
      has_retry_config = true;
      break;
    }
  }
  if (!has_retry_config) {
    OPENSSL_PUT_ERROR(SSL, SSL_R_ECH_SERVER_WOULD_HAVE_NO_RETRY_CONFIGS);
    return 0;
  }
  UniquePtr<SSL_ECH_KEYS> owned_keys = UpRef(keys);
  MutexWriteLock lock(&ctx->lock);
  ctx->ech_keys.swap(owned_keys);
  return 1;
}

int SSL_ech_accepted(const SSL *ssl) {
  if (SSL_in_early_data(ssl) && !ssl->server) {
    // In the client early data state, we report properties as if the server
    // accepted early data. The server can only accept early data with
    // ClientHelloInner.
    return ssl->s3->hs->selected_ech_config != nullptr;
  }

  return ssl->s3->ech_accept;
}

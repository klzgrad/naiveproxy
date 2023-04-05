#include "quiche/oblivious_http/buffers/oblivious_http_response.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "openssl/aead.h"
#include "openssl/hkdf.h"
#include "openssl/hpke.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/quiche_crypto_logging.h"
#include "quiche/common/quiche_random.h"
#include "quiche/oblivious_http/common/oblivious_http_header_key_config.h"

namespace quiche {
namespace {
// Generate a random string.
void random(QuicheRandom* quiche_random, char* dest, size_t len) {
  if (quiche_random == nullptr) {
    quiche_random = QuicheRandom::GetInstance();
  }
  quiche_random->RandBytes(dest, len);
}
}  // namespace

// Ctor.
ObliviousHttpResponse::ObliviousHttpResponse(std::string encrypted_data,
                                             std::string resp_plaintext)
    : encrypted_data_(std::move(encrypted_data)),
      response_plaintext_(std::move(resp_plaintext)) {}

// Response Decapsulation.
// 1. Extract resp_nonce
// 2. Build prk (pseudorandom key) using HKDF_Extract
// 3. Derive aead_key using HKDF_Labeled_Expand
// 4. Derive aead_nonce using HKDF_Labeled_Expand
// 5. Setup AEAD context and Decrypt.
// https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#section-4.2-4
absl::StatusOr<ObliviousHttpResponse>
ObliviousHttpResponse::CreateClientObliviousResponse(
    std::string encrypted_data,
    ObliviousHttpRequest::Context& oblivious_http_request_context) {
  if (oblivious_http_request_context.hpke_context_ == nullptr) {
    return absl::FailedPreconditionError(
        "HPKE context wasn't initialized before proceeding with this Response "
        "Decapsulation on Client-side.");
  }
  size_t expected_key_len = EVP_HPKE_KEM_enc_len(
      EVP_HPKE_CTX_kem(oblivious_http_request_context.hpke_context_.get()));
  if (oblivious_http_request_context.encapsulated_key_.size() !=
      expected_key_len) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid len for encapsulated_key arg. Expected:", expected_key_len,
        " Actual:", oblivious_http_request_context.encapsulated_key_.size()));
  }
  if (encrypted_data.empty()) {
    return absl::InvalidArgumentError("Empty encrypted_data input param.");
  }

  absl::StatusOr<CommonAeadParamsResult> aead_params_st =
      GetCommonAeadParams(oblivious_http_request_context);
  if (!aead_params_st.ok()) {
    return aead_params_st.status();
  }

  // secret_len = [max(Nn, Nk)] where Nk and Nn are the length of AEAD
  // key and nonce associated with HPKE context.
  // https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#section-4.2-2.1
  size_t secret_len = aead_params_st.value().secret_len;
  if (encrypted_data.size() < secret_len) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid input response. Failed to parse required minimum "
                     "expected_len=",
                     secret_len, " bytes."));
  }
  // Extract response_nonce. Step 2
  // https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#section-4.2-2.2
  absl::string_view response_nonce =
      absl::string_view(encrypted_data).substr(0, secret_len);
  absl::string_view encrypted_response =
      absl::string_view(encrypted_data).substr(secret_len);

  // Steps (1, 3 to 5) + AEAD context SetUp before 6th step is performed in
  // CommonOperations.
  auto common_ops_st = CommonOperationsToEncapDecap(
      response_nonce, oblivious_http_request_context,
      aead_params_st.value().aead_key_len,
      aead_params_st.value().aead_nonce_len, aead_params_st.value().secret_len);
  if (!common_ops_st.ok()) {
    return common_ops_st.status();
  }

  std::string decrypted(encrypted_response.size(), '\0');
  size_t decrypted_len;

  // Decrypt with initialized AEAD context.
  // response, error = Open(aead_key, aead_nonce, "", ct)
  // https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#section-4.2-6
  if (!EVP_AEAD_CTX_open(
          common_ops_st.value().aead_ctx.get(),
          reinterpret_cast<uint8_t*>(decrypted.data()), &decrypted_len,
          decrypted.size(),
          reinterpret_cast<const uint8_t*>(
              common_ops_st.value().aead_nonce.data()),
          aead_params_st.value().aead_nonce_len,
          reinterpret_cast<const uint8_t*>(encrypted_response.data()),
          encrypted_response.size(), nullptr, 0)) {
    return SslErrorAsStatus(
        "Failed to decrypt the response with derived AEAD key and nonce.");
  }
  decrypted.resize(decrypted_len);
  ObliviousHttpResponse oblivious_response(std::move(encrypted_data),
                                           std::move(decrypted));
  return oblivious_response;
}

// Response Encapsulation.
// Follows the Ohttp spec section-4.2 (Encapsulation of Responses) Ref
// https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#section-4.2
// Use HPKE context from BoringSSL to export a secret and use it to Seal (AKA
// encrypt) the response back to the Sender(client)
absl::StatusOr<ObliviousHttpResponse>
ObliviousHttpResponse::CreateServerObliviousResponse(
    std::string plaintext_payload,
    ObliviousHttpRequest::Context& oblivious_http_request_context,
    QuicheRandom* quiche_random) {
  if (oblivious_http_request_context.hpke_context_ == nullptr) {
    return absl::FailedPreconditionError(
        "HPKE context wasn't initialized before proceeding with this Response "
        "Encapsulation on Server-side.");
  }
  size_t expected_key_len = EVP_HPKE_KEM_enc_len(
      EVP_HPKE_CTX_kem(oblivious_http_request_context.hpke_context_.get()));
  if (oblivious_http_request_context.encapsulated_key_.size() !=
      expected_key_len) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid len for encapsulated_key arg. Expected:", expected_key_len,
        " Actual:", oblivious_http_request_context.encapsulated_key_.size()));
  }
  if (plaintext_payload.empty()) {
    return absl::InvalidArgumentError("Empty plaintext_payload input param.");
  }
  absl::StatusOr<CommonAeadParamsResult> aead_params_st =
      GetCommonAeadParams(oblivious_http_request_context);
  if (!aead_params_st.ok()) {
    return aead_params_st.status();
  }
  const size_t nonce_size = aead_params_st->secret_len;
  const size_t max_encrypted_data_size =
      nonce_size + plaintext_payload.size() +
      EVP_AEAD_max_overhead(EVP_HPKE_AEAD_aead(EVP_HPKE_CTX_aead(
          oblivious_http_request_context.hpke_context_.get())));
  std::string encrypted_data(max_encrypted_data_size, '\0');
  // response_nonce = random(max(Nn, Nk))
  // https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#section-4.2-2.2
  random(quiche_random, encrypted_data.data(), nonce_size);
  absl::string_view response_nonce =
      absl::string_view(encrypted_data).substr(0, nonce_size);

  // Steps (1, 3 to 5) + AEAD context SetUp before 6th step is performed in
  // CommonOperations.
  auto common_ops_st = CommonOperationsToEncapDecap(
      response_nonce, oblivious_http_request_context,
      aead_params_st.value().aead_key_len,
      aead_params_st.value().aead_nonce_len, aead_params_st.value().secret_len);
  if (!common_ops_st.ok()) {
    return common_ops_st.status();
  }

  // ct = Seal(aead_key, aead_nonce, "", response)
  // https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#section-4.2-2.6
  size_t ciphertext_len;
  if (!EVP_AEAD_CTX_seal(
          common_ops_st.value().aead_ctx.get(),
          reinterpret_cast<uint8_t*>(encrypted_data.data() + nonce_size),
          &ciphertext_len, encrypted_data.size() - nonce_size,
          reinterpret_cast<const uint8_t*>(
              common_ops_st.value().aead_nonce.data()),
          aead_params_st.value().aead_nonce_len,
          reinterpret_cast<const uint8_t*>(plaintext_payload.data()),
          plaintext_payload.size(), nullptr, 0)) {
    return SslErrorAsStatus(
        "Failed to encrypt the payload with derived AEAD key.");
  }
  encrypted_data.resize(nonce_size + ciphertext_len);
  if (nonce_size == 0 || ciphertext_len == 0) {
    return absl::InternalError(absl::StrCat(
        "ObliviousHttpResponse Object wasn't initialized with required fields.",
        (nonce_size == 0 ? "Generated nonce is empty." : ""),
        (ciphertext_len == 0 ? "Generated Encrypted payload is empty." : "")));
  }
  ObliviousHttpResponse oblivious_response(std::move(encrypted_data),
                                           std::move(plaintext_payload));
  return oblivious_response;
}

// Serialize.
// enc_response = concat(response_nonce, ct)
// https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#section-4.2-4
const std::string& ObliviousHttpResponse::EncapsulateAndSerialize() const {
  return encrypted_data_;
}

// Decrypted blob.
const std::string& ObliviousHttpResponse::GetPlaintextData() const {
  return response_plaintext_;
}

// This section mainly deals with common operations performed by both
// Sender(client) and Receiver(gateway) on ObliviousHttpResponse.

absl::StatusOr<ObliviousHttpResponse::CommonAeadParamsResult>
ObliviousHttpResponse::GetCommonAeadParams(
    ObliviousHttpRequest::Context& oblivious_http_request_context) {
  const EVP_AEAD* evp_hpke_aead = EVP_HPKE_AEAD_aead(
      EVP_HPKE_CTX_aead(oblivious_http_request_context.hpke_context_.get()));
  if (evp_hpke_aead == nullptr) {
    return absl::FailedPreconditionError(
        "Key Configuration not supported by HPKE AEADs. Check your key "
        "config.");
  }
  // Nk = [AEAD key len], is determined by BoringSSL.
  const size_t aead_key_len = EVP_AEAD_key_length(evp_hpke_aead);
  // Nn = [AEAD nonce len], is determined by BoringSSL.
  const size_t aead_nonce_len = EVP_AEAD_nonce_length(evp_hpke_aead);
  const size_t secret_len = std::max(aead_key_len, aead_nonce_len);
  CommonAeadParamsResult result{evp_hpke_aead, aead_key_len, aead_nonce_len,
                                secret_len};
  return result;
}

// Common Steps of AEAD key and AEAD nonce derivation common to both
// client(decapsulation) & Gateway(encapsulation) in handling
// Oblivious-Response. Ref Steps (1, 3-to-5, and setting up AEAD context in
// preparation for 6th step's Seal/Open) in spec.
// https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#section-4.2-4
absl::StatusOr<ObliviousHttpResponse::CommonOperationsResult>
ObliviousHttpResponse::CommonOperationsToEncapDecap(
    absl::string_view response_nonce,
    ObliviousHttpRequest::Context& oblivious_http_request_context,
    const size_t aead_key_len, const size_t aead_nonce_len,
    const size_t secret_len) {
  if (response_nonce.empty()) {
    return absl::InvalidArgumentError("Invalid input params.");
  }
  // secret = context.Export("message/bhttp response", Nk)
  // Export secret of len [max(Nn, Nk)] where Nk and Nn are the length of AEAD
  // key and nonce associated with context.
  // https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#section-4.2-2.1
  std::string secret(secret_len, '\0');
  absl::string_view resp_label =
      ObliviousHttpHeaderKeyConfig::kOhttpResponseLabel;
  if (!EVP_HPKE_CTX_export(oblivious_http_request_context.hpke_context_.get(),
                           reinterpret_cast<uint8_t*>(secret.data()),
                           secret.size(),
                           reinterpret_cast<const uint8_t*>(resp_label.data()),
                           resp_label.size())) {
    return SslErrorAsStatus("Failed to export secret.");
  }

  // salt = concat(enc, response_nonce)
  // https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#section-4.2-2.3
  std::string salt = absl::StrCat(
      oblivious_http_request_context.encapsulated_key_, response_nonce);

  // prk = Extract(salt, secret)
  // https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#section-4.2-2.3
  std::string pseudorandom_key(EVP_MAX_MD_SIZE, '\0');
  size_t prk_len;
  auto evp_md = EVP_HPKE_KDF_hkdf_md(
      EVP_HPKE_CTX_kdf(oblivious_http_request_context.hpke_context_.get()));
  if (evp_md == nullptr) {
    QUICHE_BUG(Invalid Key Configuration
               : Unsupported BoringSSL HPKE KDFs)
        << "Update KeyConfig to support only BoringSSL HKDFs.";
    return absl::FailedPreconditionError(
        "Key Configuration not supported by BoringSSL HPKE KDFs. Check your "
        "Key "
        "Config.");
  }
  if (!HKDF_extract(
          reinterpret_cast<uint8_t*>(pseudorandom_key.data()), &prk_len, evp_md,
          reinterpret_cast<const uint8_t*>(secret.data()), secret_len,
          reinterpret_cast<const uint8_t*>(salt.data()), salt.size())) {
    return SslErrorAsStatus(
        "Failed to derive pesudorandom key from salt and secret.");
  }
  pseudorandom_key.resize(prk_len);

  // aead_key = Expand(prk, "key", Nk)
  // https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#section-4.2-2.4
  std::string aead_key(aead_key_len, '\0');
  absl::string_view hkdf_info = ObliviousHttpHeaderKeyConfig::kKeyHkdfInfo;
  // All currently supported KDFs are HKDF-based. See CheckKdfId in
  // `ObliviousHttpHeaderKeyConfig`.
  if (!HKDF_expand(reinterpret_cast<uint8_t*>(aead_key.data()), aead_key_len,
                   evp_md,
                   reinterpret_cast<const uint8_t*>(pseudorandom_key.data()),
                   prk_len, reinterpret_cast<const uint8_t*>(hkdf_info.data()),
                   hkdf_info.size())) {
    return SslErrorAsStatus(
        "Failed to expand AEAD key using pseudorandom key(prk).");
  }

  // aead_nonce = Expand(prk, "nonce", Nn)
  // https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#section-4.2-2.5
  std::string aead_nonce(aead_nonce_len, '\0');
  hkdf_info = ObliviousHttpHeaderKeyConfig::kNonceHkdfInfo;
  // All currently supported KDFs are HKDF-based. See CheckKdfId in
  // `ObliviousHttpHeaderKeyConfig`.
  if (!HKDF_expand(reinterpret_cast<uint8_t*>(aead_nonce.data()),
                   aead_nonce_len, evp_md,
                   reinterpret_cast<const uint8_t*>(pseudorandom_key.data()),
                   prk_len, reinterpret_cast<const uint8_t*>(hkdf_info.data()),
                   hkdf_info.size())) {
    return SslErrorAsStatus(
        "Failed to expand AEAD nonce using pseudorandom key(prk).");
  }

  const EVP_AEAD* evp_hpke_aead = EVP_HPKE_AEAD_aead(
      EVP_HPKE_CTX_aead(oblivious_http_request_context.hpke_context_.get()));
  if (evp_hpke_aead == nullptr) {
    return absl::FailedPreconditionError(
        "Key Configuration not supported by HPKE AEADs. Check your key "
        "config.");
  }

  // Setup AEAD context for subsequent Seal/Open operation in response handling.
  bssl::UniquePtr<EVP_AEAD_CTX> aead_ctx(EVP_AEAD_CTX_new(
      evp_hpke_aead, reinterpret_cast<const uint8_t*>(aead_key.data()),
      aead_key.size(), 0));
  if (aead_ctx == nullptr) {
    return SslErrorAsStatus("Failed to initialize AEAD context.");
  }
  if (!EVP_AEAD_CTX_init(aead_ctx.get(), evp_hpke_aead,
                         reinterpret_cast<const uint8_t*>(aead_key.data()),
                         aead_key.size(), 0, nullptr)) {
    return SslErrorAsStatus(
        "Failed to initialize AEAD context with derived key.");
  }
  CommonOperationsResult result{std::move(aead_ctx), std::move(aead_nonce)};
  return result;
}

}  // namespace quiche

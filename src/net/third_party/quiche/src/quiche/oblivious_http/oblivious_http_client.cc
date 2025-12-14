#include "quiche/oblivious_http/oblivious_http_client.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/common/quiche_crypto_logging.h"

namespace quiche {

namespace {

// Use BoringSSL's setup_sender API to validate whether the HPKE public key
// input provided by the user is valid.
absl::Status ValidateClientParameters(
    absl::string_view hpke_public_key,
    const ObliviousHttpHeaderKeyConfig& ohttp_key_config) {
  // Initialize HPKE client context and check if context can be setup with the
  // given public key to verify if the public key is indeed valid.
  bssl::UniquePtr<EVP_HPKE_CTX> client_ctx(EVP_HPKE_CTX_new());
  if (client_ctx == nullptr) {
    return SslErrorAsStatus(
        "Failed to initialize HPKE ObliviousHttpClient Context.");
  }
  // Setup the sender (client)
  std::string encapsulated_key(EVP_HPKE_MAX_ENC_LENGTH, '\0');
  size_t enc_len;
  absl::string_view info = "verify if given HPKE public key is valid";
  if (!EVP_HPKE_CTX_setup_sender(
          client_ctx.get(), reinterpret_cast<uint8_t*>(encapsulated_key.data()),
          &enc_len, encapsulated_key.size(), ohttp_key_config.GetHpkeKem(),
          ohttp_key_config.GetHpkeKdf(), ohttp_key_config.GetHpkeAead(),
          reinterpret_cast<const uint8_t*>(hpke_public_key.data()),
          hpke_public_key.size(), reinterpret_cast<const uint8_t*>(info.data()),
          info.size())) {
    return SslErrorAsStatus(
        "Failed to setup HPKE context with given public key param "
        "hpke_public_key.");
  }
  return absl::OkStatus();
}

}  // namespace

// Constructor.
ObliviousHttpClient::ObliviousHttpClient(
    std::string client_public_key,
    const ObliviousHttpHeaderKeyConfig& ohttp_key_config)
    : hpke_public_key_(std::move(client_public_key)),
      ohttp_key_config_(ohttp_key_config) {}

// Initialize Bssl.
absl::StatusOr<ObliviousHttpClient> ObliviousHttpClient::Create(
    absl::string_view hpke_public_key,
    const ObliviousHttpHeaderKeyConfig& ohttp_key_config) {
  if (hpke_public_key.empty()) {
    return absl::InvalidArgumentError("Invalid/Empty HPKE public key.");
  }
  auto is_valid_input =
      ValidateClientParameters(hpke_public_key, ohttp_key_config);
  if (!is_valid_input.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid input received in method parameters. ",
                     is_valid_input.message()));
  }
  return ObliviousHttpClient(std::string(hpke_public_key), ohttp_key_config);
}

absl::StatusOr<ObliviousHttpRequest>
ObliviousHttpClient::CreateObliviousHttpRequest(
    std::string plaintext_data) const {
  return ObliviousHttpRequest::CreateClientObliviousRequest(
      std::move(plaintext_data), hpke_public_key_, ohttp_key_config_);
}

absl::StatusOr<ObliviousHttpResponse>
ObliviousHttpClient::DecryptObliviousHttpResponse(
    std::string encrypted_data,
    ObliviousHttpRequest::Context& oblivious_http_request_context) const {
  return ObliviousHttpResponse::CreateClientObliviousResponse(
      std::move(encrypted_data), oblivious_http_request_context);
}

}  // namespace quiche

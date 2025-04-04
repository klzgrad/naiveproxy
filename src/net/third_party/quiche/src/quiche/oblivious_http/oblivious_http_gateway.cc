#include "quiche/oblivious_http/oblivious_http_gateway.h"

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/common/quiche_crypto_logging.h"
#include "quiche/common/quiche_random.h"

namespace quiche {

// Constructor.
ObliviousHttpGateway::ObliviousHttpGateway(
    bssl::UniquePtr<EVP_HPKE_KEY> recipient_key,
    const ObliviousHttpHeaderKeyConfig& ohttp_key_config,
    QuicheRandom* quiche_random)
    : server_hpke_key_(std::move(recipient_key)),
      ohttp_key_config_(ohttp_key_config),
      quiche_random_(quiche_random) {}

// Initialize ObliviousHttpGateway(Recipient/Server) context.
absl::StatusOr<ObliviousHttpGateway> ObliviousHttpGateway::Create(
    absl::string_view hpke_private_key,
    const ObliviousHttpHeaderKeyConfig& ohttp_key_config,
    QuicheRandom* quiche_random) {
  if (hpke_private_key.empty()) {
    return absl::InvalidArgumentError("Invalid/Empty HPKE private key.");
  }
  // Initialize HPKE key and context.
  bssl::UniquePtr<EVP_HPKE_KEY> recipient_key(EVP_HPKE_KEY_new());
  if (recipient_key == nullptr) {
    return SslErrorAsStatus(
        "Failed to initialize ObliviousHttpGateway/Server's Key.");
  }
  if (!EVP_HPKE_KEY_init(
          recipient_key.get(), ohttp_key_config.GetHpkeKem(),
          reinterpret_cast<const uint8_t*>(hpke_private_key.data()),
          hpke_private_key.size())) {
    return SslErrorAsStatus("Failed to import HPKE private key.");
  }
  if (quiche_random == nullptr) quiche_random = QuicheRandom::GetInstance();
  return ObliviousHttpGateway(std::move(recipient_key), ohttp_key_config,
                              quiche_random);
}

absl::StatusOr<ObliviousHttpRequest>
ObliviousHttpGateway::DecryptObliviousHttpRequest(
    absl::string_view encrypted_data, absl::string_view request_label) const {
  return ObliviousHttpRequest::CreateServerObliviousRequest(
      encrypted_data, *(server_hpke_key_), ohttp_key_config_, request_label);
}

absl::StatusOr<ObliviousHttpResponse>
ObliviousHttpGateway::CreateObliviousHttpResponse(
    std::string plaintext_data,
    ObliviousHttpRequest::Context& oblivious_http_request_context,
    absl::string_view response_label) const {
  return ObliviousHttpResponse::CreateServerObliviousResponse(
      std::move(plaintext_data), oblivious_http_request_context, response_label,
      quiche_random_);
}

}  // namespace quiche

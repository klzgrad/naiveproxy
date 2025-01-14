#ifndef QUICHE_OBLIVIOUS_HTTP_OBLIVIOUS_HTTP_GATEWAY_H_
#define QUICHE_OBLIVIOUS_HTTP_OBLIVIOUS_HTTP_GATEWAY_H_

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "openssl/hpke.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_random.h"
#include "quiche/oblivious_http/buffers/oblivious_http_request.h"
#include "quiche/oblivious_http/buffers/oblivious_http_response.h"
#include "quiche/oblivious_http/common/oblivious_http_header_key_config.h"

namespace quiche {
// 1. Handles server side decryption of the payload received in HTTP POST body
// from Relay.
// 2. Handles server side encryption of response (that's in the form of Binary
// HTTP) that will be sent back to Relay in HTTP POST body.
// 3. Handles BSSL initialization and HPKE context bookkeeping.

// This class is immutable (except moves) and thus trivially thread-safe,
// assuming the `QuicheRandom* quiche_random` passed in with `Create` is
// thread-safe. Note that default `QuicheRandom::GetInstance()` is thread-safe.
class QUICHE_EXPORT ObliviousHttpGateway {
 public:
  // @params: If callers would like to pass in their own `QuicheRandom`
  // instance, they can make use of the param `quiche_random`. Otherwise, the
  // default `QuicheRandom::GetInstance()` will be used.
  static absl::StatusOr<ObliviousHttpGateway> Create(
      absl::string_view hpke_private_key,
      const ObliviousHttpHeaderKeyConfig& ohttp_key_config,
      QuicheRandom* quiche_random = nullptr);

  // only Movable (due to `UniquePtr server_hpke_key_`).
  ObliviousHttpGateway(ObliviousHttpGateway&& other) = default;
  ObliviousHttpGateway& operator=(ObliviousHttpGateway&& other) = default;

  ~ObliviousHttpGateway() = default;

  // After successful `Create`, callers will use the returned object to
  // repeatedly call into this method in order to create Oblivious HTTP request
  // with the initialized HPKE private key. Call sequence: Create ->
  // DecryptObliviousHttpRequest -> CreateObliviousHttpResponse.
  // Eg.,
  //   auto ohttp_server_object = ObliviousHttpGateway::Create( <HPKE
  //    private key>, <OHTTP key configuration described in
  //    `oblivious_http_header_key_config.h`>);
  //   auto decrypted_request1 =
  //    ohttp_server_object.DecryptObliviousHttpRequest(<encrypted binary http
  //    1>);
  //   auto decrypted_request2 =
  //    ohttp_server_object.DecryptObliviousHttpRequest(<encrypted binary http
  //    2>);
  absl::StatusOr<ObliviousHttpRequest> DecryptObliviousHttpRequest(
      absl::string_view encrypted_data,
      absl::string_view request_label =
          ObliviousHttpHeaderKeyConfig::kOhttpRequestLabel) const;

  // After `DecryptObliviousHttpRequest` operation, callers on server-side will
  // extract `oblivious_http_request_context` from the returned object
  // `ObliviousHttpRequest` and pass in to this method in order to handle the
  // response flow back to the client.
  absl::StatusOr<ObliviousHttpResponse> CreateObliviousHttpResponse(
      std::string plaintext_data,
      ObliviousHttpRequest::Context& oblivious_http_request_context,
      absl::string_view response_label =
          ObliviousHttpHeaderKeyConfig::kOhttpResponseLabel) const;

 private:
  explicit ObliviousHttpGateway(
      bssl::UniquePtr<EVP_HPKE_KEY> recipient_key,
      const ObliviousHttpHeaderKeyConfig& ohttp_key_config,
      QuicheRandom* quiche_random);
  bssl::UniquePtr<EVP_HPKE_KEY> server_hpke_key_;
  // Holds server's keyID and HPKE related IDs that's published under HPKE
  // public Key configuration.
  // https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#name-key-configuration
  ObliviousHttpHeaderKeyConfig ohttp_key_config_;
  QuicheRandom* quiche_random_;
};

}  // namespace quiche

#endif  // QUICHE_OBLIVIOUS_HTTP_OBLIVIOUS_HTTP_GATEWAY_H_

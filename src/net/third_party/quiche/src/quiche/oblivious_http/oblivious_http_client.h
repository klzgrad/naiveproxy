#ifndef QUICHE_OBLIVIOUS_HTTP_OBLIVIOUS_HTTP_CLIENT_H_
#define QUICHE_OBLIVIOUS_HTTP_OBLIVIOUS_HTTP_CLIENT_H_

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "openssl/hpke.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/oblivious_http/buffers/oblivious_http_request.h"
#include "quiche/oblivious_http/buffers/oblivious_http_response.h"
#include "quiche/oblivious_http/common/oblivious_http_header_key_config.h"

namespace quiche {
// 1. Facilitates client side to intiate OHttp request flow by initializing the
// HPKE public key obtained from server, and subsequently uses it to encrypt the
// Binary HTTP request payload.
// 2. After initializing this class with server's HPKE public key, users can
// call `CreateObliviousHttpRequest` which constructs OHTTP request of the input
// payload(Binary HTTP request).
// 3. Handles decryption of response (that's in the form of encrypted Binary
// HTTP response) that will be sent back from Server-to-Relay and
// Relay-to-client in HTTP POST body.
// 4. Handles BoringSSL HPKE context setup and bookkeeping.

// This class is immutable (except moves) and thus trivially thread-safe.
class QUICHE_EXPORT ObliviousHttpClient {
 public:
  static absl::StatusOr<ObliviousHttpClient> Create(
      absl::string_view hpke_public_key,
      const ObliviousHttpHeaderKeyConfig& ohttp_key_config);

  // Copyable.
  ObliviousHttpClient(const ObliviousHttpClient& other) = default;
  ObliviousHttpClient& operator=(const ObliviousHttpClient& other) = default;

  // Movable.
  ObliviousHttpClient(ObliviousHttpClient&& other) = default;
  ObliviousHttpClient& operator=(ObliviousHttpClient&& other) = default;

  ~ObliviousHttpClient() = default;

  // After successful `Create`, callers will use the returned object to
  // repeatedly call into this method in order to create Oblivious HTTP request
  // with the initialized HPKE public key. Call sequence: Create ->
  // CreateObliviousHttpRequest -> DecryptObliviousHttpResponse.
  // Eg.,
  //   auto ohttp_client_object = ObliviousHttpClient::Create( <HPKE
  //    public key>, <OHTTP key configuration described in
  //    `oblivious_http_header_key_config.h`>);
  //   auto encrypted_request1 =
  //    ohttp_client_object.CreateObliviousHttpRequest("binary http string 1");
  //   auto encrypted_request2 =
  //    ohttp_client_object.CreateObliviousHttpRequest("binary http string 2");
  absl::StatusOr<ObliviousHttpRequest> CreateObliviousHttpRequest(
      std::string plaintext_data) const;

  // After `CreateObliviousHttpRequest` operation, callers on client-side will
  // extract `oblivious_http_request_context` from the returned object
  // `ObliviousHttpRequest` and pass in to this method in order to decrypt the
  // response that's received from Gateway for the given request at hand.
  absl::StatusOr<ObliviousHttpResponse> DecryptObliviousHttpResponse(
      std::string encrypted_data,
      ObliviousHttpRequest::Context& oblivious_http_request_context) const;

 private:
  explicit ObliviousHttpClient(
      std::string client_public_key,
      const ObliviousHttpHeaderKeyConfig& ohttp_key_config);
  std::string hpke_public_key_;
  // Holds server's keyID and HPKE related IDs that's published under HPKE
  // public Key configuration.
  // https://www.ietf.org/archive/id/draft-ietf-ohai-ohttp-03.html#name-key-configuration
  ObliviousHttpHeaderKeyConfig ohttp_key_config_;
};

}  // namespace quiche

#endif  // QUICHE_OBLIVIOUS_HTTP_OBLIVIOUS_HTTP_CLIENT_H_

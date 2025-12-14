#ifndef QUICHE_OBLIVIOUS_HTTP_OBLIVIOUS_HTTP_GATEWAY_H_
#define QUICHE_OBLIVIOUS_HTTP_OBLIVIOUS_HTTP_GATEWAY_H_

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "openssl/hpke.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_random.h"
#include "quiche/oblivious_http/buffers/oblivious_http_request.h"
#include "quiche/oblivious_http/buffers/oblivious_http_response.h"
#include "quiche/oblivious_http/common/oblivious_http_chunk_handler.h"
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
  // https://www.rfc-editor.org/rfc/rfc9458.html#section-3
  ObliviousHttpHeaderKeyConfig ohttp_key_config_;
  QuicheRandom* quiche_random_;
};

// Manages a chunked Oblivious HTTP request and response.
// It's designed to process incoming request data in chunks, decrypting each one
// as it arrives and passing it to a handler function. It then continuously
// encrypts and sends back response chunks. This object maintains an internal
// state, so it can only be used for one complete request-response cycle.
class QUICHE_EXPORT ChunkedObliviousHttpGateway {
 public:
  // Creates a ChunkedObliviousHttpGateway. Like `ObliviousHttpGateway`,
  // `hpke_private_key` must outlive the gateway. `quiche_random` can be
  // initialized to nullptr, in which case the default
  // `QuicheRandom::GetInstance()` will be used.
  static absl::StatusOr<ChunkedObliviousHttpGateway> Create(
      absl::string_view hpke_private_key,
      const ObliviousHttpHeaderKeyConfig& ohttp_key_config,
      ObliviousHttpChunkHandler& chunk_handler,
      QuicheRandom* quiche_random = nullptr);

  // only Movable (due to `UniquePtr server_hpke_key_`).
  ChunkedObliviousHttpGateway(ChunkedObliviousHttpGateway&& other) = default;

  ~ChunkedObliviousHttpGateway() = default;

  // Parses the data into the corresponding chunks and decrypts them. This can
  // be invoked multiple times as data arrives, incomplete chunks will be
  // buffered. The first time it is called it will also decode the HPKE header.
  // On successful decryption, the chunk handler will be invoked. The
  // `end_stream` parameter must be set to true if the data contains the final
  // portion of the final chunk.
  absl::Status DecryptRequest(absl::string_view data, bool end_stream);

  // Encrypts the data as a single chunk. If `is_final_chunk` is true, the
  // response will be encoded with the final chunk indicator.
  absl::StatusOr<std::string> EncryptResponse(
      absl::string_view plaintext_payload, bool is_final_chunk);

 private:
  enum class RequestMessageSection {
    kHeader,
    kChunk,
    kFinalChunk,
    // Set by end_stream or if there is an error.
    kEnd,
  };
  enum class ResponseMessageSection {
    kNonce,
    kChunk,
    // Set after the final chunk is encrypted or if there is an error.
    kEnd,
  };

  explicit ChunkedObliviousHttpGateway(
      bssl::UniquePtr<EVP_HPKE_KEY> recipient_key,
      const ObliviousHttpHeaderKeyConfig& ohttp_key_config,
      ObliviousHttpChunkHandler& chunk_handler, QuicheRandom* quiche_random);

  // Initializes the checkpoint with the provided data and any buffered data.
  void InitializeRequestCheckpoint(absl::string_view data);
  // Carries out the decrypting logic from the checkpoint. Returns
  // OutOfRangeError if there is not enough data to process the current
  // section. When a section is fully processed, the checkpoint is updated.
  absl::Status DecryptRequestCheckpoint(bool end_stream);
  // Saves the checkpoint based on the current position of the reader.
  void SaveCheckpoint(const QuicheDataReader& reader) {
    request_checkpoint_view_ = reader.PeekRemainingPayload();
  }
  // Buffers the request checkpoint.
  void BufferRequestCheckpoint() {
    if (request_buffer_ != request_checkpoint_view_) {
      request_buffer_ = std::string(request_checkpoint_view_);
    }
  }
  absl::StatusOr<std::string> EncryptResponseChunk(
      absl::string_view plaintext_payload, bool is_final_chunk);

  bssl::UniquePtr<EVP_HPKE_KEY> server_hpke_key_;
  // Holds server's keyID and HPKE related IDs that's published under HPKE
  // public Key configuration.
  // https://www.rfc-editor.org/rfc/rfc9458.html#section-3
  ObliviousHttpHeaderKeyConfig ohttp_key_config_;
  // The handler to invoke when a chunk is decrypted successfully.
  ObliviousHttpChunkHandler& chunk_handler_;
  QuicheRandom* quiche_random_;

  std::string request_buffer_;
  // Tracks the remaining data to be processed or buffered.
  // When decoding fails due to missing data, we buffer based on this
  // checkpoint and return. When decoding succeeds, we update the checkpoint
  // to not buffer the already processed data.
  absl::string_view request_checkpoint_view_;
  RequestMessageSection request_current_section_ =
      RequestMessageSection::kHeader;
  ResponseMessageSection response_current_section_ =
      ResponseMessageSection::kNonce;

  // HPKE data derived from successfully decoding the chunked
  // request header when calling `DecryptRequest`.
  std::optional<ObliviousHttpRequest::Context> oblivious_http_request_context_;
  // The nonce for the response.
  std::string response_nonce_;
  // AEAD context data for the response. This is derived from the request HPKE
  // context data and response nonce.
  std::optional<ObliviousHttpResponse::AeadContextData> aead_context_data_;

  // Counter to keep track of the number of response chunks generated and to
  // generate the corresponding chunk nonce.
  std::optional<ObliviousHttpResponse::ChunkCounter> response_chunk_counter_;
};

}  // namespace quiche

#endif  // QUICHE_OBLIVIOUS_HTTP_OBLIVIOUS_HTTP_GATEWAY_H_

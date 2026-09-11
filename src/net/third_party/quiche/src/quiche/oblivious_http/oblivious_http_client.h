#ifndef QUICHE_OBLIVIOUS_HTTP_OBLIVIOUS_HTTP_CLIENT_H_
#define QUICHE_OBLIVIOUS_HTTP_OBLIVIOUS_HTTP_CLIENT_H_

#include <optional>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "openssl/hpke.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/oblivious_http/buffers/oblivious_http_request.h"
#include "quiche/oblivious_http/buffers/oblivious_http_response.h"
#include "quiche/oblivious_http/common/oblivious_http_chunk_handler.h"
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

  const std::string& GetPublicKey() const { return hpke_public_key_; }
  const ObliviousHttpHeaderKeyConfig& GetKeyConfig() const {
    return ohttp_key_config_;
  }

 private:
  explicit ObliviousHttpClient(
      std::string client_public_key,
      const ObliviousHttpHeaderKeyConfig& ohttp_key_config);
  std::string hpke_public_key_;
  // Holds server's keyID and HPKE related IDs that's published under HPKE
  // public Key configuration.
  // https://www.rfc-editor.org/rfc/rfc9458.html#section-3
  ObliviousHttpHeaderKeyConfig ohttp_key_config_;
};

// Manages a chunked Oblivious HTTP request and response.
// It's designed to continuously encrypt and send request chunks while
// decrypting and handling incoming response chunks. This object maintains an
// internal state, so it can only be used for one complete request-response
// cycle.
class QUICHE_EXPORT ChunkedObliviousHttpClient {
 public:
  // Movable but not copyable.
  ChunkedObliviousHttpClient(const ChunkedObliviousHttpClient& other) = delete;
  ChunkedObliviousHttpClient& operator=(
      const ChunkedObliviousHttpClient& other) = delete;
  ChunkedObliviousHttpClient(ChunkedObliviousHttpClient&& other) = default;
  // Move assignment disabled because chunk_handler_ is a reference. If we
  // ever need move assignment, we'll have to switch that to a pointer.
  ChunkedObliviousHttpClient& operator=(ChunkedObliviousHttpClient&& other) =
      delete;

  // Creates a new ChunkedObliviousHttpClient. Does not take ownership of
  // `chunk_handler`, which must refer to a valid handler that outlives this
  // client. The `seed` parameter is used to initialize the HPKE sender context.
  // If `seed` is empty, a random seed will be generated.
  static absl::StatusOr<ChunkedObliviousHttpClient> Create(
      absl::string_view hpke_public_key,
      const ObliviousHttpHeaderKeyConfig& ohttp_key_config,
      ObliviousHttpChunkHandler* absl_nonnull chunk_handler
          ABSL_ATTRIBUTE_LIFETIME_BOUND,
      absl::string_view seed = "");

  // Encrypts the data as a single chunk. The first time this is called it will
  // also include the HPKE context header data. If `is_final_chunk` is true,
  // the chunk will be encrypted with a final AAD.
  absl::StatusOr<std::string> EncryptRequestChunk(
      absl::string_view plaintext_payload, bool is_final_chunk);

  // Parses the `encrypted_data` into the corresponding chunks and decrypts
  // them. This can be invoked multiple times as data arrives, incomplete chunks
  // will be buffered. The first time it is called it will also decode the
  // response nonce. On successful decryption, the chunk handler will be
  // invoked. The `end_stream` parameter must be set to true if the
  // `encrypted_data`
  // contains the final portion of the final chunk.
  absl::Status DecryptResponse(absl::string_view encrypted_data,
                               bool end_stream);

 private:
  enum class RequestMessageSection {
    kHeader,
    kChunk,
    kEnd,  // Set by end_stream or if there is an error.
  };
  enum class ResponseMessageSection {
    kNonce,
    kChunk,
    kFinalChunk,
    kEnd,  // Set by end_stream or if there is an error.
  };

  // Does not take ownership of `chunk_handler`, which must refer to a valid
  // handler that outlives this client.
  explicit ChunkedObliviousHttpClient(
      const ObliviousHttpHeaderKeyConfig& ohttp_key_config,
      ObliviousHttpRequest::Context hpke_sender_context,
      ObliviousHttpResponse::CommonAeadParamsResult aead_params,
      ObliviousHttpChunkHandler* absl_nonnull chunk_handler
          ABSL_ATTRIBUTE_LIFETIME_BOUND);

  // Provides the response checkpoint from where to begin decryption. The
  // returned string_view is owned by either the provided data or the buffer.
  absl::string_view InitializeResponseCheckpoint(absl::string_view data);

  // Carries out the decrypting logic starting from the checkpoint. Returns
  // OutOfRangeError if there is not enough data to process the current
  // section. When a section is fully processed, the checkpoint is updated.
  absl::Status DecryptResponseCheckpoint(absl::string_view& response_checkpoint,
                                         bool end_stream);

  // Updates the checkpoint based on the current position of the reader.
  void UpdateCheckpoint(const QuicheDataReader& reader,
                        absl::string_view& response_checkpoint) {
    response_checkpoint = reader.PeekRemainingPayload();
  }

  // Buffers the response checkpoint.
  void BufferResponseCheckpoint(absl::string_view response_checkpoint) {
    if (response_buffer_ != response_checkpoint) {
      response_buffer_.assign(response_checkpoint);
    }
  }

  absl::StatusOr<std::string> EncryptRequestChunkImpl(
      absl::string_view plaintext_payload, bool is_final_chunk);

  // Holds server's keyID and HPKE related IDs that's published under HPKE
  // public Key configuration.
  // https://www.rfc-editor.org/rfc/rfc9458.html#section-3
  ObliviousHttpHeaderKeyConfig ohttp_key_config_;
  ObliviousHttpRequest::Context hpke_sender_context_;
  ObliviousHttpResponse::CommonAeadParamsResult aead_params_;

  // The handler to invoke when a chunk is decrypted successfully. Not owned.
  ObliviousHttpChunkHandler& chunk_handler_;

  RequestMessageSection request_current_section_ =
      RequestMessageSection::kHeader;
  ResponseMessageSection response_current_section_ =
      ResponseMessageSection::kNonce;

  // Holds the response data that could not be processed in the last call.
  std::string response_buffer_;

  // AEAD context data derived from the response nonce.
  std::optional<ObliviousHttpResponse::AeadContextData> aead_context_data_;

  // Counter to keep track of the number of response chunks decrypted and to
  // generate the corresponding chunk nonce.
  std::optional<ObliviousHttpResponse::ChunkCounter> response_chunk_counter_;
};

}  // namespace quiche

#endif  // QUICHE_OBLIVIOUS_HTTP_OBLIVIOUS_HTTP_CLIENT_H_

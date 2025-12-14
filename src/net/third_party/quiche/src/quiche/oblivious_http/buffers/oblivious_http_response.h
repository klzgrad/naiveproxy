#ifndef QUICHE_OBLIVIOUS_HTTP_BUFFERS_OBLIVIOUS_HTTP_RESPONSE_H_
#define QUICHE_OBLIVIOUS_HTTP_BUFFERS_OBLIVIOUS_HTTP_RESPONSE_H_

#include <stddef.h>

#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "quiche/common/quiche_random.h"
#include "quiche/oblivious_http/buffers/oblivious_http_request.h"
#include "quiche/oblivious_http/common/oblivious_http_header_key_config.h"

namespace quiche {

class QUICHE_EXPORT ObliviousHttpResponse {
 public:
  // A counter of the number of chunks sent/received in the response, used to
  // get the appropriate chunk nonce for encryption/decryption. See
  // (https://datatracker.ietf.org/doc/html/draft-ietf-ohai-chunked-ohttp-05#section-6.2).
  class QUICHE_EXPORT ChunkCounter {
   public:
    static absl::StatusOr<ChunkCounter> Create(std::string nonce);
    // Returns true if the counter has exceeded the maximum allowed value.
    bool LimitExceeded() const { return limit_exceeded_; }
    // Increments the chunk counter.
    void Increment();
    // XORs the nonce with the encoded counter to get the chunk nonce.
    std::string GetChunkNonce() const;

   private:
    explicit ChunkCounter(std::string nonce);
    // The nonce used to initialize the counter.
    const std::string nonce_;
    // Represents the counter value encoded to `Nn` bytes in network
    // byte order.
    std::string encoded_counter_;
    bool limit_exceeded_ = false;
  };

  // Common AEAD context data used for sealing/opening response chunks.
  struct QUICHE_EXPORT AeadContextData {
    bssl::UniquePtr<EVP_AEAD_CTX> aead_ctx;
    const std::string aead_nonce;
  };
  struct QUICHE_EXPORT CommonAeadParamsResult {
    const EVP_AEAD* evp_hpke_aead;
    const size_t aead_key_len;
    const size_t aead_nonce_len;
    const size_t secret_len;
  };

  // Parse and decrypt the OHttp response using ObliviousHttpContext context obj
  // that was returned from `CreateClientObliviousRequest` method. On success,
  // returns obj that callers will use to `GetDecryptedMessage`.
  // @params: Note that `oblivious_http_request_context` is required to stay
  // alive only for the lifetime of this factory method call.
  static absl::StatusOr<ObliviousHttpResponse> CreateClientObliviousResponse(
      std::string encrypted_data,
      ObliviousHttpRequest::Context& oblivious_http_request_context,
      absl::string_view resp_label =
          ObliviousHttpHeaderKeyConfig::kOhttpResponseLabel);

  // Encrypt the input param `plaintext_payload` and create OHttp response using
  // ObliviousHttpContext context obj that was returned from
  // `CreateServerObliviousRequest` method. On success, returns obj that callers
  // will use to `Serialize` OHttp response. Generic Usecase : server-side calls
  // this method in the context of Response.
  // @params: Note that `oblivious_http_request_context` is required to stay
  // alive only for the lifetime of this factory method call.
  // @params: If callers do not provide `quiche_random`, it will be initialized
  // to default supplied `QuicheRandom::GetInstance()`. It's recommended that
  // callers initialize `QuicheRandom* quiche_random` as a Singleton instance
  // within their code and pass in the same, in order to have optimized random
  // string generation. `quiche_random` is required to stay alive only for the
  // lifetime of this factory method call.
  static absl::StatusOr<ObliviousHttpResponse> CreateServerObliviousResponse(
      std::string plaintext_payload,
      ObliviousHttpRequest::Context& oblivious_http_request_context,
      absl::string_view resp_label =
          ObliviousHttpHeaderKeyConfig::kOhttpResponseLabel,
      QuicheRandom* quiche_random = nullptr);

  // Copyable.
  ObliviousHttpResponse(const ObliviousHttpResponse& other) = default;
  ObliviousHttpResponse& operator=(const ObliviousHttpResponse& other) =
      default;

  // Movable.
  ObliviousHttpResponse(ObliviousHttpResponse&& other) = default;
  ObliviousHttpResponse& operator=(ObliviousHttpResponse&& other) = default;

  ~ObliviousHttpResponse() = default;

  // Generates the AEAD context data from the response nonce.
  static absl::StatusOr<AeadContextData> GetAeadContextData(
      ObliviousHttpRequest::Context& oblivious_http_request_context,
      CommonAeadParamsResult& aead_params, absl::string_view response_label,
      absl::string_view response_nonce);

  static absl::StatusOr<std::string> EncryptChunk(
      ObliviousHttpRequest::Context& oblivious_http_request_context,
      const AeadContextData& aead_context_data,
      absl::string_view plaintext_payload, absl::string_view chunk_nonce,
      bool is_final_chunk);

  // Generic Usecase : server-side calls this method in the context of Response
  // to serialize OHTTP response that will be returned to client-side.
  // Returns serialized OHTTP response bytestring.
  const std::string& EncapsulateAndSerialize() const;

  const std::string& GetPlaintextData() const;
  std::string ConsumePlaintextData() && {
    return std::move(response_plaintext_);
  }

  // Determines AEAD key len(Nk), AEAD nonce len(Nn) based on HPKE context, and
  // further estimates secret_len = std::max(Nk, Nn)
  static absl::StatusOr<CommonAeadParamsResult> GetCommonAeadParams(
      ObliviousHttpRequest::Context& oblivious_http_request_context);

 private:
  struct CommonOperationsResult {
    bssl::UniquePtr<EVP_AEAD_CTX> aead_ctx;
    const std::string aead_nonce;
  };

  explicit ObliviousHttpResponse(std::string encrypted_data,
                                 std::string resp_plaintext);

  // Performs operations related to response handling that are common between
  // client and server.
  static absl::StatusOr<CommonOperationsResult> CommonOperationsToEncapDecap(
      absl::string_view response_nonce,
      ObliviousHttpRequest::Context& oblivious_http_request_context,
      absl::string_view resp_label, const size_t aead_key_len,
      const size_t aead_nonce_len, const size_t secret_len);
  std::string encrypted_data_;
  std::string response_plaintext_;
};

}  // namespace quiche

#endif  // QUICHE_OBLIVIOUS_HTTP_BUFFERS_OBLIVIOUS_HTTP_RESPONSE_H_

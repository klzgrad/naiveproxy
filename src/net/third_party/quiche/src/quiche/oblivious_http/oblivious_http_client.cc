#include "quiche/oblivious_http/oblivious_http_client.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "openssl/base.h"
#include "openssl/hpke.h"
#include "quiche/common/quiche_crypto_logging.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_data_writer.h"
#include "quiche/common/quiche_status_utils.h"
#include "quiche/oblivious_http/buffers/oblivious_http_request.h"
#include "quiche/oblivious_http/buffers/oblivious_http_response.h"
#include "quiche/oblivious_http/common/oblivious_http_chunk_handler.h"
#include "quiche/oblivious_http/common/oblivious_http_header_key_config.h"

namespace quiche {

namespace {
constexpr uint64_t kFinalChunkIndicator = 0;

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

ChunkedObliviousHttpClient::ChunkedObliviousHttpClient(
    const ObliviousHttpHeaderKeyConfig& ohttp_key_config,
    ObliviousHttpRequest::Context hpke_sender_context,
    ObliviousHttpResponse::CommonAeadParamsResult aead_params,
    ObliviousHttpChunkHandler* chunk_handler)
    : ohttp_key_config_(ohttp_key_config),
      hpke_sender_context_(std::move(hpke_sender_context)),
      aead_params_(aead_params),
      chunk_handler_(*chunk_handler) {}

absl::StatusOr<ChunkedObliviousHttpClient> ChunkedObliviousHttpClient::Create(
    absl::string_view hpke_public_key,
    const ObliviousHttpHeaderKeyConfig& ohttp_key_config,
    ObliviousHttpChunkHandler* chunk_handler, absl::string_view seed) {
  if (hpke_public_key.empty()) {
    return absl::InvalidArgumentError("Empty HPKE public key.");
  }
  if (chunk_handler == nullptr) {
    return absl::InvalidArgumentError("Null chunk handler.");
  }
  absl::Status is_valid_input =
      ValidateClientParameters(hpke_public_key, ohttp_key_config);
  if (!is_valid_input.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid input received in method parameters. ",
                     is_valid_input.message()));
  }
  QUICHE_ASSIGN_OR_RETURN(
      ObliviousHttpRequest::Context hpke_sender_context,
      ObliviousHttpRequest::CreateHpkeSenderContext(
          hpke_public_key, ohttp_key_config, seed,
          ObliviousHttpHeaderKeyConfig::kChunkedOhttpRequestLabel));
  QUICHE_ASSIGN_OR_RETURN(
      ObliviousHttpResponse::CommonAeadParamsResult aead_params,
      ObliviousHttpResponse::GetCommonAeadParams(hpke_sender_context));

  return ChunkedObliviousHttpClient(ohttp_key_config,
                                    std::move(hpke_sender_context),
                                    std::move(aead_params), chunk_handler);
}

absl::StatusOr<std::string> ChunkedObliviousHttpClient::EncryptRequestChunk(
    absl::string_view plaintext_payload, bool is_final_chunk) {
  if (request_current_section_ == RequestMessageSection::kEnd) {
    return absl::InvalidArgumentError("Cannot encrypt in bad state.");
  }
  absl::StatusOr<std::string> request_chunk =
      EncryptRequestChunkImpl(plaintext_payload, is_final_chunk);
  if (!request_chunk.ok()) {
    request_current_section_ = RequestMessageSection::kEnd;
  }
  return request_chunk;
}

absl::StatusOr<std::string> ChunkedObliviousHttpClient::EncryptRequestChunkImpl(
    absl::string_view plaintext_payload, bool is_final_chunk) {
  QUICHE_ASSIGN_OR_RETURN(
      std::string encrypted_data,
      ObliviousHttpRequest::EncryptChunk(plaintext_payload,
                                         hpke_sender_context_, is_final_chunk));

  std::string maybe_key_header_data = "";
  if (request_current_section_ == RequestMessageSection::kHeader) {
    maybe_key_header_data =
        absl::StrCat(ohttp_key_config_.SerializeOhttpPayloadHeader(),
                     hpke_sender_context_.GetEncapsulatedKey());
    request_current_section_ = RequestMessageSection::kChunk;
  }

  uint8_t chunk_var_int_length =
      QuicheDataWriter::GetVarInt62Len(encrypted_data.size());
  if (chunk_var_int_length == 0) {
    return absl::InvalidArgumentError(
        "Encrypted data is too large to be represented as a varint.");
  }
  uint64_t chunk_var_int = encrypted_data.size();
  if (is_final_chunk) {
    request_current_section_ = RequestMessageSection::kEnd;
    chunk_var_int_length =
        QuicheDataWriter::GetVarInt62Len(kFinalChunkIndicator);
    if (chunk_var_int_length == 0) {
      return absl::InvalidArgumentError(
          "Final chunk indicator is too large to be represented as a varint.");
    }
    chunk_var_int = kFinalChunkIndicator;
  }

  std::string request_buffer(maybe_key_header_data.size() +
                                 chunk_var_int_length + encrypted_data.size(),
                             '\0');

  QuicheDataWriter writer(request_buffer.size(), request_buffer.data());

  if (!writer.WriteStringPiece(maybe_key_header_data)) {
    return absl::InternalError(
        "Failed to write key header data to request buffer.");
  }
  if (!writer.WriteVarInt62(chunk_var_int)) {
    return absl::InternalError(
        "Failed to write encrypted chunk length to buffer.");
  }
  if (!writer.WriteStringPiece(encrypted_data)) {
    return absl::InternalError(
        "Failed to write encrypted chunk to request buffer.");
  }
  if (writer.remaining() != 0) {
    return absl::InternalError("Failed to write all data.");
  }

  return request_buffer;
}

absl::string_view ChunkedObliviousHttpClient::InitializeResponseCheckpoint(
    absl::string_view data) {
  absl::string_view response_checkpoint = data;
  // Prepend buffered data if present. This is the data from a previous call to
  // DecryptResponse that could not finish because it needed this new data.
  if (!response_buffer_.empty()) {
    absl::StrAppend(&response_buffer_, data);
    response_checkpoint = response_buffer_;
  }
  return response_checkpoint;
}

absl::Status ChunkedObliviousHttpClient::DecryptResponseCheckpoint(
    absl::string_view& response_checkpoint, bool end_stream) {
  QuicheDataReader reader(response_checkpoint);
  switch (response_current_section_) {
    case ResponseMessageSection::kEnd:
      return absl::InternalError("Response is invalid.");
    case ResponseMessageSection::kNonce: {
      absl::string_view response_nonce;
      if (!reader.ReadStringPiece(&response_nonce, aead_params_.secret_len)) {
        return absl::OutOfRangeError("Not enough data to read response nonce.");
      }

      QUICHE_ASSIGN_OR_RETURN(
          ObliviousHttpResponse::AeadContextData aead_context_data,
          ObliviousHttpResponse::GetAeadContextData(
              hpke_sender_context_, aead_params_,
              ObliviousHttpHeaderKeyConfig::kChunkedOhttpResponseLabel,
              response_nonce));
      aead_context_data_.emplace(std::move(aead_context_data));

      QUICHE_ASSIGN_OR_RETURN(
          ObliviousHttpResponse::ChunkCounter response_chunk_counter,
          ObliviousHttpResponse::ChunkCounter::Create(
              aead_context_data_->aead_nonce));

      response_chunk_counter_.emplace(std::move(response_chunk_counter));
      UpdateCheckpoint(reader, response_checkpoint);
      response_current_section_ = ResponseMessageSection::kChunk;
    }
      ABSL_FALLTHROUGH_INTENDED;
    case ResponseMessageSection::kChunk: {
      if (!aead_context_data_.has_value()) {
        return absl::InternalError(
            "HPKE context has not been derived from the response nonce.");
      }
      ObliviousHttpResponse::AeadContextData& aead_context_data =
          *aead_context_data_;
      if (!response_chunk_counter_.has_value()) {
        return absl::InternalError("Chunk counter has not been initialized.");
      }
      ObliviousHttpResponse::ChunkCounter& response_chunk_counter =
          *response_chunk_counter_;

      bool is_final_chunk = false;
      do {
        if (response_chunk_counter.LimitExceeded()) {
          return absl::InvalidArgumentError(
              "Maximum number of chunks allowed in the response has been "
              "exceeded.");
        }
        uint64_t length_or_final_chunk_indicator;
        if (!reader.ReadVarInt62(&length_or_final_chunk_indicator)) {
          return absl::OutOfRangeError("Not enough data to read chunk length.");
        }

        is_final_chunk =
            length_or_final_chunk_indicator == kFinalChunkIndicator;
        if (!is_final_chunk) {
          absl::string_view chunk;
          if (!reader.ReadStringPiece(&chunk,
                                      length_or_final_chunk_indicator)) {
            return absl::OutOfRangeError("Not enough data to read chunk.");
          }

          QUICHE_ASSIGN_OR_RETURN(
              std::string decrypted_chunk,
              ObliviousHttpResponse::DecryptChunk(
                  chunk, aead_context_data,
                  response_chunk_counter.GetChunkNonce(), is_final_chunk));

          response_chunk_counter.Increment();
          absl::Status handler_status =
              chunk_handler_.OnDecryptedChunk(decrypted_chunk);
          if (!handler_status.ok()) {
            return absl::InternalError(absl::StrCat(
                "Chunk handler failed to process decrypted chunk: ",
                handler_status.message()));
          }
        }

        UpdateCheckpoint(reader, response_checkpoint);
      } while (!is_final_chunk);

      response_current_section_ = ResponseMessageSection::kFinalChunk;
    }
      ABSL_FALLTHROUGH_INTENDED;
    case ResponseMessageSection::kFinalChunk: {
      if (!end_stream) {
        return absl::OutOfRangeError("Not enough data to read final chunk.");
      }
      if (!aead_context_data_.has_value()) {
        return absl::InternalError(
            "HPKE context has not been derived from the response nonce.");
      }
      ObliviousHttpResponse::AeadContextData& aead_context_data =
          *aead_context_data_;
      if (!response_chunk_counter_.has_value()) {
        return absl::InternalError("Chunk counter has not been initialized.");
      }
      ObliviousHttpResponse::ChunkCounter& response_chunk_counter =
          *response_chunk_counter_;
      QUICHE_ASSIGN_OR_RETURN(
          std::string decrypted_chunk,
          ObliviousHttpResponse::DecryptChunk(
              reader.PeekRemainingPayload(), aead_context_data,
              response_chunk_counter.GetChunkNonce(),
              /*is_final_chunk=*/true));

      absl::Status handler_status =
          chunk_handler_.OnDecryptedChunk(decrypted_chunk);
      if (!handler_status.ok()) {
        return absl::InternalError(
            absl::StrCat("Chunk handler failed to process decrypted chunk: ",
                         handler_status.message()));
      }
      handler_status = chunk_handler_.OnChunksDone();
      if (!handler_status.ok()) {
        return absl::InternalError(
            absl::StrCat("Chunk handler failed to process chunks done: ",
                         handler_status.message()));
      }
      return absl::OkStatus();
    }
  }
  // This should never happen because response_current_section_ is private
  // and we only ever set it to values handled by the switch statement above.
  return absl::InternalError("Unexpected ResponseMessageSection value.");
}

absl::Status ChunkedObliviousHttpClient::DecryptResponse(
    absl::string_view encrypted_data, bool end_stream) {
  if (response_current_section_ == ResponseMessageSection::kEnd) {
    return absl::InternalError("Response is invalid.");
  }
  absl::string_view response_checkpoint =
      InitializeResponseCheckpoint(encrypted_data);
  absl::Status status =
      DecryptResponseCheckpoint(response_checkpoint, end_stream);
  if (end_stream) {
    response_current_section_ = ResponseMessageSection::kEnd;
    response_buffer_.clear();
    return status;
  }
  if (absl::IsOutOfRange(status)) {
    BufferResponseCheckpoint(response_checkpoint);
    return absl::OkStatus();
  }
  if (!status.ok()) {
    response_current_section_ = ResponseMessageSection::kEnd;
  }

  response_buffer_.clear();
  return status;
}

}  // namespace quiche

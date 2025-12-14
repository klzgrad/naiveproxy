#include "quiche/oblivious_http/oblivious_http_gateway.h"

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "openssl/aead.h"
#include "openssl/base.h"
#include "openssl/hpke.h"
#include "quiche/common/quiche_crypto_logging.h"
#include "quiche/common/quiche_data_reader.h"
#include "quiche/common/quiche_data_writer.h"
#include "quiche/common/quiche_endian.h"
#include "quiche/common/quiche_random.h"
#include "quiche/oblivious_http/buffers/oblivious_http_request.h"
#include "quiche/oblivious_http/common/oblivious_http_chunk_handler.h"
#include "quiche/oblivious_http/common/oblivious_http_header_key_config.h"

namespace quiche {
namespace {
constexpr uint64_t kFinalChunkIndicator = 0;
}

// Constructor.
ObliviousHttpGateway::ObliviousHttpGateway(
    bssl::UniquePtr<EVP_HPKE_KEY> recipient_key,
    const ObliviousHttpHeaderKeyConfig& ohttp_key_config,
    QuicheRandom* quiche_random)
    : server_hpke_key_(std::move(recipient_key)),
      ohttp_key_config_(ohttp_key_config),
      quiche_random_(quiche_random) {}

absl::StatusOr<bssl::UniquePtr<EVP_HPKE_KEY>> CreateServerRecipientKey(
    absl::string_view hpke_private_key,
    const ObliviousHttpHeaderKeyConfig& ohttp_key_config) {
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
  return recipient_key;
}

// Initialize ObliviousHttpGateway(Recipient/Server) context.
absl::StatusOr<ObliviousHttpGateway> ObliviousHttpGateway::Create(
    absl::string_view hpke_private_key,
    const ObliviousHttpHeaderKeyConfig& ohttp_key_config,
    QuicheRandom* quiche_random) {
  absl::StatusOr<bssl::UniquePtr<EVP_HPKE_KEY>> recipient_key =
      CreateServerRecipientKey(hpke_private_key, ohttp_key_config);
  if (!recipient_key.ok()) {
    return recipient_key.status();
  }
  if (quiche_random == nullptr) quiche_random = QuicheRandom::GetInstance();
  return ObliviousHttpGateway(std::move(*recipient_key), ohttp_key_config,
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

// Constructor.
ChunkedObliviousHttpGateway::ChunkedObliviousHttpGateway(
    bssl::UniquePtr<EVP_HPKE_KEY> recipient_key,
    const ObliviousHttpHeaderKeyConfig& ohttp_key_config,
    ObliviousHttpChunkHandler& chunk_handler, QuicheRandom* quiche_random)
    : server_hpke_key_(std::move(recipient_key)),
      ohttp_key_config_(ohttp_key_config),
      chunk_handler_(chunk_handler),
      quiche_random_(quiche_random) {}

absl::StatusOr<ChunkedObliviousHttpGateway> ChunkedObliviousHttpGateway::Create(
    absl::string_view hpke_private_key,
    const ObliviousHttpHeaderKeyConfig& ohttp_key_config,
    ObliviousHttpChunkHandler& chunk_handler, QuicheRandom* quiche_random) {
  absl::StatusOr<bssl::UniquePtr<EVP_HPKE_KEY>> recipient_key =
      CreateServerRecipientKey(hpke_private_key, ohttp_key_config);
  if (!recipient_key.ok()) {
    return recipient_key.status();
  }
  if (quiche_random == nullptr) {
    quiche_random = QuicheRandom::GetInstance();
  }
  return ChunkedObliviousHttpGateway(std::move(*recipient_key),
                                     ohttp_key_config, chunk_handler,
                                     quiche_random);
}

void ChunkedObliviousHttpGateway::InitializeRequestCheckpoint(
    absl::string_view data) {
  request_checkpoint_view_ = data;
  // Prepend buffered data if present. This is the data from a previous call to
  // DecryptRequest that could not finish because it needed this new data.
  if (!request_buffer_.empty()) {
    if (!data.empty()) {
      absl::StrAppend(&request_buffer_, data);
    }
    request_checkpoint_view_ = request_buffer_;
  }
}

absl::Status ChunkedObliviousHttpGateway::DecryptRequestCheckpoint(
    bool end_stream) {
  QuicheDataReader reader(request_checkpoint_view_);
  switch (request_current_section_) {
    case RequestMessageSection::kEnd:
      return absl::InternalError("Request is invalid.");
    case RequestMessageSection::kHeader: {
      // Check there is enough data for the chunked request header.
      // https://www.ietf.org/archive/id/draft-ietf-ohai-chunked-ohttp-05.html#name-request-format
      if (reader.PeekRemainingPayload().size() <
          ObliviousHttpHeaderKeyConfig::kHeaderLength +
              EVP_HPKE_KEM_enc_len(EVP_HPKE_KEY_kem(server_hpke_key_.get()))) {
        return absl::OutOfRangeError("Not enough data to read header.");
      }
      absl::StatusOr<ObliviousHttpRequest::Context> context =
          ObliviousHttpRequest::DecodeEncapsulatedRequestHeader(
              reader, *server_hpke_key_, ohttp_key_config_,
              ObliviousHttpHeaderKeyConfig::kChunkedOhttpRequestLabel);
      if (!context.ok()) {
        return context.status();
      }

      oblivious_http_request_context_ = std::move(*context);
      SaveCheckpoint(reader);
      request_current_section_ = RequestMessageSection::kChunk;
    }
      ABSL_FALLTHROUGH_INTENDED;
    case RequestMessageSection::kChunk: {
      uint64_t length_or_final_chunk_indicator;
      do {
        if (!reader.ReadVarInt62(&length_or_final_chunk_indicator)) {
          return absl::OutOfRangeError("Not enough data to read chunk length.");
        }
        absl::string_view chunk;
        if (length_or_final_chunk_indicator != kFinalChunkIndicator) {
          if (!reader.ReadStringPiece(&chunk,
                                      length_or_final_chunk_indicator)) {
            return absl::OutOfRangeError("Not enough data to read chunk.");
          }
          if (!oblivious_http_request_context_.has_value()) {
            return absl::InternalError(
                "HPKE context has not been derived from an encrypted request.");
          }
          absl::StatusOr<std::string> decrypted_chunk =
              ObliviousHttpRequest::DecryptChunk(
                  *oblivious_http_request_context_, chunk,
                  /*is_final_chunk=*/false);
          if (!decrypted_chunk.ok()) {
            return decrypted_chunk.status();
          }
          absl::Status handle_chunk_status =
              chunk_handler_.OnDecryptedChunk(*decrypted_chunk);
          if (!handle_chunk_status.ok()) {
            return handle_chunk_status;
          }
        }

        SaveCheckpoint(reader);
      } while (length_or_final_chunk_indicator != kFinalChunkIndicator);

      request_current_section_ = RequestMessageSection::kFinalChunk;
    }
      ABSL_FALLTHROUGH_INTENDED;
    case RequestMessageSection::kFinalChunk: {
      if (!end_stream) {
        return absl::OutOfRangeError("Not enough data to read final chunk.");
      }
      if (!oblivious_http_request_context_.has_value()) {
        return absl::InternalError(
            "HPKE context has not been derived from an encrypted request.");
      }
      absl::StatusOr<std::string> decrypted_chunk =
          ObliviousHttpRequest::DecryptChunk(*oblivious_http_request_context_,
                                             reader.PeekRemainingPayload(),
                                             /*is_final_chunk=*/true);
      if (!decrypted_chunk.ok()) {
        return decrypted_chunk.status();
      }
      absl::Status handle_chunk_status =
          chunk_handler_.OnDecryptedChunk(*decrypted_chunk);
      if (!handle_chunk_status.ok()) {
        return handle_chunk_status;
      }
      handle_chunk_status = chunk_handler_.OnChunksDone();
      if (!handle_chunk_status.ok()) {
        return handle_chunk_status;
      }
    }
  }
  return absl::OkStatus();
}

absl::Status ChunkedObliviousHttpGateway::DecryptRequest(absl::string_view data,
                                                         bool end_stream) {
  if (request_current_section_ == RequestMessageSection::kEnd) {
    return absl::InternalError("Decrypting is marked as invalid.");
  }
  InitializeRequestCheckpoint(data);
  absl::Status status = DecryptRequestCheckpoint(end_stream);
  if (end_stream) {
    request_current_section_ = RequestMessageSection::kEnd;
    if (absl::IsOutOfRange(status)) {
      // OutOfRange only used internally for buffering, so return
      // InvalidArgument if this is the end of the stream.
      status = absl::InvalidArgumentError(status.message());
    }
    return status;
  }
  if (absl::IsOutOfRange(status)) {
    BufferRequestCheckpoint();
    return absl::OkStatus();
  }
  if (!status.ok()) {
    request_current_section_ = RequestMessageSection::kEnd;
  }

  request_buffer_.clear();
  return status;
}

absl::StatusOr<std::string> ChunkedObliviousHttpGateway::EncryptResponse(
    absl::string_view plaintext_payload, bool is_final_chunk) {
  if (response_current_section_ == ResponseMessageSection::kEnd) {
    return absl::InvalidArgumentError("Encrypting is marked as invalid.");
  }
  absl::StatusOr<std::string> response_chunk =
      EncryptResponseChunk(plaintext_payload, is_final_chunk);
  if (!response_chunk.ok()) {
    response_current_section_ = ResponseMessageSection::kEnd;
  }
  return response_chunk;
}

absl::StatusOr<std::string> ChunkedObliviousHttpGateway::EncryptResponseChunk(
    absl::string_view plaintext_payload, bool is_final_chunk) {
  if (response_chunk_counter_.has_value() &&
      response_chunk_counter_->LimitExceeded()) {
    return absl::InternalError(
        "Response chunk counter has exceeded the maximum allowed value.");
  }
  if (!oblivious_http_request_context_.has_value()) {
    return absl::InternalError(
        "HPKE context has not been derived from an encrypted request.");
  }

  if (!aead_context_data_.has_value()) {
    absl::StatusOr<ObliviousHttpResponse::CommonAeadParamsResult> aead_params =
        ObliviousHttpResponse::GetCommonAeadParams(
            *oblivious_http_request_context_);
    if (!aead_params.ok()) {
      return aead_params.status();
    }

    // secret_len represents max(Nn, Nk))
    response_nonce_ = std::string(aead_params->secret_len, '\0');
    quiche_random_->RandBytes(response_nonce_.data(), response_nonce_.size());

    auto aead_context_data = ObliviousHttpResponse::GetAeadContextData(
        *oblivious_http_request_context_, *aead_params,
        ObliviousHttpHeaderKeyConfig::kChunkedOhttpResponseLabel,
        response_nonce_);
    if (!aead_context_data.ok()) {
      return aead_context_data.status();
    }
    aead_context_data_.emplace(std::move(*aead_context_data));

    auto response_chunk_counter = ObliviousHttpResponse::ChunkCounter::Create(
        aead_context_data_->aead_nonce);
    if (!response_chunk_counter.ok()) {
      return response_chunk_counter.status();
    }
    response_chunk_counter_.emplace(std::move(*response_chunk_counter));
  }

  if (!response_chunk_counter_.has_value()) {
    return absl::InternalError(
        "Response chunk counter has not been initialized.");
  }

  absl::StatusOr<std::string> encrypted_data =
      ObliviousHttpResponse::EncryptChunk(
          *oblivious_http_request_context_, *aead_context_data_,
          plaintext_payload, response_chunk_counter_->GetChunkNonce(),
          is_final_chunk);
  if (!encrypted_data.ok()) {
    return encrypted_data.status();
  }

  absl::string_view maybe_nonce;
  if (response_current_section_ == ResponseMessageSection::kNonce) {
    maybe_nonce = response_nonce_;
    response_current_section_ = ResponseMessageSection::kChunk;
  }

  uint8_t chunk_var_int_length =
      QuicheDataWriter::GetVarInt62Len(encrypted_data->size());
  uint64_t chunk_var_int = encrypted_data->size();
  if (is_final_chunk) {
    response_current_section_ = ResponseMessageSection::kEnd;
    chunk_var_int_length =
        QuicheDataWriter::GetVarInt62Len(kFinalChunkIndicator);
    // encrypted_data is guaranteed to be non-empty, so chunk_var_int_length
    // should never be 0.
    if (chunk_var_int_length == 0) {
      return absl::InvalidArgumentError(
          "Encrypted data is too large to be represented as a varint.");
    }
    chunk_var_int = kFinalChunkIndicator;
  }

  std::string response_buffer(
      maybe_nonce.size() + chunk_var_int_length + encrypted_data->size(), '\0');
  QuicheDataWriter writer(response_buffer.size(), response_buffer.data());

  if (!writer.WriteStringPiece(maybe_nonce)) {
    return absl::InternalError("Failed to write response nonce to buffer.");
  }
  if (!writer.WriteVarInt62(chunk_var_int)) {
    return absl::InternalError("Failed to write chunk to buffer.");
  }
  if (!writer.WriteStringPiece(*encrypted_data)) {
    return absl::InternalError("Failed to write encrypted data to buffer.");
  }

  if (writer.remaining() != 0) {
    return absl::InternalError("Failed to write all data.");
  }

  response_chunk_counter_->Increment();
  return response_buffer;
}

}  // namespace quiche

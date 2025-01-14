// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/tls_chlo_extractor.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/frames/quic_crypto_frame.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_framer.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

namespace {
bool HasExtension(const SSL_CLIENT_HELLO* client_hello, uint16_t extension) {
  const uint8_t* unused_extension_bytes;
  size_t unused_extension_len;
  return 1 == SSL_early_callback_ctx_extension_get(client_hello, extension,
                                                   &unused_extension_bytes,
                                                   &unused_extension_len);
}

std::vector<uint16_t> GetSupportedGroups(const SSL_CLIENT_HELLO* client_hello) {
  const uint8_t* extension_data;
  size_t extension_len;
  int rv = SSL_early_callback_ctx_extension_get(
      client_hello, TLSEXT_TYPE_supported_groups, &extension_data,
      &extension_len);
  if (rv != 1) {
    return {};
  }

  // See https://datatracker.ietf.org/doc/html/rfc8446#section-4.2.7 for the
  // format of this extension.
  QuicDataReader named_groups_reader(
      reinterpret_cast<const char*>(extension_data), extension_len);
  uint16_t named_groups_len;
  if (!named_groups_reader.ReadUInt16(&named_groups_len) ||
      named_groups_len + sizeof(uint16_t) != extension_len) {
    QUIC_CODE_COUNT(quic_chlo_supported_groups_invalid_length);
    return {};
  }

  std::vector<uint16_t> named_groups;
  while (!named_groups_reader.IsDoneReading()) {
    uint16_t named_group;
    if (!named_groups_reader.ReadUInt16(&named_group)) {
      QUIC_CODE_COUNT(quic_chlo_supported_groups_odd_length);
      QUIC_LOG_FIRST_N(WARNING, 10) << "Failed to read named groups";
      break;
    }
    named_groups.push_back(named_group);
  }
  return named_groups;
}

std::vector<uint16_t> GetCertCompressionAlgos(
    const SSL_CLIENT_HELLO* client_hello) {
  const uint8_t* extension_data;
  size_t extension_len;
  int rv = SSL_early_callback_ctx_extension_get(
      client_hello, TLSEXT_TYPE_cert_compression, &extension_data,
      &extension_len);
  if (rv != 1) {
    return {};
  }
  // See https://datatracker.ietf.org/doc/html/rfc8879#section-3 for the format
  // of this extension.
  QuicDataReader cert_compression_algos_reader(
      reinterpret_cast<const char*>(extension_data), extension_len);
  uint8_t algos_len;
  if (!cert_compression_algos_reader.ReadUInt8(&algos_len) || algos_len == 0 ||
      algos_len % sizeof(uint16_t) != 0 ||
      algos_len + sizeof(uint8_t) != extension_len) {
    QUIC_CODE_COUNT(quic_chlo_cert_compression_algos_invalid_length);
    return {};
  }

  size_t num_algos = algos_len / sizeof(uint16_t);
  std::vector<uint16_t> cert_compression_algos;
  cert_compression_algos.reserve(num_algos);
  for (size_t i = 0; i < num_algos; ++i) {
    uint16_t cert_compression_algo;
    if (!cert_compression_algos_reader.ReadUInt16(&cert_compression_algo)) {
      QUIC_CODE_COUNT(quic_chlo_fail_to_read_cert_compression_algo);
      return {};
    }
    cert_compression_algos.push_back(cert_compression_algo);
  }
  return cert_compression_algos;
}

}  // namespace

TlsChloExtractor::TlsChloExtractor()
    : crypto_stream_sequencer_(this),
      state_(State::kInitial),
      parsed_crypto_frame_in_this_packet_(false) {}

TlsChloExtractor::TlsChloExtractor(TlsChloExtractor&& other)
    : TlsChloExtractor() {
  *this = std::move(other);
}

TlsChloExtractor& TlsChloExtractor::operator=(TlsChloExtractor&& other) {
  framer_ = std::move(other.framer_);
  if (framer_) {
    framer_->set_visitor(this);
  }
  crypto_stream_sequencer_ = std::move(other.crypto_stream_sequencer_);
  crypto_stream_sequencer_.set_stream(this);
  ssl_ = std::move(other.ssl_);
  if (ssl_) {
    std::pair<SSL_CTX*, int> shared_handles = GetSharedSslHandles();
    int ex_data_index = shared_handles.second;
    const int rv = SSL_set_ex_data(ssl_.get(), ex_data_index, this);
    QUICHE_CHECK_EQ(rv, 1) << "Internal allocation failure in SSL_set_ex_data";
  }
  state_ = other.state_;
  error_details_ = std::move(other.error_details_);
  parsed_crypto_frame_in_this_packet_ =
      other.parsed_crypto_frame_in_this_packet_;
  supported_groups_ = std::move(other.supported_groups_);
  cert_compression_algos_ = std::move(other.cert_compression_algos_);
  alpns_ = std::move(other.alpns_);
  server_name_ = std::move(other.server_name_);
  client_hello_bytes_ = std::move(other.client_hello_bytes_);
  return *this;
}

void TlsChloExtractor::IngestPacket(const ParsedQuicVersion& version,
                                    const QuicReceivedPacket& packet) {
  if (state_ == State::kUnrecoverableFailure) {
    QUIC_DLOG(ERROR) << "Not ingesting packet after unrecoverable error";
    return;
  }
  if (version == UnsupportedQuicVersion()) {
    QUIC_DLOG(ERROR) << "Not ingesting packet with unsupported version";
    return;
  }
  if (version.handshake_protocol != PROTOCOL_TLS1_3) {
    QUIC_DLOG(ERROR) << "Not ingesting packet with non-TLS version " << version;
    return;
  }
  if (framer_) {
    // This is not the first packet we have ingested, check if version matches.
    if (!framer_->IsSupportedVersion(version)) {
      QUIC_DLOG(ERROR)
          << "Not ingesting packet with version mismatch, expected "
          << framer_->version() << ", got " << version;
      return;
    }
  } else {
    // This is the first packet we have ingested, setup parser.
    framer_ = std::make_unique<QuicFramer>(
        ParsedQuicVersionVector{version}, QuicTime::Zero(),
        Perspective::IS_SERVER, /*expected_server_connection_id_length=*/0);
    // Note that expected_server_connection_id_length only matters for short
    // headers and we explicitly drop those so we can pass any value here.
    framer_->set_visitor(this);
  }

  // When the framer parses |packet|, if it sees a CRYPTO frame it will call
  // OnCryptoFrame below and that will set parsed_crypto_frame_in_this_packet_
  // to true.
  parsed_crypto_frame_in_this_packet_ = false;
  const bool parse_success = framer_->ProcessPacket(packet);
  if (state_ == State::kInitial && parsed_crypto_frame_in_this_packet_) {
    // If we parsed a CRYPTO frame but didn't advance the state from initial,
    // then it means that we will need more packets to reassemble the full CHLO,
    // so we advance the state here. This can happen when the first packet
    // received is not the first one in the crypto stream. This allows us to
    // differentiate our state between single-packet CHLO and multi-packet CHLO.
    state_ = State::kParsedPartialChloFragment;
  }

  if (!parse_success) {
    // This could be due to the packet being non-initial for example.
    QUIC_DLOG(ERROR) << "Failed to process packet";
    return;
  }
}

// This is called when the framer parsed the unencrypted parts of the header.
bool TlsChloExtractor::OnUnauthenticatedPublicHeader(
    const QuicPacketHeader& header) {
  if (header.form != IETF_QUIC_LONG_HEADER_PACKET) {
    QUIC_DLOG(ERROR) << "Not parsing non-long-header packet " << header;
    return false;
  }
  if (header.long_packet_type != INITIAL) {
    QUIC_DLOG(ERROR) << "Not parsing non-initial packet " << header;
    return false;
  }
  // QuicFramer is constructed without knowledge of the server's connection ID
  // so it needs to be set up here in order to decrypt the packet.
  //
  // Only call SetInitialObfuscators once for the first ingested packet, whose
  // |header.destination_connection_id| is the original connection ID.
  if (framer_->GetDecrypter(ENCRYPTION_INITIAL) == nullptr) {
    framer_->SetInitialObfuscators(header.destination_connection_id);
  }

  return true;
}

// This is called by the framer if it detects a change in version during
// parsing.
bool TlsChloExtractor::OnProtocolVersionMismatch(ParsedQuicVersion version) {
  // This should never be called because we already check versions in
  // IngestPacket.
  QUIC_BUG(quic_bug_10855_1) << "Unexpected version mismatch, expected "
                             << framer_->version() << ", got " << version;
  return false;
}

// This is called by the QuicStreamSequencer if it encounters an unrecoverable
// error that will prevent it from reassembling the crypto stream data.
void TlsChloExtractor::OnUnrecoverableError(QuicErrorCode error,
                                            const std::string& details) {
  HandleUnrecoverableError(absl::StrCat(
      "Crypto stream error ", QuicErrorCodeToString(error), ": ", details));
}

void TlsChloExtractor::OnUnrecoverableError(
    QuicErrorCode error, QuicIetfTransportErrorCodes ietf_error,
    const std::string& details) {
  HandleUnrecoverableError(absl::StrCat(
      "Crypto stream error ", QuicErrorCodeToString(error), "(",
      QuicIetfTransportErrorCodeString(ietf_error), "): ", details));
}

// This is called by the framer if it sees a CRYPTO frame during parsing.
bool TlsChloExtractor::OnCryptoFrame(const QuicCryptoFrame& frame) {
  if (frame.level != ENCRYPTION_INITIAL) {
    // Since we drop non-INITIAL packets in OnUnauthenticatedPublicHeader,
    // we should never receive any CRYPTO frames at other encryption levels.
    QUIC_BUG(quic_bug_10855_2) << "Parsed bad-level CRYPTO frame " << frame;
    return false;
  }
  // parsed_crypto_frame_in_this_packet_ is checked in IngestPacket to allow
  // advancing our state to track the difference between single-packet CHLO
  // and multi-packet CHLO.
  parsed_crypto_frame_in_this_packet_ = true;
  crypto_stream_sequencer_.OnCryptoFrame(frame);
  return true;
}

// Called by the QuicStreamSequencer when it receives a CRYPTO frame that
// advances the amount of contiguous data we now have starting from offset 0.
void TlsChloExtractor::OnDataAvailable() {
  // Lazily set up BoringSSL handle.
  SetupSslHandle();

  // Get data from the stream sequencer and pass it to BoringSSL.
  struct iovec iov;
  while (crypto_stream_sequencer_.GetReadableRegion(&iov)) {
    const int rv = SSL_provide_quic_data(
        ssl_.get(), ssl_encryption_initial,
        reinterpret_cast<const uint8_t*>(iov.iov_base), iov.iov_len);
    if (rv != 1) {
      HandleUnrecoverableError("SSL_provide_quic_data failed");
      return;
    }
    crypto_stream_sequencer_.MarkConsumed(iov.iov_len);
  }

  // Instruct BoringSSL to attempt parsing a full CHLO from the provided data.
  // We ignore the return value since we know the handshake is going to fail
  // because we explicitly cancel processing once we've parsed the CHLO.
  (void)SSL_do_handshake(ssl_.get());
}

// static
TlsChloExtractor* TlsChloExtractor::GetInstanceFromSSL(SSL* ssl) {
  std::pair<SSL_CTX*, int> shared_handles = GetSharedSslHandles();
  int ex_data_index = shared_handles.second;
  return reinterpret_cast<TlsChloExtractor*>(
      SSL_get_ex_data(ssl, ex_data_index));
}

// static
int TlsChloExtractor::SetReadSecretCallback(
    SSL* ssl, enum ssl_encryption_level_t /*level*/,
    const SSL_CIPHER* /*cipher*/, const uint8_t* /*secret*/,
    size_t /*secret_length*/) {
  GetInstanceFromSSL(ssl)->HandleUnexpectedCallback("SetReadSecretCallback");
  return 0;
}

// static
int TlsChloExtractor::SetWriteSecretCallback(
    SSL* ssl, enum ssl_encryption_level_t /*level*/,
    const SSL_CIPHER* /*cipher*/, const uint8_t* /*secret*/,
    size_t /*secret_length*/) {
  GetInstanceFromSSL(ssl)->HandleUnexpectedCallback("SetWriteSecretCallback");
  return 0;
}

// static
int TlsChloExtractor::WriteMessageCallback(
    SSL* ssl, enum ssl_encryption_level_t /*level*/, const uint8_t* /*data*/,
    size_t /*len*/) {
  GetInstanceFromSSL(ssl)->HandleUnexpectedCallback("WriteMessageCallback");
  return 0;
}

// static
int TlsChloExtractor::FlushFlightCallback(SSL* ssl) {
  GetInstanceFromSSL(ssl)->HandleUnexpectedCallback("FlushFlightCallback");
  return 0;
}

void TlsChloExtractor::HandleUnexpectedCallback(
    const std::string& callback_name) {
  std::string error_details =
      absl::StrCat("Unexpected callback ", callback_name);
  QUIC_BUG(quic_bug_10855_3) << error_details;
  HandleUnrecoverableError(error_details);
}

// static
int TlsChloExtractor::SendAlertCallback(SSL* ssl,
                                        enum ssl_encryption_level_t /*level*/,
                                        uint8_t desc) {
  GetInstanceFromSSL(ssl)->SendAlert(desc);
  return 0;
}

void TlsChloExtractor::SendAlert(uint8_t tls_alert_value) {
  if (tls_alert_value == SSL3_AD_HANDSHAKE_FAILURE && HasParsedFullChlo()) {
    // This is the most common scenario. Since we return an error from
    // SelectCertCallback in order to cancel further processing, BoringSSL will
    // try to send this alert to tell the client that the handshake failed.
    return;
  }
  HandleUnrecoverableError(absl::StrCat(
      "BoringSSL attempted to send alert ", static_cast<int>(tls_alert_value),
      " ", SSL_alert_desc_string_long(tls_alert_value)));
  if (state_ == State::kUnrecoverableFailure) {
    tls_alert_ = tls_alert_value;
  }
}

// static
enum ssl_select_cert_result_t TlsChloExtractor::SelectCertCallback(
    const SSL_CLIENT_HELLO* client_hello) {
  GetInstanceFromSSL(client_hello->ssl)->HandleParsedChlo(client_hello);
  // Always return an error to cancel any further processing in BoringSSL.
  return ssl_select_cert_error;
}

// Extracts the server name and ALPN from the parsed ClientHello.
void TlsChloExtractor::HandleParsedChlo(const SSL_CLIENT_HELLO* client_hello) {
  const char* server_name =
      SSL_get_servername(client_hello->ssl, TLSEXT_NAMETYPE_host_name);
  if (server_name) {
    server_name_ = std::string(server_name);
  }

  resumption_attempted_ =
      HasExtension(client_hello, TLSEXT_TYPE_pre_shared_key);
  early_data_attempted_ = HasExtension(client_hello, TLSEXT_TYPE_early_data);

  QUICHE_DCHECK(client_hello_bytes_.empty());
  client_hello_bytes_.assign(
      client_hello->client_hello,
      client_hello->client_hello + client_hello->client_hello_len);

  const uint8_t* alpn_data;
  size_t alpn_len;
  int rv = SSL_early_callback_ctx_extension_get(
      client_hello, TLSEXT_TYPE_application_layer_protocol_negotiation,
      &alpn_data, &alpn_len);
  if (rv == 1) {
    QuicDataReader alpns_reader(reinterpret_cast<const char*>(alpn_data),
                                alpn_len);
    absl::string_view alpns_payload;
    if (!alpns_reader.ReadStringPiece16(&alpns_payload)) {
      QUIC_CODE_COUNT_N(quic_chlo_alpns_invalid, 1, 2);
      HandleUnrecoverableError("Failed to read alpns_payload");
      return;
    }
    QuicDataReader alpns_payload_reader(alpns_payload);
    while (!alpns_payload_reader.IsDoneReading()) {
      absl::string_view alpn_payload;
      if (!alpns_payload_reader.ReadStringPiece8(&alpn_payload)) {
        QUIC_CODE_COUNT_N(quic_chlo_alpns_invalid, 2, 2);
        HandleUnrecoverableError("Failed to read alpn_payload");
        return;
      }
      alpns_.emplace_back(std::string(alpn_payload));
    }
  }

  supported_groups_ = GetSupportedGroups(client_hello);
  if (GetQuicReloadableFlag(quic_parse_cert_compression_algos_from_chlo)) {
    cert_compression_algos_ = GetCertCompressionAlgos(client_hello);
    if (cert_compression_algos_.empty()) {
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_parse_cert_compression_algos_from_chlo,
                                   1, 2);
    } else {
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_parse_cert_compression_algos_from_chlo,
                                   2, 2);
    }
  }

  // Update our state now that we've parsed a full CHLO.
  if (state_ == State::kInitial) {
    state_ = State::kParsedFullSinglePacketChlo;
  } else if (state_ == State::kParsedPartialChloFragment) {
    state_ = State::kParsedFullMultiPacketChlo;
  } else {
    QUIC_BUG(quic_bug_10855_4)
        << "Unexpected state on successful parse " << StateToString(state_);
  }
}

// static
std::pair<SSL_CTX*, int> TlsChloExtractor::GetSharedSslHandles() {
  // Use a lambda to benefit from C++11 guarantee that static variables are
  // initialized lazily in a thread-safe manner. |shared_handles| is therefore
  // guaranteed to be initialized exactly once and never destructed.
  static std::pair<SSL_CTX*, int>* shared_handles = []() {
    CRYPTO_library_init();
    SSL_CTX* ssl_ctx = SSL_CTX_new(TLS_with_buffers_method());
    SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_3_VERSION);
    static const SSL_QUIC_METHOD kQuicCallbacks{
        TlsChloExtractor::SetReadSecretCallback,
        TlsChloExtractor::SetWriteSecretCallback,
        TlsChloExtractor::WriteMessageCallback,
        TlsChloExtractor::FlushFlightCallback,
        TlsChloExtractor::SendAlertCallback};
    SSL_CTX_set_quic_method(ssl_ctx, &kQuicCallbacks);
    SSL_CTX_set_select_certificate_cb(ssl_ctx,
                                      TlsChloExtractor::SelectCertCallback);
    int ex_data_index =
        SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
    return new std::pair<SSL_CTX*, int>(ssl_ctx, ex_data_index);
  }();
  return *shared_handles;
}

// Sets up the per-instance SSL handle needed by BoringSSL.
void TlsChloExtractor::SetupSslHandle() {
  if (ssl_) {
    // Handles have already been set up.
    return;
  }

  std::pair<SSL_CTX*, int> shared_handles = GetSharedSslHandles();
  SSL_CTX* ssl_ctx = shared_handles.first;
  int ex_data_index = shared_handles.second;

  ssl_ = bssl::UniquePtr<SSL>(SSL_new(ssl_ctx));
  const int rv = SSL_set_ex_data(ssl_.get(), ex_data_index, this);
  QUICHE_CHECK_EQ(rv, 1) << "Internal allocation failure in SSL_set_ex_data";
  SSL_set_accept_state(ssl_.get());

  // Make sure we use the right TLS extension codepoint.
  int use_legacy_extension = 0;
  if (framer_->version().UsesLegacyTlsExtension()) {
    use_legacy_extension = 1;
  }
  SSL_set_quic_use_legacy_codepoint(ssl_.get(), use_legacy_extension);
}

// Called by other methods to record any unrecoverable failures they experience.
void TlsChloExtractor::HandleUnrecoverableError(
    const std::string& error_details) {
  if (HasParsedFullChlo()) {
    // Ignore errors if we've parsed everything successfully.
    QUIC_DLOG(ERROR) << "Ignoring error: " << error_details;
    return;
  }
  QUIC_DLOG(ERROR) << "Handling error: " << error_details;

  state_ = State::kUnrecoverableFailure;

  if (error_details_.empty()) {
    error_details_ = error_details;
  } else {
    error_details_ = absl::StrCat(error_details_, "; ", error_details);
  }
}

// static
std::string TlsChloExtractor::StateToString(State state) {
  switch (state) {
    case State::kInitial:
      return "Initial";
    case State::kParsedFullSinglePacketChlo:
      return "ParsedFullSinglePacketChlo";
    case State::kParsedFullMultiPacketChlo:
      return "ParsedFullMultiPacketChlo";
    case State::kParsedPartialChloFragment:
      return "ParsedPartialChloFragment";
    case State::kUnrecoverableFailure:
      return "UnrecoverableFailure";
  }
  return absl::StrCat("Unknown(", static_cast<int>(state), ")");
}

std::ostream& operator<<(std::ostream& os,
                         const TlsChloExtractor::State& state) {
  os << TlsChloExtractor::StateToString(state);
  return os;
}

}  // namespace quic

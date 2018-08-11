// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/quartc/quartc_session.h"

#include "net/third_party/quic/core/tls_client_handshaker.h"
#include "net/third_party/quic/core/tls_server_handshaker.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"

using std::string;

namespace net {

namespace {

// Arbitrary server port number for net::QuicCryptoClientConfig.
const int kQuicServerPort = 0;

// Length of HKDF input keying material, equal to its number of bytes.
// https://tools.ietf.org/html/rfc5869#section-2.2.
// TODO(zhihuang): Verify that input keying material length is correct.
const size_t kInputKeyingMaterialLength = 32;

// Used by QuicCryptoServerConfig to provide dummy proof credentials.
// TODO(zhihuang): Remove when secure P2P QUIC handshake is possible.
class DummyProofSource : public ProofSource {
 public:
  DummyProofSource() {}
  ~DummyProofSource() override {}

  // ProofSource override.
  void GetProof(const QuicSocketAddress& server_addr,
                const string& hostname,
                const string& server_config,
                QuicTransportVersion transport_version,
                QuicStringPiece chlo_hash,
                std::unique_ptr<Callback> callback) override {
    QuicReferenceCountedPointer<ProofSource::Chain> chain;
    QuicCryptoProof proof;
    std::vector<string> certs;
    certs.push_back("Dummy cert");
    chain = new ProofSource::Chain(certs);
    proof.signature = "Dummy signature";
    proof.leaf_cert_scts = "Dummy timestamp";
    callback->Run(true, chain, proof, nullptr /* details */);
  }

  QuicReferenceCountedPointer<Chain> GetCertChain(
      const QuicSocketAddress& server_address,
      const string& hostname) override {
    return QuicReferenceCountedPointer<Chain>();
  }

  void ComputeTlsSignature(
      const QuicSocketAddress& server_address,
      const string& hostname,
      uint16_t signature_algorithm,
      QuicStringPiece in,
      std::unique_ptr<SignatureCallback> callback) override {
    callback->Run(true, "Dummy signature");
  }
};

// Used by QuicCryptoClientConfig to ignore the peer's credentials
// and establish an insecure QUIC connection.
// TODO(zhihuang): Remove when secure P2P QUIC handshake is possible.
class InsecureProofVerifier : public ProofVerifier {
 public:
  InsecureProofVerifier() {}
  ~InsecureProofVerifier() override {}

  // ProofVerifier override.
  QuicAsyncStatus VerifyProof(
      const string& hostname,
      const uint16_t port,
      const string& server_config,
      QuicTransportVersion transport_version,
      QuicStringPiece chlo_hash,
      const std::vector<string>& certs,
      const string& cert_sct,
      const string& signature,
      const ProofVerifyContext* context,
      string* error_details,
      std::unique_ptr<ProofVerifyDetails>* verify_details,
      std::unique_ptr<ProofVerifierCallback> callback) override {
    return QUIC_SUCCESS;
  }

  QuicAsyncStatus VerifyCertChain(
      const string& hostname,
      const std::vector<string>& certs,
      const ProofVerifyContext* context,
      string* error_details,
      std::unique_ptr<ProofVerifyDetails>* details,
      std::unique_ptr<ProofVerifierCallback> callback) override {
    return QUIC_SUCCESS;
  }
};

}  // namespace

QuicConnectionId QuartcCryptoServerStreamHelper::GenerateConnectionIdForReject(
    QuicConnectionId connection_id) const {
  return 0;
}

bool QuartcCryptoServerStreamHelper::CanAcceptClientHello(
    const CryptoHandshakeMessage& message,
    const QuicSocketAddress& client_address,
    const QuicSocketAddress& peer_address,
    const QuicSocketAddress& self_address,
    string* error_details) const {
  return true;
}

QuartcSessionVisitorAdapter::~QuartcSessionVisitorAdapter() {}

QuartcSessionVisitorAdapter::QuartcSessionVisitorAdapter() {}

void QuartcSessionVisitorAdapter::OnPacketSent(
    const SerializedPacket& serialized_packet,
    QuicPacketNumber original_packet_number,
    TransmissionType transmission_type,
    QuicTime sent_time) {
  for (QuartcSessionVisitor* visitor : visitors_) {
    visitor->OnPacketSent(serialized_packet, original_packet_number,
                          transmission_type, sent_time);
  }
}

void QuartcSessionVisitorAdapter::OnIncomingAck(
    const QuicAckFrame& ack_frame,
    QuicTime ack_receive_time,
    QuicPacketNumber largest_observed,
    bool rtt_updated,
    QuicPacketNumber least_unacked_sent_packet) {
  for (QuartcSessionVisitor* visitor : visitors_) {
    visitor->OnIncomingAck(ack_frame, ack_receive_time, largest_observed,
                           rtt_updated, least_unacked_sent_packet);
  }
}

void QuartcSessionVisitorAdapter::OnPacketLoss(
    QuicPacketNumber lost_packet_number,
    TransmissionType transmission_type,
    QuicTime detection_time) {
  for (QuartcSessionVisitor* visitor : visitors_) {
    visitor->OnPacketLoss(lost_packet_number, transmission_type,
                          detection_time);
  }
}

void QuartcSessionVisitorAdapter::OnWindowUpdateFrame(
    const QuicWindowUpdateFrame& frame,
    const QuicTime& receive_time) {
  for (QuartcSessionVisitor* visitor : visitors_) {
    visitor->OnWindowUpdateFrame(frame, receive_time);
  }
}

void QuartcSessionVisitorAdapter::OnSuccessfulVersionNegotiation(
    const ParsedQuicVersion& version) {
  for (QuartcSessionVisitor* visitor : visitors_) {
    visitor->OnSuccessfulVersionNegotiation(version);
  }
}

QuartcSession::QuartcSession(std::unique_ptr<QuicConnection> connection,
                             const QuicConfig& config,
                             const string& unique_remote_server_id,
                             Perspective perspective,
                             QuicConnectionHelperInterface* helper,
                             QuicClock* clock,
                             std::unique_ptr<QuartcPacketWriter> packet_writer)
    : QuicSession(connection.get(), nullptr /*visitor*/, config),
      unique_remote_server_id_(unique_remote_server_id),
      perspective_(perspective),
      connection_(std::move(connection)),
      helper_(helper),
      clock_(clock),
      packet_writer_(std::move(packet_writer)) {
  packet_writer_->set_connection(connection_.get());

  // Initialization with default crypto configuration.
  if (perspective_ == Perspective::IS_CLIENT) {
    std::unique_ptr<ProofVerifier> proof_verifier(new InsecureProofVerifier);
    quic_crypto_client_config_ = QuicMakeUnique<QuicCryptoClientConfig>(
        std::move(proof_verifier), TlsClientHandshaker::CreateSslCtx());
  } else {
    std::unique_ptr<ProofSource> proof_source(new DummyProofSource);
    // Generate a random source address token secret. For long-running servers
    // it's better to not regenerate it for each connection to enable zero-RTT
    // handshakes, but for transient clients it does not matter.
    char source_address_token_secret[kInputKeyingMaterialLength];
    helper_->GetRandomGenerator()->RandBytes(source_address_token_secret,
                                             kInputKeyingMaterialLength);
    quic_crypto_server_config_ = QuicMakeUnique<QuicCryptoServerConfig>(
        string(source_address_token_secret, kInputKeyingMaterialLength),
        helper_->GetRandomGenerator(), std::move(proof_source),
        TlsServerHandshaker::CreateSslCtx());
    // Provide server with serialized config string to prove ownership.
    QuicCryptoServerConfig::ConfigOptions options;
    // The |message| is used to handle the return value of AddDefaultConfig
    // which is raw pointer of the CryptoHandshakeMessage.
    std::unique_ptr<CryptoHandshakeMessage> message(
        quic_crypto_server_config_->AddDefaultConfig(
            helper_->GetRandomGenerator(), helper_->GetClock(), options));
  }
}

QuartcSession::~QuartcSession() {}

const QuicCryptoStream* QuartcSession::GetCryptoStream() const {
  return crypto_stream_.get();
}

QuicCryptoStream* QuartcSession::GetMutableCryptoStream() {
  return crypto_stream_.get();
}

QuartcStream* QuartcSession::CreateOutgoingDynamicStream() {
  // Use default priority for incoming QUIC streams.
  // TODO(zhihuang): Determine if this value is correct.
  return ActivateDataStream(CreateDataStream(GetNextOutgoingStreamId(),
                                             QuicStream::kDefaultPriority));
}

void QuartcSession::OnCryptoHandshakeEvent(CryptoHandshakeEvent event) {
  QuicSession::OnCryptoHandshakeEvent(event);
  if (event == HANDSHAKE_CONFIRMED) {
    DCHECK(IsEncryptionEstablished());
    DCHECK(IsCryptoHandshakeConfirmed());

    DCHECK(session_delegate_);
    session_delegate_->OnCryptoHandshakeComplete();
  }
}

void QuartcSession::CloseStream(QuicStreamId stream_id) {
  if (IsClosedStream(stream_id)) {
    // When CloseStream has been called recursively (via
    // QuicStream::OnClose), the stream is already closed so return.
    return;
  }
  QuicSession::CloseStream(stream_id);
}

void QuartcSession::CancelStream(QuicStreamId stream_id) {
  ResetStream(stream_id, QuicRstStreamErrorCode::QUIC_STREAM_CANCELLED);
}

void QuartcSession::ResetStream(QuicStreamId stream_id,
                                QuicRstStreamErrorCode error) {
  if (!IsOpenStream(stream_id)) {
    return;
  }
  QuicStream* stream = QuicSession::GetOrCreateStream(stream_id);
  if (stream) {
    stream->Reset(error);
  }
}

bool QuartcSession::IsOpenStream(QuicStreamId stream_id) {
  return QuicSession::IsOpenStream(stream_id);
}

QuicConnectionStats QuartcSession::GetStats() {
  return connection_->GetStats();
}

void QuartcSession::OnConnectionClosed(QuicErrorCode error,
                                       const string& error_details,
                                       ConnectionCloseSource source) {
  QuicSession::OnConnectionClosed(error, error_details, source);
  DCHECK(session_delegate_);
  session_delegate_->OnConnectionClosed(
      error, source == ConnectionCloseSource::FROM_PEER);
}

void QuartcSession::StartCryptoHandshake() {
  if (perspective_ == Perspective::IS_CLIENT) {
    QuicServerId server_id(unique_remote_server_id_, kQuicServerPort);
    QuicCryptoClientStream* crypto_stream =
        new QuicCryptoClientStream(server_id, this, new ProofVerifyContext(),
                                   quic_crypto_client_config_.get(), this);
    crypto_stream_.reset(crypto_stream);
    QuicSession::Initialize();
    crypto_stream->CryptoConnect();
  } else {
    quic_compressed_certs_cache_.reset(new QuicCompressedCertsCache(
        QuicCompressedCertsCache::kQuicCompressedCertsCacheSize));
    bool use_stateless_rejects_if_peer_supported = false;
    QuicCryptoServerStream* crypto_stream = new QuicCryptoServerStream(
        quic_crypto_server_config_.get(), quic_compressed_certs_cache_.get(),
        use_stateless_rejects_if_peer_supported, this, &stream_helper_);
    crypto_stream_.reset(crypto_stream);
    QuicSession::Initialize();
  }
}

bool QuartcSession::ExportKeyingMaterial(const string& label,
                                         const uint8_t* context,
                                         size_t context_len,
                                         bool used_context,
                                         uint8_t* result,
                                         size_t result_len) {
  string quic_context(reinterpret_cast<const char*>(context), context_len);
  string quic_result;
  bool success = crypto_stream_->ExportKeyingMaterial(label, quic_context,
                                                      result_len, &quic_result);
  quic_result.copy(reinterpret_cast<char*>(result), result_len);
  DCHECK(quic_result.length() == result_len);
  return success;
}

void QuartcSession::CloseConnection(const string& details) {
  connection_->CloseConnection(
      QuicErrorCode::QUIC_CONNECTION_CANCELLED, details,
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET_WITH_NO_ACK);
}

QuartcStreamInterface* QuartcSession::CreateOutgoingStream(
    const OutgoingStreamParameters& param) {
  // The |param| is for forward-compatibility. Not used for now.
  return CreateOutgoingDynamicStream();
}

void QuartcSession::SetDelegate(
    QuartcSessionInterface::Delegate* session_delegate) {
  if (session_delegate_) {
    LOG(WARNING) << "The delegate for the session has already been set.";
  }
  session_delegate_ = session_delegate;
  DCHECK(session_delegate_);
}

void QuartcSession::AddSessionVisitor(QuartcSessionVisitor* visitor) {
  // If there aren't any visitors yet, install the adapter as a connection debug
  // visitor to delegate any future calls.
  if (session_visitor_adapter_.visitors().empty()) {
    connection_->set_debug_visitor(&session_visitor_adapter_);
  }
  session_visitor_adapter_.mutable_visitors().insert(visitor);
  visitor->OnQuicConnection(connection_.get());
}

void QuartcSession::RemoveSessionVisitor(QuartcSessionVisitor* visitor) {
  session_visitor_adapter_.mutable_visitors().erase(visitor);
  // If the last visitor is removed, uninstall the connection debug visitor to
  // avoid delegating debug calls unnecessarily.
  if (session_visitor_adapter_.visitors().empty()) {
    connection_->set_debug_visitor(nullptr);
  }
}

void QuartcSession::OnTransportCanWrite() {
  connection()->writer()->SetWritable();
  if (HasDataToWrite()) {
    connection()->OnCanWrite();
  }
}

bool QuartcSession::OnTransportReceived(const char* data, size_t data_len) {
  // If the session is currently bundling packets, it must stop and flush writes
  // before processing incoming data.  QUIC expects pending packets to be
  // written before receiving data, because received data may change the
  // contents of ACK frames in pending packets.
  FlushWrites();

  QuicReceivedPacket packet(data, data_len, clock_->Now());
  ProcessUdpPacket(connection()->self_address(), connection()->peer_address(),
                   packet);
  return true;
}

void QuartcSession::BundleWrites() {
  if (!packet_flusher_) {
    packet_flusher_ = QuicMakeUnique<QuicConnection::ScopedPacketFlusher>(
        connection_.get(), QuicConnection::SEND_ACK_IF_QUEUED);
  }
}

void QuartcSession::FlushWrites() {
  packet_flusher_ = nullptr;
}

void QuartcSession::OnProofValid(
    const QuicCryptoClientConfig::CachedState& cached) {
  // TODO(zhihuang): Handle the proof verification.
}

void QuartcSession::OnProofVerifyDetailsAvailable(
    const ProofVerifyDetails& verify_details) {
  // TODO(zhihuang): Handle the proof verification.
}

void QuartcSession::SetClientCryptoConfig(
    QuicCryptoClientConfig* client_config) {
  quic_crypto_client_config_.reset(client_config);
}

void QuartcSession::SetServerCryptoConfig(
    QuicCryptoServerConfig* server_config) {
  quic_crypto_server_config_.reset(server_config);
}

QuicStream* QuartcSession::CreateIncomingDynamicStream(QuicStreamId id) {
  return ActivateDataStream(CreateDataStream(id, QuicStream::kDefaultPriority));
}

std::unique_ptr<QuartcStream> QuartcSession::CreateDataStream(
    QuicStreamId id,
    spdy::SpdyPriority priority) {
  if (crypto_stream_ == nullptr || !crypto_stream_->encryption_established()) {
    // Encryption not active so no stream created
    return nullptr;
  }
  auto stream = QuicMakeUnique<QuartcStream>(id, this);
  if (stream) {
    // Register the stream to the QuicWriteBlockedList. |priority| is clamped
    // between 0 and 7, with 0 being the highest priority and 7 the lowest
    // priority.
    write_blocked_streams()->UpdateStreamPriority(stream->id(), priority);

    if (IsIncomingStream(id)) {
      DCHECK(session_delegate_);
      // Incoming streams need to be registered with the session_delegate_.
      session_delegate_->OnIncomingStream(stream.get());
    }
  }
  return stream;
}

QuartcStream* QuartcSession::ActivateDataStream(
    std::unique_ptr<QuartcStream> stream) {
  // Transfer ownership of the data stream to the session via ActivateStream().
  QuartcStream* raw = stream.release();
  if (raw) {
    // Make QuicSession take ownership of the stream.
    ActivateStream(std::unique_ptr<QuicStream>(raw));
  }
  return raw;
}

}  // namespace net

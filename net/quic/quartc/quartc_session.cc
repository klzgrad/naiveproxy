// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quartc/quartc_session.h"

#include "net/quic/platform/api/quic_ptr_util.h"

using std::string;

namespace net {

namespace {

// Default priority for incoming QUIC streams.
// TODO(zhihuang): Determine if this value is correct.
static const SpdyPriority kDefaultPriority = 3;

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
    const QuicSocketAddress& self_address,
    string* error_details) const {
  return true;
}

QuartcSession::QuartcSession(std::unique_ptr<QuicConnection> connection,
                             const QuicConfig& config,
                             const string& unique_remote_server_id,
                             Perspective perspective,
                             QuicConnectionHelperInterface* helper,
                             QuicClock* clock)
    : QuicSession(connection.get(), nullptr /*visitor*/, config),
      unique_remote_server_id_(unique_remote_server_id),
      perspective_(perspective),
      connection_(std::move(connection)),
      helper_(helper),
      clock_(clock) {
  // Initialization with default crypto configuration.
  if (perspective_ == Perspective::IS_CLIENT) {
    std::unique_ptr<ProofVerifier> proof_verifier(new InsecureProofVerifier);
    quic_crypto_client_config_.reset(
        new QuicCryptoClientConfig(std::move(proof_verifier)));
  } else {
    std::unique_ptr<ProofSource> proof_source(new DummyProofSource);
    // Generate a random source address token secret. For long-running servers
    // it's better to not regenerate it for each connection to enable zero-RTT
    // handshakes, but for transient clients it does not matter.
    char source_address_token_secret[kInputKeyingMaterialLength];
    helper_->GetRandomGenerator()->RandBytes(source_address_token_secret,
                                             kInputKeyingMaterialLength);
    quic_crypto_server_config_.reset(new QuicCryptoServerConfig(
        string(source_address_token_secret, kInputKeyingMaterialLength),
        helper_->GetRandomGenerator(), std::move(proof_source)));
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
  return ActivateDataStream(
      CreateDataStream(GetNextOutgoingStreamId(), kDefaultPriority));
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
  write_blocked_streams()->UnregisterStream(stream_id);
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

QuartcSessionStats QuartcSession::GetStats() {
  QuartcSessionStats stats;
  const QuicConnectionStats& connection_stats = connection_->GetStats();
  stats.bandwidth_estimate = connection_stats.estimated_bandwidth;
  stats.smoothed_rtt =
      QuicTime::Delta::FromMicroseconds(connection_stats.srtt_us);
  return stats;
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

void QuartcSession::OnTransportCanWrite() {
  if (HasDataToWrite()) {
    connection()->OnCanWrite();
  }
}

bool QuartcSession::OnTransportReceived(const char* data, size_t data_len) {
  QuicReceivedPacket packet(data, data_len, clock_->Now());
  ProcessUdpPacket(connection()->self_address(), connection()->peer_address(),
                   packet);
  return true;
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
  return ActivateDataStream(CreateDataStream(id, kDefaultPriority));
}

std::unique_ptr<QuartcStream> QuartcSession::CreateDataStream(
    QuicStreamId id,
    SpdyPriority priority) {
  if (crypto_stream_ == nullptr || !crypto_stream_->encryption_established()) {
    // Encryption not active so no stream created
    return nullptr;
  }
  auto stream = QuicMakeUnique<QuartcStream>(id, this);
  if (stream) {
    // Register the stream to the QuicWriteBlockedList. |priority| is clamped
    // between 0 and 7, with 0 being the highest priority and 7 the lowest
    // priority.
    write_blocked_streams()->RegisterStream(stream->id(), priority);

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

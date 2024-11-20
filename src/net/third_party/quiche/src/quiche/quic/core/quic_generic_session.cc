// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_generic_session.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/web_transport_stream_adapter.h"
#include "quiche/quic/core/quic_crypto_client_stream.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_stream_priority.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

namespace {

class NoOpProofHandler : public QuicCryptoClientStream::ProofHandler {
 public:
  void OnProofValid(const QuicCryptoClientConfig::CachedState&) override {}
  void OnProofVerifyDetailsAvailable(const ProofVerifyDetails&) override {}
};

class NoOpServerCryptoHelper : public QuicCryptoServerStreamBase::Helper {
 public:
  bool CanAcceptClientHello(const CryptoHandshakeMessage& /*message*/,
                            const QuicSocketAddress& /*client_address*/,
                            const QuicSocketAddress& /*peer_address*/,
                            const QuicSocketAddress& /*self_address*/,
                            std::string* /*error_details*/) const override {
    return true;
  }
};

}  // namespace

ParsedQuicVersionVector GetQuicVersionsForGenericSession() {
  return {ParsedQuicVersion::RFCv1()};
}

// QuicGenericStream is a stream that provides a general-purpose implementation
// of a webtransport::Stream interface.
class QUICHE_EXPORT QuicGenericStream : public QuicStream {
 public:
  QuicGenericStream(QuicStreamId id, QuicSession* session)
      : QuicStream(id, session, /*is_static=*/false,
                   QuicUtils::GetStreamType(
                       id, session->connection()->perspective(),
                       session->IsIncomingStream(id), session->version())),
        adapter_(session, this, sequencer(), std::nullopt) {
    adapter_.SetPriority(webtransport::StreamPriority{0, 0});
  }

  WebTransportStreamAdapter* adapter() { return &adapter_; }

  // QuicSession method implementations.
  void OnDataAvailable() override { adapter_.OnDataAvailable(); }
  void OnCanWriteNewData() override { adapter_.OnCanWriteNewData(); }

 private:
  WebTransportStreamAdapter adapter_;
};

QuicGenericSessionBase::QuicGenericSessionBase(
    QuicConnection* connection, bool owns_connection, Visitor* owner,
    const QuicConfig& config, std::string alpn, WebTransportVisitor* visitor,
    bool owns_visitor,
    std::unique_ptr<QuicDatagramQueue::Observer> datagram_observer)
    : QuicSession(connection, owner, config, GetQuicVersionsForGenericSession(),
                  /*num_expected_unidirectional_static_streams=*/0,
                  std::move(datagram_observer),
                  QuicPriorityType::kWebTransport),
      alpn_(std::move(alpn)),
      visitor_(visitor),
      owns_connection_(owns_connection),
      owns_visitor_(owns_visitor) {}

QuicGenericSessionBase::~QuicGenericSessionBase() {
  if (owns_connection_) {
    DeleteConnection();
  }
  if (owns_visitor_) {
    delete visitor_;
    visitor_ = nullptr;
  }
}

QuicStream* QuicGenericSessionBase::CreateIncomingStream(QuicStreamId id) {
  QUIC_DVLOG(1) << "Creating incoming QuicGenricStream " << id;
  QuicGenericStream* stream = CreateStream(id);
  if (stream->type() == BIDIRECTIONAL) {
    incoming_bidirectional_streams_.push_back(id);
    visitor_->OnIncomingBidirectionalStreamAvailable();
  } else {
    incoming_unidirectional_streams_.push_back(id);
    visitor_->OnIncomingUnidirectionalStreamAvailable();
  }
  return stream;
}

void QuicGenericSessionBase::OnTlsHandshakeComplete() {
  QuicSession::OnTlsHandshakeComplete();
  visitor_->OnSessionReady();
}

webtransport::Stream*
QuicGenericSessionBase::AcceptIncomingBidirectionalStream() {
  while (!incoming_bidirectional_streams_.empty()) {
    webtransport::Stream* stream =
        GetStreamById(incoming_bidirectional_streams_.front());
    incoming_bidirectional_streams_.pop_front();
    if (stream != nullptr) {
      return stream;
    }
  }
  return nullptr;
}

webtransport::Stream*
QuicGenericSessionBase::AcceptIncomingUnidirectionalStream() {
  while (!incoming_unidirectional_streams_.empty()) {
    webtransport::Stream* stream =
        GetStreamById(incoming_unidirectional_streams_.front());
    incoming_unidirectional_streams_.pop_front();
    if (stream != nullptr) {
      return stream;
    }
  }
  return nullptr;
}

webtransport::Stream*
QuicGenericSessionBase::OpenOutgoingBidirectionalStream() {
  if (!CanOpenNextOutgoingBidirectionalStream()) {
    QUIC_BUG(QuicGenericSessionBase_flow_control_violation_bidi)
        << "Attempted to open a stream in violation of flow control";
    return nullptr;
  }
  return CreateStream(GetNextOutgoingBidirectionalStreamId())->adapter();
}

webtransport::Stream*
QuicGenericSessionBase::OpenOutgoingUnidirectionalStream() {
  if (!CanOpenNextOutgoingUnidirectionalStream()) {
    QUIC_BUG(QuicGenericSessionBase_flow_control_violation_unidi)
        << "Attempted to open a stream in violation of flow control";
    return nullptr;
  }
  return CreateStream(GetNextOutgoingUnidirectionalStreamId())->adapter();
}

QuicGenericStream* QuicGenericSessionBase::CreateStream(QuicStreamId id) {
  auto stream = std::make_unique<QuicGenericStream>(id, this);
  QuicGenericStream* stream_ptr = stream.get();
  ActivateStream(std::move(stream));
  return stream_ptr;
}

void QuicGenericSessionBase::OnMessageReceived(absl::string_view message) {
  visitor_->OnDatagramReceived(message);
}

void QuicGenericSessionBase::OnCanCreateNewOutgoingStream(bool unidirectional) {
  if (unidirectional) {
    visitor_->OnCanCreateNewOutgoingUnidirectionalStream();
  } else {
    visitor_->OnCanCreateNewOutgoingBidirectionalStream();
  }
}

webtransport::Stream* QuicGenericSessionBase::GetStreamById(
    webtransport::StreamId id) {
  QuicStream* stream = GetActiveStream(id);
  if (stream == nullptr) {
    return nullptr;
  }
  return static_cast<QuicGenericStream*>(stream)->adapter();
}

webtransport::DatagramStatus QuicGenericSessionBase::SendOrQueueDatagram(
    absl::string_view datagram) {
  quiche::QuicheBuffer buffer = quiche::QuicheBuffer::Copy(
      quiche::SimpleBufferAllocator::Get(), datagram);
  return MessageStatusToWebTransportStatus(
      datagram_queue()->SendOrQueueDatagram(
          quiche::QuicheMemSlice(std::move(buffer))));
}

void QuicGenericSessionBase::OnConnectionClosed(
    const QuicConnectionCloseFrame& frame, ConnectionCloseSource source) {
  QuicSession::OnConnectionClosed(frame, source);
  visitor_->OnSessionClosed(static_cast<webtransport::SessionErrorCode>(
                                frame.transport_close_frame_type),
                            frame.error_details);
}

QuicGenericClientSession::QuicGenericClientSession(
    QuicConnection* connection, bool owns_connection, Visitor* owner,
    const QuicConfig& config, std::string host, uint16_t port, std::string alpn,
    webtransport::SessionVisitor* visitor, bool owns_visitor,
    std::unique_ptr<QuicDatagramQueue::Observer> datagram_observer,
    QuicCryptoClientConfig* crypto_config)
    : QuicGenericSessionBase(connection, owns_connection, owner, config,
                             std::move(alpn), visitor, owns_visitor,
                             std::move(datagram_observer)) {
  static NoOpProofHandler* handler = new NoOpProofHandler();
  crypto_stream_ = std::make_unique<QuicCryptoClientStream>(
      QuicServerId(std::move(host), port), this,
      crypto_config->proof_verifier()->CreateDefaultContext(), crypto_config,
      /*proof_handler=*/handler, /*has_application_state=*/false);
}

QuicGenericClientSession::QuicGenericClientSession(
    QuicConnection* connection, bool owns_connection, Visitor* owner,
    const QuicConfig& config, std::string host, uint16_t port, std::string alpn,
    CreateWebTransportSessionVisitorCallback create_visitor_callback,
    std::unique_ptr<QuicDatagramQueue::Observer> datagram_observer,
    QuicCryptoClientConfig* crypto_config)
    : QuicGenericClientSession(
          connection, owns_connection, owner, config, std::move(host), port,
          std::move(alpn), std::move(create_visitor_callback)(*this).release(),
          /*owns_visitor=*/true, std::move(datagram_observer), crypto_config) {}

QuicGenericServerSession::QuicGenericServerSession(
    QuicConnection* connection, bool owns_connection, Visitor* owner,
    const QuicConfig& config, std::string alpn,
    webtransport::SessionVisitor* visitor, bool owns_visitor,
    std::unique_ptr<QuicDatagramQueue::Observer> datagram_observer,
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache)
    : QuicGenericSessionBase(connection, owns_connection, owner, config,
                             std::move(alpn), visitor, owns_visitor,
                             std::move(datagram_observer)) {
  static NoOpServerCryptoHelper* helper = new NoOpServerCryptoHelper();
  crypto_stream_ = CreateCryptoServerStream(
      crypto_config, compressed_certs_cache, this, helper);
}

QuicGenericServerSession::QuicGenericServerSession(
    QuicConnection* connection, bool owns_connection, Visitor* owner,
    const QuicConfig& config, std::string alpn,
    CreateWebTransportSessionVisitorCallback create_visitor_callback,
    std::unique_ptr<QuicDatagramQueue::Observer> datagram_observer,
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache)
    : QuicGenericServerSession(
          connection, owns_connection, owner, config, std::move(alpn),
          std::move(create_visitor_callback)(*this).release(),
          /*owns_visitor=*/true, std::move(datagram_observer), crypto_config,
          compressed_certs_cache) {}

}  // namespace quic

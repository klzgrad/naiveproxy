// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/tools/quic_transport_simple_server_session.h"

#include <memory>

#include "url/gurl.h"
#include "url/origin.h"
#include "quic/core/quic_buffer_allocator.h"
#include "quic/core/quic_types.h"
#include "quic/core/quic_versions.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_logging.h"
#include "quic/quic_transport/quic_transport_protocol.h"
#include "quic/quic_transport/quic_transport_stream.h"
#include "quic/tools/web_transport_test_visitors.h"

namespace quic {

QuicTransportSimpleServerSession::QuicTransportSimpleServerSession(
    QuicConnection* connection,
    bool owns_connection,
    Visitor* owner,
    const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    const QuicCryptoServerConfig* crypto_config,
    QuicCompressedCertsCache* compressed_certs_cache,
    std::vector<url::Origin> accepted_origins)
    : QuicTransportServerSession(connection,
                                 owner,
                                 config,
                                 supported_versions,
                                 crypto_config,
                                 compressed_certs_cache,
                                 this),
      owns_connection_(owns_connection),
      mode_(DISCARD),
      accepted_origins_(accepted_origins) {}

QuicTransportSimpleServerSession::~QuicTransportSimpleServerSession() {
  if (owns_connection_) {
    DeleteConnection();
  }
}

void QuicTransportSimpleServerSession::OnIncomingDataStream(
    QuicTransportStream* stream) {
  switch (mode_) {
    case DISCARD:
      stream->SetVisitor(std::make_unique<WebTransportDiscardVisitor>(stream));
      break;

    case ECHO:
      switch (stream->type()) {
        case BIDIRECTIONAL:
          QUIC_DVLOG(1) << "Opening bidirectional echo stream " << stream->id();
          stream->SetVisitor(
              std::make_unique<WebTransportBidirectionalEchoVisitor>(stream));
          break;
        case READ_UNIDIRECTIONAL:
          QUIC_DVLOG(1)
              << "Started receiving data on unidirectional echo stream "
              << stream->id();
          stream->SetVisitor(
              std::make_unique<WebTransportUnidirectionalEchoReadVisitor>(
                  stream,
                  [this](const std::string& s) { this->EchoStreamBack(s); }));
          break;
        default:
          QUIC_NOTREACHED();
          break;
      }
      break;

    case OUTGOING_BIDIRECTIONAL:
      stream->SetVisitor(std::make_unique<WebTransportDiscardVisitor>(stream));
      ++pending_outgoing_bidirectional_streams_;
      MaybeCreateOutgoingBidirectionalStream();
      break;
  }
}

void QuicTransportSimpleServerSession::OnCanCreateNewOutgoingStream(
    bool unidirectional) {
  if (mode_ == ECHO && unidirectional) {
    MaybeEchoStreamsBack();
  } else if (mode_ == OUTGOING_BIDIRECTIONAL && !unidirectional) {
    MaybeCreateOutgoingBidirectionalStream();
  }
}

bool QuicTransportSimpleServerSession::CheckOrigin(url::Origin origin) {
  if (accepted_origins_.empty()) {
    return true;
  }

  for (const url::Origin& accepted_origin : accepted_origins_) {
    if (origin.IsSameOriginWith(accepted_origin)) {
      return true;
    }
  }
  return false;
}

bool QuicTransportSimpleServerSession::ProcessPath(const GURL& url) {
  if (url.path() == "/discard") {
    mode_ = DISCARD;
    return true;
  }
  if (url.path() == "/echo") {
    mode_ = ECHO;
    return true;
  }
  if (url.path() == "/receive-bidirectional") {
    mode_ = OUTGOING_BIDIRECTIONAL;
    return true;
  }

  QUIC_DLOG(WARNING) << "Unknown path requested: " << url.path();
  return false;
}

void QuicTransportSimpleServerSession::OnMessageReceived(
    absl::string_view message) {
  if (mode_ != ECHO) {
    return;
  }
  QuicUniqueBufferPtr buffer = MakeUniqueBuffer(
      connection()->helper()->GetStreamSendBufferAllocator(), message.size());
  memcpy(buffer.get(), message.data(), message.size());
  datagram_queue()->SendOrQueueDatagram(
      QuicMemSlice(std::move(buffer), message.size()));
}

void QuicTransportSimpleServerSession::MaybeEchoStreamsBack() {
  while (!streams_to_echo_back_.empty() &&
         CanOpenNextOutgoingUnidirectionalStream()) {
    // Remove the stream from the queue first, in order to avoid accidentally
    // entering an infinite loop in case any of the following code calls
    // OnCanCreateNewOutgoingStream().
    std::string data = std::move(streams_to_echo_back_.front());
    streams_to_echo_back_.pop_front();

    auto stream_owned = std::make_unique<QuicTransportStream>(
        GetNextOutgoingUnidirectionalStreamId(), this, this);
    QuicTransportStream* stream = stream_owned.get();
    ActivateStream(std::move(stream_owned));
    QUIC_DVLOG(1) << "Opened echo response stream " << stream->id();

    stream->SetVisitor(
        std::make_unique<WebTransportUnidirectionalEchoWriteVisitor>(stream,
                                                                     data));
    stream->visitor()->OnCanWrite();
  }
}

void QuicTransportSimpleServerSession::
    MaybeCreateOutgoingBidirectionalStream() {
  while (pending_outgoing_bidirectional_streams_ > 0 &&
         CanOpenNextOutgoingBidirectionalStream()) {
    auto stream_owned = std::make_unique<QuicTransportStream>(
        GetNextOutgoingBidirectionalStreamId(), this, this);
    QuicTransportStream* stream = stream_owned.get();
    ActivateStream(std::move(stream_owned));
    QUIC_DVLOG(1) << "Opened outgoing bidirectional stream " << stream->id();
    stream->SetVisitor(
        std::make_unique<WebTransportBidirectionalEchoVisitor>(stream));
    if (!stream->Write("hello")) {
      QUIC_DVLOG(1) << "Write failed.";
    }
    --pending_outgoing_bidirectional_streams_;
  }
}

}  // namespace quic

// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/tools/quic_transport_simple_server_session.h"

#include <memory>

#include "url/gurl.h"
#include "url/origin.h"
#include "net/third_party/quiche/src/quic/core/quic_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_protocol.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_stream.h"

namespace quic {

namespace {

// Discards any incoming data.
class DiscardVisitor : public QuicTransportStream::Visitor {
 public:
  DiscardVisitor(QuicTransportStream* stream) : stream_(stream) {}

  void OnCanRead() override {
    std::string buffer;
    size_t bytes_read = stream_->Read(&buffer);
    QUIC_DVLOG(2) << "Read " << bytes_read << " bytes from stream "
                  << stream_->id();
  }

  void OnFinRead() override {}
  void OnCanWrite() override {}

 private:
  QuicTransportStream* stream_;
};

// Echoes any incoming data back on the same stream.
class BidirectionalEchoVisitor : public QuicTransportStream::Visitor {
 public:
  BidirectionalEchoVisitor(QuicTransportStream* stream) : stream_(stream) {}

  void OnCanRead() override {
    stream_->Read(&buffer_);
    OnCanWrite();
  }

  void OnFinRead() override {
    bool success = stream_->SendFin();
    DCHECK(success);
  }

  void OnCanWrite() override {
    if (buffer_.empty()) {
      return;
    }

    bool success = stream_->Write(buffer_);
    if (success) {
      buffer_ = "";
    }
  }

 private:
  QuicTransportStream* stream_;
  std::string buffer_;
};

// Buffers all of the data and calls EchoStreamBack() on the parent session.
class UnidirectionalEchoReadVisitor : public QuicTransportStream::Visitor {
 public:
  UnidirectionalEchoReadVisitor(QuicTransportSimpleServerSession* session,
                                QuicTransportStream* stream)
      : session_(session), stream_(stream) {}

  void OnCanRead() override {
    bool success = stream_->Read(&buffer_);
    DCHECK(success);
  }

  void OnFinRead() override {
    QUIC_DVLOG(1) << "Finished receiving data on stream " << stream_->id()
                  << ", queueing up the echo";
    session_->EchoStreamBack(buffer_);
  }

  void OnCanWrite() override { QUIC_NOTREACHED(); }

 private:
  QuicTransportSimpleServerSession* session_;
  QuicTransportStream* stream_;
  std::string buffer_;
};

// Sends supplied data.
class UnidirectionalEchoWriteVisitor : public QuicTransportStream::Visitor {
 public:
  UnidirectionalEchoWriteVisitor(QuicTransportStream* stream,
                                 const std::string& data)
      : stream_(stream), data_(data) {}

  void OnCanRead() override { QUIC_NOTREACHED(); }
  void OnFinRead() override { QUIC_NOTREACHED(); }
  void OnCanWrite() override {
    if (data_.empty()) {
      return;
    }
    if (!stream_->Write(data_)) {
      return;
    }
    data_ = "";
    bool fin_sent = stream_->SendFin();
    DCHECK(fin_sent);
  }

 private:
  QuicTransportStream* stream_;
  std::string data_;
};

}  // namespace

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
      stream->set_visitor(std::make_unique<DiscardVisitor>(stream));
      break;

    case ECHO:
      switch (stream->type()) {
        case BIDIRECTIONAL:
          QUIC_DVLOG(1) << "Opening bidirectional echo stream " << stream->id();
          stream->set_visitor(
              std::make_unique<BidirectionalEchoVisitor>(stream));
          break;
        case READ_UNIDIRECTIONAL:
          QUIC_DVLOG(1)
              << "Started receiving data on unidirectional echo stream "
              << stream->id();
          stream->set_visitor(
              std::make_unique<UnidirectionalEchoReadVisitor>(this, stream));
          break;
        default:
          QUIC_NOTREACHED();
          break;
      }
      break;
  }
}

void QuicTransportSimpleServerSession::OnCanCreateNewOutgoingStream(
    bool unidirectional) {
  if (mode_ == ECHO && unidirectional) {
    MaybeEchoStreamsBack();
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

  QUIC_DLOG(WARNING) << "Unknown path requested: " << url.path();
  return false;
}

void QuicTransportSimpleServerSession::OnMessageReceived(
    quiche::QuicheStringPiece message) {
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

    stream->set_visitor(
        std::make_unique<UnidirectionalEchoWriteVisitor>(stream, data));
    stream->visitor()->OnCanWrite();
  }
}

}  // namespace quic

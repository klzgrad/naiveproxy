// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_WEB_TRANSPORT_TEST_VISITORS_H_
#define QUICHE_QUIC_TOOLS_WEB_TRANSPORT_TEST_VISITORS_H_

#include <string>

#include "absl/status/status.h"
#include "quiche/quic/core/web_transport_interface.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/common/quiche_stream.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/web_transport/complete_buffer_visitor.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

// Discards any incoming data.
class WebTransportDiscardVisitor : public WebTransportStreamVisitor {
 public:
  WebTransportDiscardVisitor(WebTransportStream* stream) : stream_(stream) {}

  void OnCanRead() override {
    std::string buffer;
    WebTransportStream::ReadResult result = stream_->Read(&buffer);
    QUIC_DVLOG(2) << "Read " << result.bytes_read
                  << " bytes from WebTransport stream "
                  << stream_->GetStreamId() << ", fin: " << result.fin;
  }

  void OnCanWrite() override {}

  void OnResetStreamReceived(WebTransportStreamError /*error*/) override {}
  void OnStopSendingReceived(WebTransportStreamError /*error*/) override {}
  void OnWriteSideInDataRecvdState() override {}

 private:
  WebTransportStream* stream_;
};

class DiscardWebTransportSessionVisitor : public WebTransportVisitor {
 public:
  DiscardWebTransportSessionVisitor(WebTransportSession* session)
      : session_(session) {}

  void OnSessionReady() override {}
  void OnSessionClosed(WebTransportSessionError /*error_code*/,
                       const std::string& /*error_message*/) override {}

  void OnIncomingBidirectionalStreamAvailable() override {
    while (true) {
      WebTransportStream* stream =
          session_->AcceptIncomingBidirectionalStream();
      if (stream == nullptr) {
        return;
      }
      stream->SetVisitor(std::make_unique<WebTransportDiscardVisitor>(stream));
      stream->visitor()->OnCanRead();
    }
  }

  void OnIncomingUnidirectionalStreamAvailable() override {
    while (true) {
      WebTransportStream* stream =
          session_->AcceptIncomingUnidirectionalStream();
      if (stream == nullptr) {
        return;
      }
      stream->SetVisitor(std::make_unique<WebTransportDiscardVisitor>(stream));
      stream->visitor()->OnCanRead();
    }
  }

  void OnDatagramReceived(absl::string_view) override {}
  void OnCanCreateNewOutgoingBidirectionalStream() override {}
  void OnCanCreateNewOutgoingUnidirectionalStream() override {}

 private:
  webtransport::Session* session_;
};

// Echoes any incoming data back on the same stream.
class WebTransportBidirectionalEchoVisitor : public WebTransportStreamVisitor {
 public:
  WebTransportBidirectionalEchoVisitor(WebTransportStream* stream)
      : stream_(stream) {}

  void OnCanRead() override {
    WebTransportStream::ReadResult result = stream_->Read(&buffer_);
    QUIC_DVLOG(1) << "Attempted reading on WebTransport bidirectional stream "
                  << stream_->GetStreamId()
                  << ", bytes read: " << result.bytes_read;
    if (result.fin) {
      send_fin_ = true;
    }
    OnCanWrite();
  }

  void OnCanWrite() override {
    if (stop_sending_received_) {
      return;
    }

    if (!buffer_.empty()) {
      absl::Status status = quiche::WriteIntoStream(*stream_, buffer_);
      QUIC_DVLOG(1) << "Attempted writing on WebTransport bidirectional stream "
                    << stream_->GetStreamId() << ", success: " << status;
      if (!status.ok()) {
        return;
      }

      buffer_ = "";
    }

    if (send_fin_ && !fin_sent_) {
      absl::Status status = quiche::SendFinOnStream(*stream_);
      if (status.ok()) {
        fin_sent_ = true;
      }
    }
  }

  void OnResetStreamReceived(WebTransportStreamError /*error*/) override {
    // Send FIN in response to a stream reset.  We want to test that we can
    // operate one side of the stream cleanly while the other is reset, thus
    // replying with a FIN rather than a RESET_STREAM is more appropriate here.
    send_fin_ = true;
    OnCanWrite();
  }
  void OnStopSendingReceived(WebTransportStreamError /*error*/) override {
    stop_sending_received_ = true;
  }
  void OnWriteSideInDataRecvdState() override {}

 protected:
  WebTransportStream* stream() { return stream_; }

 private:
  WebTransportStream* stream_;
  std::string buffer_;
  bool send_fin_ = false;
  bool fin_sent_ = false;
  bool stop_sending_received_ = false;
};

using WebTransportUnidirectionalEchoReadVisitor =
    ::webtransport::CompleteBufferVisitor;
using WebTransportUnidirectionalEchoWriteVisitor =
    ::webtransport::CompleteBufferVisitor;

// A session visitor which sets unidirectional or bidirectional stream visitors
// to echo.
class EchoWebTransportSessionVisitor : public WebTransportVisitor {
 public:
  EchoWebTransportSessionVisitor(WebTransportSession* session,
                                 bool open_server_initiated_echo_stream = true)
      : session_(session),
        echo_stream_opened_(!open_server_initiated_echo_stream) {}

  void OnSessionReady() override {
    if (session_->CanOpenNextOutgoingBidirectionalStream()) {
      OnCanCreateNewOutgoingBidirectionalStream();
    }
  }

  void OnSessionClosed(WebTransportSessionError /*error_code*/,
                       const std::string& /*error_message*/) override {}

  void OnIncomingBidirectionalStreamAvailable() override {
    while (true) {
      WebTransportStream* stream =
          session_->AcceptIncomingBidirectionalStream();
      if (stream == nullptr) {
        return;
      }
      QUIC_DVLOG(1)
          << "EchoWebTransportSessionVisitor received a bidirectional stream "
          << stream->GetStreamId();
      stream->SetVisitor(
          std::make_unique<WebTransportBidirectionalEchoVisitor>(stream));
      stream->visitor()->OnCanRead();
    }
  }

  void OnIncomingUnidirectionalStreamAvailable() override {
    while (true) {
      WebTransportStream* stream =
          session_->AcceptIncomingUnidirectionalStream();
      if (stream == nullptr) {
        return;
      }
      QUIC_DVLOG(1)
          << "EchoWebTransportSessionVisitor received a unidirectional stream";
      stream->SetVisitor(
          std::make_unique<WebTransportUnidirectionalEchoReadVisitor>(
              stream, [this](const std::string& data) {
                streams_to_echo_back_.push_back(data);
                TrySendingUnidirectionalStreams();
              }));
      stream->visitor()->OnCanRead();
    }
  }

  void OnDatagramReceived(absl::string_view datagram) override {
    session_->SendOrQueueDatagram(datagram);
  }

  void OnCanCreateNewOutgoingBidirectionalStream() override {
    if (!echo_stream_opened_) {
      WebTransportStream* stream = session_->OpenOutgoingBidirectionalStream();
      stream->SetVisitor(
          std::make_unique<WebTransportBidirectionalEchoVisitor>(stream));
      echo_stream_opened_ = true;
    }
  }
  void OnCanCreateNewOutgoingUnidirectionalStream() override {
    TrySendingUnidirectionalStreams();
  }

  void TrySendingUnidirectionalStreams() {
    while (!streams_to_echo_back_.empty() &&
           session_->CanOpenNextOutgoingUnidirectionalStream()) {
      QUIC_DVLOG(1)
          << "EchoWebTransportServer echoed a unidirectional stream back";
      WebTransportStream* stream = session_->OpenOutgoingUnidirectionalStream();
      stream->SetVisitor(
          std::make_unique<WebTransportUnidirectionalEchoWriteVisitor>(
              stream, streams_to_echo_back_.front()));
      streams_to_echo_back_.pop_front();
      stream->visitor()->OnCanWrite();
    }
  }

 private:
  WebTransportSession* session_;
  quiche::SimpleBufferAllocator allocator_;
  bool echo_stream_opened_;

  quiche::QuicheCircularDeque<std::string> streams_to_echo_back_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_WEB_TRANSPORT_TEST_VISITORS_H_

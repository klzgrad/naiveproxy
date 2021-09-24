// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_WEB_TRANSPORT_TEST_VISITORS_H_
#define QUICHE_QUIC_TOOLS_WEB_TRANSPORT_TEST_VISITORS_H_

#include <string>

#include "quic/core/quic_simple_buffer_allocator.h"
#include "quic/core/web_transport_interface.h"
#include "quic/platform/api/quic_logging.h"
#include "common/quiche_circular_deque.h"

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

 private:
  WebTransportStream* stream_;
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
    if (!buffer_.empty()) {
      bool success = stream_->Write(buffer_);
      QUIC_DVLOG(1) << "Attempted writing on WebTransport bidirectional stream "
                    << stream_->GetStreamId()
                    << ", success: " << (success ? "yes" : "no");
      if (!success) {
        return;
      }

      buffer_ = "";
    }

    if (send_fin_) {
      bool success = stream_->SendFin();
      QUICHE_DCHECK(success);
    }
  }

 private:
  WebTransportStream* stream_;
  std::string buffer_;
  bool send_fin_ = false;
};

// Buffers all of the data and calls |callback| with the entirety of the stream
// data.
class WebTransportUnidirectionalEchoReadVisitor
    : public WebTransportStreamVisitor {
 public:
  using Callback = std::function<void(const std::string&)>;

  WebTransportUnidirectionalEchoReadVisitor(WebTransportStream* stream,
                                            Callback callback)
      : stream_(stream), callback_(std::move(callback)) {}

  void OnCanRead() override {
    WebTransportStream::ReadResult result = stream_->Read(&buffer_);
    QUIC_DVLOG(1) << "Attempted reading on WebTransport unidirectional stream "
                  << stream_->GetStreamId()
                  << ", bytes read: " << result.bytes_read;
    if (result.fin) {
      QUIC_DVLOG(1) << "Finished receiving data on a WebTransport stream "
                    << stream_->GetStreamId() << ", queueing up the echo";
      callback_(buffer_);
    }
  }

  void OnCanWrite() override { QUIC_NOTREACHED(); }

 private:
  WebTransportStream* stream_;
  std::string buffer_;
  Callback callback_;
};

// Sends supplied data.
class WebTransportUnidirectionalEchoWriteVisitor
    : public WebTransportStreamVisitor {
 public:
  WebTransportUnidirectionalEchoWriteVisitor(WebTransportStream* stream,
                                             const std::string& data)
      : stream_(stream), data_(data) {}

  void OnCanRead() override { QUIC_NOTREACHED(); }
  void OnCanWrite() override {
    if (data_.empty()) {
      return;
    }
    if (!stream_->Write(data_)) {
      return;
    }
    data_ = "";
    bool fin_sent = stream_->SendFin();
    QUICHE_DVLOG(1)
        << "WebTransportUnidirectionalEchoWriteVisitor finished sending data.";
    QUICHE_DCHECK(fin_sent);
  }

 private:
  WebTransportStream* stream_;
  std::string data_;
};

// A session visitor which sets unidirectional or bidirectional stream visitors
// to echo.
class EchoWebTransportSessionVisitor : public WebTransportVisitor {
 public:
  EchoWebTransportSessionVisitor(WebTransportSession* session)
      : session_(session) {}

  void OnSessionReady() override {
    if (session_->CanOpenNextOutgoingBidirectionalStream()) {
      OnCanCreateNewOutgoingBidirectionalStream();
    }
  }

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
    auto buffer = MakeUniqueBuffer(&allocator_, datagram.size());
    memcpy(buffer.get(), datagram.data(), datagram.size());
    QuicMemSlice slice(std::move(buffer), datagram.size());
    session_->SendOrQueueDatagram(std::move(slice));
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
  SimpleBufferAllocator allocator_;
  bool echo_stream_opened_ = false;

  quiche::QuicheCircularDeque<std::string> streams_to_echo_back_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_WEB_TRANSPORT_TEST_VISITORS_H_

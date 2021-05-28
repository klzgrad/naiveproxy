// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/test_tools/quic_test_backend.h"

#include <cstring>
#include <memory>

#include "absl/strings/string_view.h"
#include "quic/core/quic_buffer_allocator.h"
#include "quic/core/quic_circular_deque.h"
#include "quic/core/quic_simple_buffer_allocator.h"
#include "quic/core/web_transport_interface.h"
#include "quic/platform/api/quic_mem_slice.h"
#include "quic/tools/web_transport_test_visitors.h"

namespace quic {
namespace test {

namespace {

class EchoWebTransportServer : public WebTransportVisitor {
 public:
  EchoWebTransportServer(WebTransportSession* session) : session_(session) {}

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
      QUIC_DVLOG(1) << "EchoWebTransportServer received a bidirectional stream "
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
          << "EchoWebTransportServer received a unidirectional stream";
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

  QuicCircularDeque<std::string> streams_to_echo_back_;
};

}  // namespace

QuicSimpleServerBackend::WebTransportResponse
QuicTestBackend::ProcessWebTransportRequest(
    const spdy::Http2HeaderBlock& request_headers,
    WebTransportSession* session) {
  if (!SupportsWebTransport()) {
    return QuicSimpleServerBackend::ProcessWebTransportRequest(request_headers,
                                                               session);
  }

  auto path_it = request_headers.find(":path");
  if (path_it == request_headers.end()) {
    WebTransportResponse response;
    response.response_headers[":status"] = "400";
    return response;
  }
  absl::string_view path = path_it->second;
  if (path == "/echo") {
    WebTransportResponse response;
    response.response_headers[":status"] = "200";
    response.visitor = std::make_unique<EchoWebTransportServer>(session);
    return response;
  }

  WebTransportResponse response;
  response.response_headers[":status"] = "404";
  return response;
}

}  // namespace test
}  // namespace quic

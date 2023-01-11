// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/web_transport_resets_backend.h"

#include <memory>

#include "quiche/quic/core/web_transport_interface.h"
#include "quiche/quic/tools/web_transport_test_visitors.h"
#include "quiche/common/quiche_circular_deque.h"

namespace quic {
namespace test {

namespace {

class ResetsVisitor;

class BidirectionalEchoVisitorWithLogging
    : public WebTransportBidirectionalEchoVisitor {
 public:
  BidirectionalEchoVisitorWithLogging(WebTransportStream* stream,
                                      ResetsVisitor* session_visitor)
      : WebTransportBidirectionalEchoVisitor(stream),
        session_visitor_(session_visitor) {}

  void OnResetStreamReceived(WebTransportStreamError error) override;
  void OnStopSendingReceived(WebTransportStreamError error) override;

 private:
  ResetsVisitor* session_visitor_;  // Not owned.
};

class ResetsVisitor : public WebTransportVisitor {
 public:
  ResetsVisitor(WebTransportSession* session) : session_(session) {}

  void OnSessionReady(const spdy::Http2HeaderBlock& /*headers*/) override {}
  void OnSessionClosed(WebTransportSessionError /*error_code*/,
                       const std::string& /*error_message*/) override {}

  void OnIncomingBidirectionalStreamAvailable() override {
    while (true) {
      WebTransportStream* stream =
          session_->AcceptIncomingBidirectionalStream();
      if (stream == nullptr) {
        return;
      }
      stream->SetVisitor(
          std::make_unique<BidirectionalEchoVisitorWithLogging>(stream, this));
      stream->visitor()->OnCanRead();
    }
  }
  void OnIncomingUnidirectionalStreamAvailable() override {}

  void OnDatagramReceived(absl::string_view /*datagram*/) override {}

  void OnCanCreateNewOutgoingBidirectionalStream() override {}
  void OnCanCreateNewOutgoingUnidirectionalStream() override {
    MaybeSendLogsBack();
  }

  void Log(std::string line) {
    log_.push_back(std::move(line));
    MaybeSendLogsBack();
  }

 private:
  void MaybeSendLogsBack() {
    while (!log_.empty() &&
           session_->CanOpenNextOutgoingUnidirectionalStream()) {
      WebTransportStream* stream = session_->OpenOutgoingUnidirectionalStream();
      stream->SetVisitor(
          std::make_unique<WebTransportUnidirectionalEchoWriteVisitor>(
              stream, log_.front()));
      log_.pop_front();
      stream->visitor()->OnCanWrite();
    }
  }

  WebTransportSession* session_;  // Not owned.
  quiche::QuicheCircularDeque<std::string> log_;
};

void BidirectionalEchoVisitorWithLogging::OnResetStreamReceived(
    WebTransportStreamError error) {
  session_visitor_->Log(absl::StrCat("Received reset for stream ",
                                     stream()->GetStreamId(),
                                     " with error code ", error));
  WebTransportBidirectionalEchoVisitor::OnResetStreamReceived(error);
}
void BidirectionalEchoVisitorWithLogging::OnStopSendingReceived(
    WebTransportStreamError error) {
  session_visitor_->Log(absl::StrCat("Received stop sending for stream ",
                                     stream()->GetStreamId(),
                                     " with error code ", error));
  WebTransportBidirectionalEchoVisitor::OnStopSendingReceived(error);
}

}  // namespace

QuicSimpleServerBackend::WebTransportResponse WebTransportResetsBackend(
    const spdy::Http2HeaderBlock& /*request_headers*/,
    WebTransportSession* session) {
  QuicSimpleServerBackend::WebTransportResponse response;
  response.response_headers[":status"] = "200";
  response.visitor = std::make_unique<ResetsVisitor>(session);
  return response;
}

}  // namespace test
}  // namespace quic

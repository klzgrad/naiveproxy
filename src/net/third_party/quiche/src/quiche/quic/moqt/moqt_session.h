// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_SESSION_H_
#define QUICHE_QUIC_MOQT_MOQT_SESSION_H_

#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/simple_buffer_allocator.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

using MoqtSessionEstablishedCallback = quiche::SingleUseCallback<void()>;
using MoqtSessionTerminatedCallback =
    quiche::SingleUseCallback<void(absl::string_view error_message)>;

class QUICHE_EXPORT MoqtSession : public webtransport::SessionVisitor {
 public:
  MoqtSession(webtransport::Session* session, MoqtSessionParameters parameters,
              MoqtSessionEstablishedCallback session_established_callback,
              MoqtSessionTerminatedCallback session_terminated_callback)
      : session_(session),
        parameters_(parameters),
        session_established_callback_(std::move(session_established_callback)),
        session_terminated_callback_(std::move(session_terminated_callback)),
        framer_(quiche::SimpleBufferAllocator::Get(), parameters.perspective,
                parameters.using_webtrans) {}

  // webtransport::SessionVisitor implementation.
  void OnSessionReady() override;
  void OnSessionClosed(webtransport::SessionErrorCode,
                       const std::string&) override;
  void OnIncomingBidirectionalStreamAvailable() override;
  void OnIncomingUnidirectionalStreamAvailable() override;
  void OnDatagramReceived(absl::string_view datagram) override {}
  void OnCanCreateNewOutgoingBidirectionalStream() override {}
  void OnCanCreateNewOutgoingUnidirectionalStream() override {}

  void Error(absl::string_view error);

  quic::Perspective perspective() const { return parameters_.perspective; }

 private:
  class QUICHE_EXPORT Stream : public webtransport::StreamVisitor,
                               public MoqtParserVisitor {
   public:
    Stream(MoqtSession* session, webtransport::Stream* stream)
        : session_(session),
          stream_(stream),
          parser_(session->parameters_.perspective,
                  session->parameters_.using_webtrans, *this) {}
    Stream(MoqtSession* session, webtransport::Stream* stream,
           bool is_control_stream)
        : session_(session),
          stream_(stream),
          parser_(session->parameters_.perspective,
                  session->parameters_.using_webtrans, *this),
          is_control_stream_(is_control_stream) {}

    // webtransport::StreamVisitor implementation.
    void OnCanRead() override;
    void OnCanWrite() override;
    void OnResetStreamReceived(webtransport::StreamErrorCode error) override;
    void OnStopSendingReceived(webtransport::StreamErrorCode error) override;
    void OnWriteSideInDataRecvdState() override {}

    // MoqtParserVisitor implementation.
    void OnObjectMessage(const MoqtObject& message, absl::string_view payload,
                         bool end_of_message) override {}
    void OnSetupMessage(const MoqtSetup& message) override;
    void OnSubscribeRequestMessage(
        const MoqtSubscribeRequest& message) override {}
    void OnSubscribeOkMessage(const MoqtSubscribeOk& message) override {}
    void OnSubscribeErrorMessage(const MoqtSubscribeError& message) override {}
    void OnUnsubscribeMessage(const MoqtUnsubscribe& message) override {}
    void OnAnnounceMessage(const MoqtAnnounce& message) override {}
    void OnAnnounceOkMessage(const MoqtAnnounceOk& message) override {}
    void OnAnnounceErrorMessage(const MoqtAnnounceError& message) override {}
    void OnUnannounceMessage(const MoqtUnannounce& message) override {}
    void OnGoAwayMessage() override {}
    void OnParsingError(absl::string_view reason) override;

    quic::Perspective perspective() const {
      return session_->parameters_.perspective;
    }

   private:
    MoqtSession* session_;
    webtransport::Stream* stream_;
    MoqtParser parser_;
    // nullopt means "incoming stream, and we don't know if it's the control
    // stream or a data stream yet".
    absl::optional<bool> is_control_stream_;
  };

  webtransport::Session* session_;
  MoqtSessionParameters parameters_;
  MoqtSessionEstablishedCallback session_established_callback_;
  MoqtSessionTerminatedCallback session_terminated_callback_;
  MoqtFramer framer_;

  absl::optional<webtransport::StreamId> control_stream_;
  std::string error_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SESSION_H_

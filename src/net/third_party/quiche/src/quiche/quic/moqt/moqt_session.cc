// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_session.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_stream.h"
#include "quiche/web_transport/web_transport.h"

#define ENDPOINT \
  (perspective() == Perspective::IS_SERVER ? "MoQT Server: " : "MoQT Client: ")

namespace moqt {

using ::quic::Perspective;

void MoqtSession::OnSessionReady() {
  QUICHE_DLOG(INFO) << ENDPOINT << "Underlying session ready";
  if (parameters_.perspective == Perspective::IS_SERVER) {
    return;
  }

  webtransport::Stream* control_stream =
      session_->OpenOutgoingBidirectionalStream();
  if (control_stream == nullptr) {
    Error("Unable to open a control stream");
    return;
  }
  control_stream->SetVisitor(std::make_unique<Stream>(
      this, control_stream, /*is_control_stream=*/true));
  control_stream_ = control_stream->GetStreamId();
  MoqtSetup setup = MoqtSetup{
      .supported_versions = std::vector<MoqtVersion>{parameters_.version},
      .role = MoqtRole::kBoth};
  if (!parameters_.using_webtrans) {
    setup.path = parameters_.path;
  }
  quiche::QuicheBuffer serialized_setup = framer_.SerializeSetup(setup);
  bool success = control_stream->Write(serialized_setup.AsStringView());
  if (!success) {
    Error("Failed to write client SETUP message");
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT << "Send the SETUP message";
}

void MoqtSession::OnIncomingBidirectionalStreamAvailable() {
  while (webtransport::Stream* stream =
             session_->AcceptIncomingBidirectionalStream()) {
    stream->SetVisitor(std::make_unique<Stream>(this, stream));
    stream->visitor()->OnCanRead();
  }
}
void MoqtSession::OnIncomingUnidirectionalStreamAvailable() {
  while (webtransport::Stream* stream =
             session_->AcceptIncomingUnidirectionalStream()) {
    stream->SetVisitor(std::make_unique<Stream>(this, stream));
    stream->visitor()->OnCanRead();
  }
}

void MoqtSession::OnSessionClosed(webtransport::SessionErrorCode,
                                  const std::string& error_message) {
  if (!error_.empty()) {
    // Avoid erroring out twice.
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT << "Underlying session closed with message: "
                    << error_message;
  error_ = error_message;
  std::move(session_terminated_callback_)(error_message);
}

void MoqtSession::Error(absl::string_view error) {
  if (!error_.empty()) {
    // Avoid erroring out twice.
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT
                    << "MOQT session closed with message: " << error;
  error_ = std::string(error);
  // TODO(vasilvv): figure out the error code.
  session_->CloseSession(1, error);
  std::move(session_terminated_callback_)(error);
}

void MoqtSession::Stream::OnCanRead() {
  bool fin =
      quiche::ProcessAllReadableRegions(*stream_, [&](absl::string_view chunk) {
        parser_.ProcessData(chunk, /*end_of_stream=*/false);
      });
  if (fin) {
    parser_.ProcessData("", /*end_of_stream=*/true);
  }
}
void MoqtSession::Stream::OnCanWrite() {}
void MoqtSession::Stream::OnResetStreamReceived(
    webtransport::StreamErrorCode error) {
  if (is_control_stream_.has_value() && *is_control_stream_) {
    session_->Error(
        absl::StrCat("Control stream reset with error code ", error));
  }
}
void MoqtSession::Stream::OnStopSendingReceived(
    webtransport::StreamErrorCode error) {
  if (is_control_stream_.has_value() && *is_control_stream_) {
    session_->Error(
        absl::StrCat("Control stream reset with error code ", error));
  }
}

void MoqtSession::Stream::OnSetupMessage(const MoqtSetup& message) {
  if (is_control_stream_.has_value()) {
    if (!*is_control_stream_) {
      session_->Error("Received SETUP on non-control stream");
      return;
    }
  } else {
    is_control_stream_ = true;
  }
  if (absl::c_find(message.supported_versions, session_->parameters_.version) ==
      message.supported_versions.end()) {
    session_->Error(absl::StrCat("Version mismatch: expected 0x",
                                 absl::Hex(session_->parameters_.version)));
    return;
  }
  QUICHE_DLOG(INFO) << ENDPOINT << "Received the SETUP message";
  if (session_->parameters_.perspective == Perspective::IS_SERVER) {
    MoqtSetup response =
        MoqtSetup{.supported_versions =
                      std::vector<MoqtVersion>{session_->parameters_.version},
                  .role = MoqtRole::kBoth};
    bool success = stream_->Write(
        session_->framer_.SerializeSetup(response).AsStringView());
    if (!success) {
      session_->Error("Failed to write server SETUP message");
      return;
    }
    QUICHE_DLOG(INFO) << ENDPOINT << "Sent the SETUP message";
  }
  // TODO: handle role and path.
  std::move(session_->session_established_callback_)();
}

void MoqtSession::Stream::OnParsingError(absl::string_view reason) {
  session_->Error(absl::StrCat("Parse error: ", reason));
}

}  // namespace moqt

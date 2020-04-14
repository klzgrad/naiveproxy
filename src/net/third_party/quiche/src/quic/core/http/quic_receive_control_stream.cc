// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_receive_control_stream.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/http/http_constants.h"
#include "net/third_party/quiche/src/quic/core/http/http_decoder.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_session.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {

// Visitor of HttpDecoder that passes data frame to QuicSpdyStream and closes
// the connection on unexpected frames.
class QuicReceiveControlStream::HttpDecoderVisitor
    : public HttpDecoder::Visitor {
 public:
  explicit HttpDecoderVisitor(QuicReceiveControlStream* stream)
      : stream_(stream) {}
  HttpDecoderVisitor(const HttpDecoderVisitor&) = delete;
  HttpDecoderVisitor& operator=(const HttpDecoderVisitor&) = delete;

  void OnError(HttpDecoder* decoder) override {
    stream_->session()->connection()->CloseConnection(
        decoder->error(), decoder->error_detail(),
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }

  bool OnCancelPushFrame(const CancelPushFrame& /*frame*/) override {
    CloseConnectionOnWrongFrame("Cancel Push");
    return false;
  }

  bool OnMaxPushIdFrame(const MaxPushIdFrame& frame) override {
    if (stream_->spdy_session()->perspective() == Perspective::IS_SERVER) {
      stream_->spdy_session()->SetMaxAllowedPushId(frame.push_id);
      return true;
    }
    CloseConnectionOnWrongFrame("Max Push Id");
    return false;
  }

  bool OnGoAwayFrame(const GoAwayFrame& frame) override {
    if (stream_->spdy_session()->perspective() == Perspective::IS_SERVER) {
      CloseConnectionOnWrongFrame("Go Away");
      return false;
    }
    stream_->spdy_session()->OnHttp3GoAway(frame.stream_id);
    return true;
  }

  bool OnSettingsFrameStart(QuicByteCount header_length) override {
    return stream_->OnSettingsFrameStart(header_length);
  }

  bool OnSettingsFrame(const SettingsFrame& frame) override {
    return stream_->OnSettingsFrame(frame);
  }

  bool OnDuplicatePushFrame(const DuplicatePushFrame& /*frame*/) override {
    CloseConnectionOnWrongFrame("Duplicate Push");
    return false;
  }

  bool OnDataFrameStart(QuicByteCount /*header_length*/) override {
    CloseConnectionOnWrongFrame("Data");
    return false;
  }

  bool OnDataFramePayload(quiche::QuicheStringPiece /*payload*/) override {
    CloseConnectionOnWrongFrame("Data");
    return false;
  }

  bool OnDataFrameEnd() override {
    CloseConnectionOnWrongFrame("Data");
    return false;
  }

  bool OnHeadersFrameStart(QuicByteCount /*header_length*/) override {
    CloseConnectionOnWrongFrame("Headers");
    return false;
  }

  bool OnHeadersFramePayload(quiche::QuicheStringPiece /*payload*/) override {
    CloseConnectionOnWrongFrame("Headers");
    return false;
  }

  bool OnHeadersFrameEnd() override {
    CloseConnectionOnWrongFrame("Headers");
    return false;
  }

  bool OnPushPromiseFrameStart(QuicByteCount /*header_length*/) override {
    CloseConnectionOnWrongFrame("Push Promise");
    return false;
  }

  bool OnPushPromiseFramePushId(PushId /*push_id*/,
                                QuicByteCount /*push_id_length*/) override {
    CloseConnectionOnWrongFrame("Push Promise");
    return false;
  }

  bool OnPushPromiseFramePayload(
      quiche::QuicheStringPiece /*payload*/) override {
    CloseConnectionOnWrongFrame("Push Promise");
    return false;
  }

  bool OnPushPromiseFrameEnd() override {
    CloseConnectionOnWrongFrame("Push Promise");
    return false;
  }

  bool OnPriorityUpdateFrameStart(QuicByteCount header_length) override {
    return stream_->OnPriorityUpdateFrameStart(header_length);
  }

  bool OnPriorityUpdateFrame(const PriorityUpdateFrame& frame) override {
    return stream_->OnPriorityUpdateFrame(frame);
  }

  bool OnUnknownFrameStart(uint64_t /* frame_type */,
                           QuicByteCount /* header_length */) override {
    // Ignore unknown frame types.
    return true;
  }

  bool OnUnknownFramePayload(quiche::QuicheStringPiece /* payload */) override {
    // Ignore unknown frame types.
    return true;
  }

  bool OnUnknownFrameEnd() override {
    // Ignore unknown frame types.
    return true;
  }

 private:
  void CloseConnectionOnWrongFrame(quiche::QuicheStringPiece frame_type) {
    stream_->session()->connection()->CloseConnection(
        QUIC_HTTP_FRAME_UNEXPECTED_ON_CONTROL_STREAM,
        quiche::QuicheStrCat(frame_type, " frame received on control stream"),
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }

  QuicReceiveControlStream* stream_;
};

QuicReceiveControlStream::QuicReceiveControlStream(
    PendingStream* pending,
    QuicSpdySession* spdy_session)
    : QuicStream(pending, READ_UNIDIRECTIONAL, /*is_static=*/true),
      settings_frame_received_(false),
      http_decoder_visitor_(std::make_unique<HttpDecoderVisitor>(this)),
      decoder_(http_decoder_visitor_.get()),
      spdy_session_(spdy_session) {
  sequencer()->set_level_triggered(true);
}

QuicReceiveControlStream::~QuicReceiveControlStream() {}

void QuicReceiveControlStream::OnStreamReset(
    const QuicRstStreamFrame& /*frame*/) {
  // TODO(renjietang) Change the error code to H/3 specific
  // HTTP_CLOSED_CRITICAL_STREAM.
  session()->connection()->CloseConnection(
      QUIC_INVALID_STREAM_ID, "Attempt to reset receive control stream",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicReceiveControlStream::OnDataAvailable() {
  iovec iov;
  while (!reading_stopped() && decoder_.error() == QUIC_NO_ERROR &&
         sequencer()->GetReadableRegion(&iov)) {
    DCHECK(!sequencer()->IsClosed());

    QuicByteCount processed_bytes = decoder_.ProcessInput(
        reinterpret_cast<const char*>(iov.iov_base), iov.iov_len);
    sequencer()->MarkConsumed(processed_bytes);

    if (!session()->connection()->connected()) {
      return;
    }

    // The only reason QuicReceiveControlStream pauses HttpDecoder is an error,
    // in which case the connection would have already been closed.
    DCHECK_EQ(iov.iov_len, processed_bytes);
  }
}

bool QuicReceiveControlStream::OnSettingsFrameStart(
    QuicByteCount /* header_length */) {
  if (settings_frame_received_) {
    // TODO(renjietang): Change error code to HTTP_UNEXPECTED_FRAME.
    session()->connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Settings frames are received twice.",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  settings_frame_received_ = true;

  return true;
}

bool QuicReceiveControlStream::OnSettingsFrame(const SettingsFrame& settings) {
  QUIC_DVLOG(1) << "Control Stream " << id()
                << " received settings frame: " << settings;
  if (spdy_session_->debug_visitor() != nullptr) {
    spdy_session_->debug_visitor()->OnSettingsFrameReceived(settings);
  }
  for (const auto& setting : settings.values) {
    spdy_session_->OnSetting(setting.first, setting.second);
  }
  return true;
}

bool QuicReceiveControlStream::OnPriorityUpdateFrameStart(
    QuicByteCount /* header_length */) {
  if (!settings_frame_received_) {
    session()->connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID,
        "PRIORITY_UPDATE frame received before SETTINGS.",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  return true;
}

bool QuicReceiveControlStream::OnPriorityUpdateFrame(
    const PriorityUpdateFrame& priority) {
  // TODO(b/147306124): Use a proper structured headers parser instead.
  for (auto key_value :
       quiche::QuicheTextUtils::Split(priority.priority_field_value, ',')) {
    auto key_and_value = quiche::QuicheTextUtils::Split(key_value, '=');
    if (key_and_value.size() != 2) {
      continue;
    }

    quiche::QuicheStringPiece key = key_and_value[0];
    quiche::QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&key);
    if (key != "u") {
      continue;
    }

    quiche::QuicheStringPiece value = key_and_value[1];
    int urgency;
    if (!quiche::QuicheTextUtils::StringToInt(value, &urgency) || urgency < 0 ||
        urgency > 7) {
      session()->connection()->CloseConnection(
          QUIC_INVALID_STREAM_ID,
          "Invalid value for PRIORITY_UPDATE urgency parameter.",
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return false;
    }

    if (priority.prioritized_element_type == REQUEST_STREAM) {
      return spdy_session_->OnPriorityUpdateForRequestStream(
          priority.prioritized_element_id, urgency);
    } else {
      return spdy_session_->OnPriorityUpdateForPushStream(
          priority.prioritized_element_id, urgency);
    }
  }

  // Ignore frame if no urgency parameter can be parsed.
  return true;
}

}  // namespace quic

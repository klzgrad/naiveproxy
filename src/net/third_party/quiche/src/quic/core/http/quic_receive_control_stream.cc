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

QuicReceiveControlStream::QuicReceiveControlStream(
    PendingStream* pending,
    QuicSpdySession* spdy_session)
    : QuicStream(pending, READ_UNIDIRECTIONAL, /*is_static=*/true),
      settings_frame_received_(false),
      decoder_(this),
      spdy_session_(spdy_session) {
  sequencer()->set_level_triggered(true);
}

QuicReceiveControlStream::~QuicReceiveControlStream() {}

void QuicReceiveControlStream::OnStreamReset(
    const QuicRstStreamFrame& /*frame*/) {
  stream_delegate()->OnStreamError(
      QUIC_HTTP_CLOSED_CRITICAL_STREAM,
      "RESET_STREAM received for receive control stream");
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

void QuicReceiveControlStream::OnError(HttpDecoder* decoder) {
  OnUnrecoverableError(decoder->error(), decoder->error_detail());
}

bool QuicReceiveControlStream::OnCancelPushFrame(const CancelPushFrame& frame) {
  if (spdy_session()->debug_visitor()) {
    spdy_session()->debug_visitor()->OnCancelPushFrameReceived(frame);
  }

  if (!settings_frame_received_) {
    stream_delegate()->OnStreamError(
        QUIC_HTTP_MISSING_SETTINGS_FRAME,
        "CANCEL_PUSH frame received before SETTINGS.");
    return false;
  }

  // TODO(b/151841240): Handle CANCEL_PUSH frames instead of ignoring them.
  return true;
}

bool QuicReceiveControlStream::OnMaxPushIdFrame(const MaxPushIdFrame& frame) {
  if (spdy_session()->debug_visitor()) {
    spdy_session()->debug_visitor()->OnMaxPushIdFrameReceived(frame);
  }

  if (!settings_frame_received_) {
    stream_delegate()->OnStreamError(
        QUIC_HTTP_MISSING_SETTINGS_FRAME,
        "MAX_PUSH_ID frame received before SETTINGS.");
    return false;
  }

  if (spdy_session()->perspective() == Perspective::IS_CLIENT) {
    OnWrongFrame("Max Push Id");
    return false;
  }

  // TODO(b/124216424): Signal error if received push ID is smaller than a
  // previously received value.
  spdy_session()->OnMaxPushIdFrame(frame.push_id);
  return true;
}

bool QuicReceiveControlStream::OnGoAwayFrame(const GoAwayFrame& frame) {
  if (spdy_session()->debug_visitor()) {
    spdy_session()->debug_visitor()->OnGoAwayFrameReceived(frame);
  }

  if (!settings_frame_received_) {
    stream_delegate()->OnStreamError(QUIC_HTTP_MISSING_SETTINGS_FRAME,
                                     "GOAWAY frame received before SETTINGS.");
    return false;
  }

  if (spdy_session()->perspective() == Perspective::IS_SERVER) {
    OnWrongFrame("Go Away");
    return false;
  }

  spdy_session()->OnHttp3GoAway(frame.stream_id);
  return true;
}

bool QuicReceiveControlStream::OnSettingsFrameStart(
    QuicByteCount /*header_length*/) {
  if (settings_frame_received_) {
    stream_delegate()->OnStreamError(
        QUIC_HTTP_INVALID_FRAME_SEQUENCE_ON_CONTROL_STREAM,
        "Settings frames are received twice.");
    return false;
  }

  settings_frame_received_ = true;

  return true;
}

bool QuicReceiveControlStream::OnSettingsFrame(const SettingsFrame& frame) {
  QUIC_DVLOG(1) << "Control Stream " << id()
                << " received settings frame: " << frame;
  if (spdy_session_->debug_visitor() != nullptr) {
    spdy_session_->debug_visitor()->OnSettingsFrameReceived(frame);
  }
  for (const auto& setting : frame.values) {
    spdy_session_->OnSetting(setting.first, setting.second);
  }
  return true;
}

bool QuicReceiveControlStream::OnDataFrameStart(QuicByteCount /*header_length*/,
                                                QuicByteCount
                                                /*payload_length*/) {
  OnWrongFrame("Data");
  return false;
}

bool QuicReceiveControlStream::OnDataFramePayload(
    quiche::QuicheStringPiece /*payload*/) {
  OnWrongFrame("Data");
  return false;
}

bool QuicReceiveControlStream::OnDataFrameEnd() {
  OnWrongFrame("Data");
  return false;
}

bool QuicReceiveControlStream::OnHeadersFrameStart(
    QuicByteCount /*header_length*/,
    QuicByteCount
    /*payload_length*/) {
  OnWrongFrame("Headers");
  return false;
}

bool QuicReceiveControlStream::OnHeadersFramePayload(
    quiche::QuicheStringPiece /*payload*/) {
  OnWrongFrame("Headers");
  return false;
}

bool QuicReceiveControlStream::OnHeadersFrameEnd() {
  OnWrongFrame("Headers");
  return false;
}

bool QuicReceiveControlStream::OnPushPromiseFrameStart(
    QuicByteCount /*header_length*/) {
  OnWrongFrame("Push Promise");
  return false;
}

bool QuicReceiveControlStream::OnPushPromiseFramePushId(
    PushId /*push_id*/,
    QuicByteCount /*push_id_length*/,
    QuicByteCount /*header_block_length*/) {
  OnWrongFrame("Push Promise");
  return false;
}

bool QuicReceiveControlStream::OnPushPromiseFramePayload(
    quiche::QuicheStringPiece /*payload*/) {
  OnWrongFrame("Push Promise");
  return false;
}

bool QuicReceiveControlStream::OnPushPromiseFrameEnd() {
  OnWrongFrame("Push Promise");
  return false;
}

bool QuicReceiveControlStream::OnPriorityUpdateFrameStart(
    QuicByteCount /*header_length*/) {
  if (!settings_frame_received_) {
    stream_delegate()->OnStreamError(
        QUIC_HTTP_MISSING_SETTINGS_FRAME,
        "PRIORITY_UPDATE frame received before SETTINGS.");
    return false;
  }
  return true;
}

bool QuicReceiveControlStream::OnPriorityUpdateFrame(
    const PriorityUpdateFrame& frame) {
  if (spdy_session()->debug_visitor()) {
    spdy_session()->debug_visitor()->OnPriorityUpdateFrameReceived(frame);
  }

  // TODO(b/147306124): Use a proper structured headers parser instead.
  for (auto key_value :
       quiche::QuicheTextUtils::Split(frame.priority_field_value, ',')) {
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
      stream_delegate()->OnStreamError(
          QUIC_INVALID_STREAM_ID,
          "Invalid value for PRIORITY_UPDATE urgency parameter.");
      return false;
    }

    if (frame.prioritized_element_type == REQUEST_STREAM) {
      return spdy_session_->OnPriorityUpdateForRequestStream(
          frame.prioritized_element_id, urgency);
    } else {
      return spdy_session_->OnPriorityUpdateForPushStream(
          frame.prioritized_element_id, urgency);
    }
  }

  // Ignore frame if no urgency parameter can be parsed.
  return true;
}

bool QuicReceiveControlStream::OnUnknownFrameStart(
    uint64_t frame_type,
    QuicByteCount /*header_length*/,
    QuicByteCount payload_length) {
  if (spdy_session()->debug_visitor()) {
    spdy_session()->debug_visitor()->OnUnknownFrameReceived(id(), frame_type,
                                                            payload_length);
  }

  if (!settings_frame_received_) {
    stream_delegate()->OnStreamError(QUIC_HTTP_MISSING_SETTINGS_FRAME,
                                     "Unknown frame received before SETTINGS.");
    return false;
  }

  return true;
}

bool QuicReceiveControlStream::OnUnknownFramePayload(
    quiche::QuicheStringPiece /*payload*/) {
  // Ignore unknown frame types.
  return true;
}

bool QuicReceiveControlStream::OnUnknownFrameEnd() {
  // Ignore unknown frame types.
  return true;
}

void QuicReceiveControlStream::OnWrongFrame(
    quiche::QuicheStringPiece frame_type) {
  OnUnrecoverableError(
      QUIC_HTTP_FRAME_UNEXPECTED_ON_CONTROL_STREAM,
      quiche::QuicheStrCat(frame_type, " frame received on control stream"));
}

}  // namespace quic

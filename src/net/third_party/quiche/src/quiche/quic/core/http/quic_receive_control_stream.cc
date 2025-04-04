// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/quic_receive_control_stream.h"

#include <optional>
#include <utility>

#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/http_constants.h"
#include "quiche/quic/core/http/http_decoder.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/quic_stream_priority.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/quiche_text_utils.h"

namespace quic {

QuicReceiveControlStream::QuicReceiveControlStream(
    PendingStream* pending, QuicSpdySession* spdy_session)
    : QuicStream(pending, spdy_session,
                 /*is_static=*/true),
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
    QUICHE_DCHECK(!sequencer()->IsClosed());

    QuicByteCount processed_bytes = decoder_.ProcessInput(
        reinterpret_cast<const char*>(iov.iov_base), iov.iov_len);
    sequencer()->MarkConsumed(processed_bytes);

    if (!session()->connection()->connected()) {
      return;
    }

    // The only reason QuicReceiveControlStream pauses HttpDecoder is an error,
    // in which case the connection would have already been closed.
    QUICHE_DCHECK_EQ(iov.iov_len, processed_bytes);
  }
}

void QuicReceiveControlStream::OnError(HttpDecoder* decoder) {
  stream_delegate()->OnStreamError(decoder->error(), decoder->error_detail());
}

bool QuicReceiveControlStream::OnMaxPushIdFrame() {
  return ValidateFrameType(HttpFrameType::MAX_PUSH_ID);
}

bool QuicReceiveControlStream::OnGoAwayFrame(const GoAwayFrame& frame) {
  if (spdy_session()->debug_visitor()) {
    spdy_session()->debug_visitor()->OnGoAwayFrameReceived(frame);
  }

  if (!ValidateFrameType(HttpFrameType::GOAWAY)) {
    return false;
  }

  spdy_session()->OnHttp3GoAway(frame.id);
  return true;
}

bool QuicReceiveControlStream::OnSettingsFrameStart(
    QuicByteCount /*header_length*/) {
  return ValidateFrameType(HttpFrameType::SETTINGS);
}

bool QuicReceiveControlStream::OnSettingsFrame(const SettingsFrame& frame) {
  QUIC_DVLOG(1) << "Control Stream " << id()
                << " received settings frame: " << frame;
  return spdy_session_->OnSettingsFrame(frame);
}

bool QuicReceiveControlStream::OnDataFrameStart(QuicByteCount /*header_length*/,
                                                QuicByteCount
                                                /*payload_length*/) {
  return ValidateFrameType(HttpFrameType::DATA);
}

bool QuicReceiveControlStream::OnDataFramePayload(
    absl::string_view /*payload*/) {
  QUICHE_NOTREACHED();
  return false;
}

bool QuicReceiveControlStream::OnDataFrameEnd() {
  QUICHE_NOTREACHED();
  return false;
}

bool QuicReceiveControlStream::OnHeadersFrameStart(
    QuicByteCount /*header_length*/, QuicByteCount
    /*payload_length*/) {
  return ValidateFrameType(HttpFrameType::HEADERS);
}

bool QuicReceiveControlStream::OnHeadersFramePayload(
    absl::string_view /*payload*/) {
  QUICHE_NOTREACHED();
  return false;
}

bool QuicReceiveControlStream::OnHeadersFrameEnd() {
  QUICHE_NOTREACHED();
  return false;
}

bool QuicReceiveControlStream::OnPriorityUpdateFrameStart(
    QuicByteCount /*header_length*/) {
  return ValidateFrameType(HttpFrameType::PRIORITY_UPDATE_REQUEST_STREAM);
}

bool QuicReceiveControlStream::OnPriorityUpdateFrame(
    const PriorityUpdateFrame& frame) {
  if (spdy_session()->debug_visitor()) {
    spdy_session()->debug_visitor()->OnPriorityUpdateFrameReceived(frame);
  }

  std::optional<HttpStreamPriority> priority =
      ParsePriorityFieldValue(frame.priority_field_value);

  if (!priority.has_value()) {
    stream_delegate()->OnStreamError(QUIC_INVALID_PRIORITY_UPDATE,
                                     "Invalid PRIORITY_UPDATE frame payload.");
    return false;
  }

  const QuicStreamId stream_id = frame.prioritized_element_id;
  return spdy_session_->OnPriorityUpdateForRequestStream(stream_id, *priority);
}

bool QuicReceiveControlStream::OnOriginFrameStart(
    QuicByteCount /* header_length */) {
  return ValidateFrameType(HttpFrameType::ORIGIN);
}

bool QuicReceiveControlStream::OnOriginFrame(const OriginFrame& frame) {
  QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, spdy_session()->perspective());

  if (spdy_session()->debug_visitor()) {
    spdy_session()->debug_visitor()->OnOriginFrameReceived(frame);
  }

  spdy_session()->OnOriginFrame(frame);
  return false;
}

bool QuicReceiveControlStream::OnAcceptChFrameStart(
    QuicByteCount /* header_length */) {
  return ValidateFrameType(HttpFrameType::ACCEPT_CH);
}

bool QuicReceiveControlStream::OnAcceptChFrame(const AcceptChFrame& frame) {
  QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, spdy_session()->perspective());

  if (spdy_session()->debug_visitor()) {
    spdy_session()->debug_visitor()->OnAcceptChFrameReceived(frame);
  }

  spdy_session()->OnAcceptChFrame(frame);
  return true;
}

void QuicReceiveControlStream::OnWebTransportStreamFrameType(
    QuicByteCount /*header_length*/, WebTransportSessionId /*session_id*/) {
  QUIC_BUG(WEBTRANSPORT_STREAM on Control Stream)
      << "Parsed WEBTRANSPORT_STREAM on a control stream.";
}

bool QuicReceiveControlStream::OnMetadataFrameStart(
    QuicByteCount /*header_length*/, QuicByteCount /*payload_length*/) {
  return ValidateFrameType(HttpFrameType::METADATA);
}

bool QuicReceiveControlStream::OnMetadataFramePayload(
    absl::string_view /*payload*/) {
  // Ignore METADATA frames.
  return true;
}

bool QuicReceiveControlStream::OnMetadataFrameEnd() {
  // Ignore METADATA frames.
  return true;
}

bool QuicReceiveControlStream::OnUnknownFrameStart(
    uint64_t frame_type, QuicByteCount /*header_length*/,
    QuicByteCount payload_length) {
  if (spdy_session()->debug_visitor()) {
    spdy_session()->debug_visitor()->OnUnknownFrameReceived(id(), frame_type,
                                                            payload_length);
  }

  return ValidateFrameType(static_cast<HttpFrameType>(frame_type));
}

bool QuicReceiveControlStream::OnUnknownFramePayload(
    absl::string_view /*payload*/) {
  // Ignore unknown frame types.
  return true;
}

bool QuicReceiveControlStream::OnUnknownFrameEnd() {
  // Ignore unknown frame types.
  return true;
}

bool QuicReceiveControlStream::ValidateFrameType(HttpFrameType frame_type) {
  // Certain frame types are forbidden.
  if (frame_type == HttpFrameType::DATA ||
      frame_type == HttpFrameType::HEADERS ||
      (spdy_session()->perspective() == Perspective::IS_CLIENT &&
       frame_type == HttpFrameType::MAX_PUSH_ID) ||
      (spdy_session()->perspective() == Perspective::IS_SERVER &&
       ((GetQuicReloadableFlag(enable_h3_origin_frame) &&
         frame_type == HttpFrameType::ORIGIN) ||
        frame_type == HttpFrameType::ACCEPT_CH))) {
    stream_delegate()->OnStreamError(
        QUIC_HTTP_FRAME_UNEXPECTED_ON_CONTROL_STREAM,
        absl::StrCat("Invalid frame type ", static_cast<int>(frame_type),
                     " received on control stream."));
    return false;
  }

  if (settings_frame_received_) {
    if (frame_type == HttpFrameType::SETTINGS) {
      // SETTINGS frame may only be the first frame on the control stream.
      stream_delegate()->OnStreamError(
          QUIC_HTTP_INVALID_FRAME_SEQUENCE_ON_CONTROL_STREAM,
          "SETTINGS frame can only be received once.");
      return false;
    }
    return true;
  }

  if (frame_type == HttpFrameType::SETTINGS) {
    settings_frame_received_ = true;
    return true;
  }
  stream_delegate()->OnStreamError(
      QUIC_HTTP_MISSING_SETTINGS_FRAME,
      absl::StrCat("First frame received on control stream is type ",
                   static_cast<int>(frame_type), ", but it must be SETTINGS."));
  return false;
}

}  // namespace quic

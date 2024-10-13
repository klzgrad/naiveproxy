// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/quic_send_control_stream.h"

#include <cstdint>
#include <memory>
#include <string>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/http/http_constants.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {
namespace {

}  // anonymous namespace

QuicSendControlStream::QuicSendControlStream(QuicStreamId id,
                                             QuicSpdySession* spdy_session,
                                             const SettingsFrame& settings)
    : QuicStream(id, spdy_session, /*is_static = */ true, WRITE_UNIDIRECTIONAL),
      settings_sent_(false),
      origin_frame_sent_(false),
      settings_(settings),
      spdy_session_(spdy_session) {}

void QuicSendControlStream::OnStreamReset(const QuicRstStreamFrame& /*frame*/) {
  QUIC_BUG(quic_bug_10382_1)
      << "OnStreamReset() called for write unidirectional stream.";
}

bool QuicSendControlStream::OnStopSending(QuicResetStreamError /* code */) {
  stream_delegate()->OnStreamError(
      QUIC_HTTP_CLOSED_CRITICAL_STREAM,
      "STOP_SENDING received for send control stream");
  return false;
}

void QuicSendControlStream::MaybeSendSettingsFrame() {
  if (settings_sent_) {
    return;
  }

  QuicConnection::ScopedPacketFlusher flusher(session()->connection());
  // Send the stream type on so the peer knows about this stream.
  char data[sizeof(kControlStream)];
  QuicDataWriter writer(ABSL_ARRAYSIZE(data), data);
  writer.WriteVarInt62(kControlStream);
  WriteOrBufferData(absl::string_view(writer.data(), writer.length()), false,
                    nullptr);

  SettingsFrame settings = settings_;
  // https://tools.ietf.org/html/draft-ietf-quic-http-25#section-7.2.4.1
  // specifies that setting identifiers of 0x1f * N + 0x21 are reserved and
  // greasing should be attempted.
  if (!GetQuicFlag(quic_enable_http3_grease_randomness)) {
    settings.values[0x40] = 20;
  } else {
    uint32_t result;
    QuicRandom::GetInstance()->RandBytes(&result, sizeof(result));
    uint64_t setting_id = 0x1fULL * static_cast<uint64_t>(result) + 0x21ULL;
    QuicRandom::GetInstance()->RandBytes(&result, sizeof(result));
    settings.values[setting_id] = result;
  }

  std::string settings_frame = HttpEncoder::SerializeSettingsFrame(settings);
  QUIC_DVLOG(1) << "Control stream " << id() << " is writing settings frame "
                << settings;
  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnSettingsFrameSent(settings);
  }
  WriteOrBufferData(settings_frame, /*fin = */ false, nullptr);
  settings_sent_ = true;

  // https://tools.ietf.org/html/draft-ietf-quic-http-25#section-7.2.9
  // specifies that a reserved frame type has no semantic meaning and should be
  // discarded. A greasing frame is added here.
  WriteOrBufferData(HttpEncoder::SerializeGreasingFrame(), /*fin = */ false,
                    nullptr);
}

void QuicSendControlStream::MaybeSendOriginFrame(
    std::vector<std::string> origins) {
  if (origins.empty() || origin_frame_sent_) {
    return;
  }
  OriginFrame frame;
  frame.origins = std::move(origins);
  QUIC_DVLOG(1) << "Control stream " << id() << " is writing origin frame "
                << frame;
  WriteOrBufferData(HttpEncoder::SerializeOriginFrame(frame), /*fin =*/false,
                    nullptr);
  origin_frame_sent_ = true;
}

void QuicSendControlStream::WritePriorityUpdate(QuicStreamId stream_id,
                                                HttpStreamPriority priority) {
  QuicConnection::ScopedPacketFlusher flusher(session()->connection());
  MaybeSendSettingsFrame();

  const std::string priority_field_value =
      SerializePriorityFieldValue(priority);
  PriorityUpdateFrame priority_update_frame{stream_id, priority_field_value};
  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnPriorityUpdateFrameSent(
        priority_update_frame);
  }

  std::string frame =
      HttpEncoder::SerializePriorityUpdateFrame(priority_update_frame);
  QUIC_DVLOG(1) << "Control Stream " << id() << " is writing "
                << priority_update_frame;
  WriteOrBufferData(frame, false, nullptr);
}

void QuicSendControlStream::SendGoAway(QuicStreamId id) {
  QuicConnection::ScopedPacketFlusher flusher(session()->connection());
  MaybeSendSettingsFrame();

  GoAwayFrame frame;
  frame.id = id;
  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnGoAwayFrameSent(id);
  }

  WriteOrBufferData(HttpEncoder::SerializeGoAwayFrame(frame), false, nullptr);
}

}  // namespace quic

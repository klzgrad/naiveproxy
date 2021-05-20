// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/http/quic_send_control_stream.h"
#include <cstdint>
#include <memory>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "quic/core/crypto/quic_random.h"
#include "quic/core/http/http_constants.h"
#include "quic/core/http/quic_spdy_session.h"
#include "quic/core/quic_session.h"
#include "quic/core/quic_types.h"
#include "quic/core/quic_utils.h"
#include "quic/platform/api/quic_logging.h"

namespace quic {

QuicSendControlStream::QuicSendControlStream(QuicStreamId id,
                                             QuicSpdySession* spdy_session,
                                             const SettingsFrame& settings)
    : QuicStream(id, spdy_session, /*is_static = */ true, WRITE_UNIDIRECTIONAL),
      settings_sent_(false),
      settings_(settings),
      spdy_session_(spdy_session) {}

void QuicSendControlStream::OnStreamReset(const QuicRstStreamFrame& /*frame*/) {
  QUIC_BUG << "OnStreamReset() called for write unidirectional stream.";
}

bool QuicSendControlStream::OnStopSending(QuicRstStreamErrorCode /* code */) {
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
  if (!GetQuicFlag(FLAGS_quic_enable_http3_grease_randomness)) {
    settings.values[0x40] = 20;
  } else {
    uint32_t result;
    QuicRandom::GetInstance()->RandBytes(&result, sizeof(result));
    uint64_t setting_id = 0x1fULL * static_cast<uint64_t>(result) + 0x21ULL;
    QuicRandom::GetInstance()->RandBytes(&result, sizeof(result));
    settings.values[setting_id] = result;
  }

  std::unique_ptr<char[]> buffer;
  QuicByteCount frame_length =
      HttpEncoder::SerializeSettingsFrame(settings, &buffer);
  QUIC_DVLOG(1) << "Control stream " << id() << " is writing settings frame "
                << settings;
  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnSettingsFrameSent(settings);
  }
  WriteOrBufferData(absl::string_view(buffer.get(), frame_length),
                    /*fin = */ false, nullptr);
  settings_sent_ = true;

  // https://tools.ietf.org/html/draft-ietf-quic-http-25#section-7.2.9
  // specifies that a reserved frame type has no semantic meaning and should be
  // discarded. A greasing frame is added here.
  std::unique_ptr<char[]> grease;
  QuicByteCount grease_length = HttpEncoder::SerializeGreasingFrame(&grease);
  WriteOrBufferData(absl::string_view(grease.get(), grease_length),
                    /*fin = */ false, nullptr);
}

void QuicSendControlStream::WritePriorityUpdate(
    const PriorityUpdateFrame& priority_update) {
  QuicConnection::ScopedPacketFlusher flusher(session()->connection());
  MaybeSendSettingsFrame();

  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnPriorityUpdateFrameSent(priority_update);
  }

  std::unique_ptr<char[]> buffer;
  QuicByteCount frame_length =
      HttpEncoder::SerializePriorityUpdateFrame(priority_update, &buffer);
  QUIC_DVLOG(1) << "Control Stream " << id() << " is writing "
                << priority_update;
  WriteOrBufferData(absl::string_view(buffer.get(), frame_length), false,
                    nullptr);
}

void QuicSendControlStream::SendMaxPushIdFrame(PushId max_push_id) {
  QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, session()->perspective());
  QuicConnection::ScopedPacketFlusher flusher(session()->connection());
  MaybeSendSettingsFrame();

  MaxPushIdFrame frame;
  frame.push_id = max_push_id;
  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnMaxPushIdFrameSent(frame);
  }

  std::unique_ptr<char[]> buffer;
  QuicByteCount frame_length =
      HttpEncoder::SerializeMaxPushIdFrame(frame, &buffer);
  WriteOrBufferData(absl::string_view(buffer.get(), frame_length),
                    /*fin = */ false, nullptr);
}

void QuicSendControlStream::SendGoAway(QuicStreamId id) {
  QuicConnection::ScopedPacketFlusher flusher(session()->connection());
  MaybeSendSettingsFrame();

  GoAwayFrame frame;
  frame.id = id;
  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnGoAwayFrameSent(id);
  }

  std::unique_ptr<char[]> buffer;
  QuicByteCount frame_length =
      HttpEncoder::SerializeGoAwayFrame(frame, &buffer);
  WriteOrBufferData(absl::string_view(buffer.get(), frame_length), false,
                    nullptr);
}

}  // namespace quic

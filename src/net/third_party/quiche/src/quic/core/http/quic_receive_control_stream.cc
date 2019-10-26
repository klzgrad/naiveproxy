// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_receive_control_stream.h"

#include "net/third_party/quiche/src/quic/core/http/http_constants.h"
#include "net/third_party/quiche/src/quic/core/http/http_decoder.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_session.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"

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

  void OnError(HttpDecoder* /*decoder*/) override {
    stream_->session()->connection()->CloseConnection(
        QUIC_HTTP_DECODER_ERROR, "Http decoder internal error",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }

  bool OnPriorityFrameStart(QuicByteCount header_length) override {
    if (stream_->session()->perspective() == Perspective::IS_CLIENT) {
      stream_->session()->connection()->CloseConnection(
          QUIC_HTTP_DECODER_ERROR, "Server must not send Priority frames.",
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return false;
    }
    return stream_->OnPriorityFrameStart(header_length);
  }

  bool OnPriorityFrame(const PriorityFrame& frame) override {
    if (stream_->session()->perspective() == Perspective::IS_CLIENT) {
      stream_->session()->connection()->CloseConnection(
          QUIC_HTTP_DECODER_ERROR, "Server must not send Priority frames.",
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return false;
    }
    return stream_->OnPriorityFrame(frame);
  }

  bool OnCancelPushFrame(const CancelPushFrame& /*frame*/) override {
    CloseConnectionOnWrongFrame("Cancel Push");
    return false;
  }

  bool OnMaxPushIdFrame(const MaxPushIdFrame& frame) override {
    if (stream_->session()->perspective() == Perspective::IS_SERVER) {
      QuicSpdySession* spdy_session =
          static_cast<QuicSpdySession*>(stream_->session());
      spdy_session->set_max_allowed_push_id(frame.push_id);
      return true;
    }
    CloseConnectionOnWrongFrame("Max Push Id");
    return false;
  }

  bool OnGoAwayFrame(const GoAwayFrame& /*frame*/) override {
    CloseConnectionOnWrongFrame("Goaway");
    return false;
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

  bool OnDataFramePayload(QuicStringPiece /*payload*/) override {
    CloseConnectionOnWrongFrame("Data");
    return false;
  }

  bool OnDataFrameEnd() override {
    CloseConnectionOnWrongFrame("Data");
    return false;
  }

  bool OnHeadersFrameStart(QuicByteCount /*frame_length*/) override {
    CloseConnectionOnWrongFrame("Headers");
    return false;
  }

  bool OnHeadersFramePayload(QuicStringPiece /*payload*/) override {
    CloseConnectionOnWrongFrame("Headers");
    return false;
  }

  bool OnHeadersFrameEnd() override {
    CloseConnectionOnWrongFrame("Headers");
    return false;
  }

  bool OnPushPromiseFrameStart(PushId /*push_id*/,
                               QuicByteCount /*frame_length*/,
                               QuicByteCount /*push_id_length*/) override {
    CloseConnectionOnWrongFrame("Push Promise");
    return false;
  }

  bool OnPushPromiseFramePayload(QuicStringPiece /*payload*/) override {
    CloseConnectionOnWrongFrame("Push Promise");
    return false;
  }

  bool OnPushPromiseFrameEnd() override {
    CloseConnectionOnWrongFrame("Push Promise");
    return false;
  }

  bool OnUnknownFrameStart(uint64_t /* frame_type */,
                           QuicByteCount /* frame_length */) override {
    // Ignore unknown frame types.
    return true;
  }

  bool OnUnknownFramePayload(QuicStringPiece /* payload */) override {
    // Ignore unknown frame types.
    return true;
  }

  bool OnUnknownFrameEnd() override {
    // Ignore unknown frame types.
    return true;
  }

 private:
  void CloseConnectionOnWrongFrame(QuicStringPiece frame_type) {
    // TODO(renjietang): Change to HTTP/3 error type.
    stream_->session()->connection()->CloseConnection(
        QUIC_HTTP_DECODER_ERROR,
        QuicStrCat(frame_type, " frame received on control stream"),
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }

  QuicReceiveControlStream* stream_;
};

QuicReceiveControlStream::QuicReceiveControlStream(PendingStream* pending)
    : QuicStream(pending, READ_UNIDIRECTIONAL, /*is_static=*/true),
      settings_frame_received_(false),
      http_decoder_visitor_(QuicMakeUnique<HttpDecoderVisitor>(this)),
      decoder_(http_decoder_visitor_.get()) {
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
  QuicSpdySession* spdy_session = static_cast<QuicSpdySession*>(session());
  for (const auto& setting : settings.values) {
    spdy_session->OnSetting(setting.first, setting.second);
  }
  return true;
}

bool QuicReceiveControlStream::OnPriorityFrameStart(
    QuicByteCount /* header_length */) {
  DCHECK_EQ(Perspective::IS_SERVER, session()->perspective());
  return true;
}

bool QuicReceiveControlStream::OnPriorityFrame(const PriorityFrame& priority) {
  DCHECK_EQ(Perspective::IS_SERVER, session()->perspective());
  QuicStream* stream =
      session()->GetOrCreateStream(priority.prioritized_element_id);
  // It's possible that the client sends a Priority frame for a request stream
  // that the server is not permitted to open. In that case, simply drop the
  // frame.
  if (stream) {
    stream->SetPriority(spdy::SpdyStreamPrecedence(priority.weight));
  }
  return true;
}

}  // namespace quic

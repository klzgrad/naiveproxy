// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_receive_control_stream.h"

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_session.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"

namespace quic {

const uint16_t kSettingsMaxHeaderListSize = 6;
const uint16_t kSettingsNumPlaceholders = 8;

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
        QUIC_HTTP_DECODER_ERROR, "Http decoder internal error",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }

  void OnPriorityFrame(const PriorityFrame& frame) override {
    CloseConnectionOnWrongFrame("Priority");
  }

  void OnCancelPushFrame(const CancelPushFrame& frame) override {
    CloseConnectionOnWrongFrame("Cancel Push");
  }

  void OnMaxPushIdFrame(const MaxPushIdFrame& frame) override {
    CloseConnectionOnWrongFrame("Max Push Id");
  }

  void OnGoAwayFrame(const GoAwayFrame& frame) override {
    CloseConnectionOnWrongFrame("Goaway");
  }

  void OnSettingsFrameStart(Http3FrameLengths frame_lengths) override {
    stream_->OnSettingsFrameStart(frame_lengths);
  }

  void OnSettingsFrame(const SettingsFrame& frame) override {
    stream_->OnSettingsFrame(frame);
  }

  void OnDuplicatePushFrame(const DuplicatePushFrame& frame) override {
    CloseConnectionOnWrongFrame("Duplicate Push");
  }

  void OnDataFrameStart(Http3FrameLengths frame_lengths) override {
    CloseConnectionOnWrongFrame("Data");
  }

  void OnDataFramePayload(QuicStringPiece payload) override {
    CloseConnectionOnWrongFrame("Data");
  }

  void OnDataFrameEnd() override { CloseConnectionOnWrongFrame("Data"); }

  void OnHeadersFrameStart(Http3FrameLengths frame_length) override {
    CloseConnectionOnWrongFrame("Headers");
  }

  void OnHeadersFramePayload(QuicStringPiece payload) override {
    CloseConnectionOnWrongFrame("Headers");
  }

  void OnHeadersFrameEnd() override { CloseConnectionOnWrongFrame("Headers"); }

  void OnPushPromiseFrameStart(PushId push_id) override {
    CloseConnectionOnWrongFrame("Push Promise");
  }

  void OnPushPromiseFramePayload(QuicStringPiece payload) override {
    CloseConnectionOnWrongFrame("Push Promise");
  }

  void OnPushPromiseFrameEnd() override {
    CloseConnectionOnWrongFrame("Push Promise");
  }

 private:
  void CloseConnectionOnWrongFrame(std::string frame_type) {
    // TODO(renjietang): Change to HTTP/3 error type.
    stream_->session()->connection()->CloseConnection(
        QUIC_HTTP_DECODER_ERROR,
        frame_type + " frame received on control stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }

  QuicReceiveControlStream* stream_;
};

QuicReceiveControlStream::QuicReceiveControlStream(QuicStreamId id,
                                                   QuicSpdySession* session)
    : QuicStream(id, session, /*is_static = */ true, READ_UNIDIRECTIONAL),
      received_settings_length_(0),
      http_decoder_visitor_(new HttpDecoderVisitor(this)) {
  decoder_.set_visitor(http_decoder_visitor_.get());
  sequencer()->set_level_triggered(true);
}

QuicReceiveControlStream::QuicReceiveControlStream(PendingStream pending)
    : QuicStream(std::move(pending), READ_UNIDIRECTIONAL, /*is_static=*/true),
      received_settings_length_(0),
      http_decoder_visitor_(new HttpDecoderVisitor(this)) {
  decoder_.set_visitor(http_decoder_visitor_.get());
  sequencer()->set_level_triggered(true);
}

QuicReceiveControlStream::~QuicReceiveControlStream() {}

void QuicReceiveControlStream::OnStreamReset(const QuicRstStreamFrame& frame) {
  // TODO(renjietang) Change the error code to H/3 specific
  // HTTP_CLOSED_CRITICAL_STREAM.
  session()->connection()->CloseConnection(
      QUIC_INVALID_STREAM_ID, "Attempt to reset receive control stream",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicReceiveControlStream::OnDataAvailable() {
  iovec iov;
  while (!reading_stopped() && sequencer()->PrefetchNextRegion(&iov)) {
    decoder_.ProcessInput(reinterpret_cast<const char*>(iov.iov_base),
                          iov.iov_len);
  }
}

void QuicReceiveControlStream::OnSettingsFrameStart(
    Http3FrameLengths frame_lengths) {
  if (received_settings_length_ != 0) {
    // TODO(renjietang): Change error code to HTTP_UNEXPECTED_FRAME.
    session()->connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID, "Settings frames are received twice.",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  received_settings_length_ +=
      frame_lengths.header_length + frame_lengths.payload_length;
}

void QuicReceiveControlStream::OnSettingsFrame(const SettingsFrame& settings) {
  QuicSpdySession* spdy_session = static_cast<QuicSpdySession*>(session());
  for (auto& it : settings.values) {
    uint16_t setting_id = it.first;
    switch (setting_id) {
      case kSettingsMaxHeaderListSize:
        spdy_session->set_max_inbound_header_list_size(it.second);
        break;
      case kSettingsNumPlaceholders:
        // TODO: Support placeholder setting
        break;
      default:
        break;
    }
  }
  sequencer()->MarkConsumed(received_settings_length_);
}

}  // namespace quic

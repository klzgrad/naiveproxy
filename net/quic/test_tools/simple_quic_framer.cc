// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/simple_quic_framer.h"

#include <memory>

#include "base/macros.h"
#include "net/quic/core/crypto/quic_decrypter.h"
#include "net/quic/core/crypto/quic_encrypter.h"
#include "net/quic/platform/api/quic_ptr_util.h"
#include "net/quic/platform/api/quic_string_piece.h"

using std::string;

namespace net {
namespace test {

class SimpleFramerVisitor : public QuicFramerVisitorInterface {
 public:
  SimpleFramerVisitor() : error_(QUIC_NO_ERROR) {}

  ~SimpleFramerVisitor() override {}

  void OnError(QuicFramer* framer) override { error_ = framer->error(); }

  bool OnProtocolVersionMismatch(QuicTransportVersion version) override {
    return false;
  }

  void OnPacket() override {}
  void OnPublicResetPacket(const QuicPublicResetPacket& packet) override {
    public_reset_packet_.reset(new QuicPublicResetPacket(packet));
  }
  void OnVersionNegotiationPacket(
      const QuicVersionNegotiationPacket& packet) override {
    version_negotiation_packet_.reset(new QuicVersionNegotiationPacket(packet));
  }

  bool OnUnauthenticatedPublicHeader(
      const QuicPacketPublicHeader& header) override {
    return true;
  }
  bool OnUnauthenticatedHeader(const QuicPacketHeader& header) override {
    return true;
  }
  void OnDecryptedPacket(EncryptionLevel level) override {}
  bool OnPacketHeader(const QuicPacketHeader& header) override {
    has_header_ = true;
    header_ = header;
    return true;
  }

  bool OnStreamFrame(const QuicStreamFrame& frame) override {
    // Save a copy of the data so it is valid after the packet is processed.
    string* string_data = new string(frame.data_buffer, frame.data_length);
    stream_data_.push_back(QuicWrapUnique(string_data));
    // TODO(ianswett): A pointer isn't necessary with emplace_back.
    stream_frames_.push_back(QuicMakeUnique<QuicStreamFrame>(
        frame.stream_id, frame.fin, frame.offset,
        QuicStringPiece(*string_data)));
    return true;
  }

  bool OnAckFrame(const QuicAckFrame& frame) override {
    ack_frames_.push_back(frame);
    return true;
  }

  bool OnStopWaitingFrame(const QuicStopWaitingFrame& frame) override {
    stop_waiting_frames_.push_back(frame);
    return true;
  }

  bool OnPaddingFrame(const QuicPaddingFrame& frame) override {
    padding_frames_.push_back(frame);
    return true;
  }

  bool OnPingFrame(const QuicPingFrame& frame) override {
    ping_frames_.push_back(frame);
    return true;
  }

  bool OnRstStreamFrame(const QuicRstStreamFrame& frame) override {
    rst_stream_frames_.push_back(frame);
    return true;
  }

  bool OnConnectionCloseFrame(const QuicConnectionCloseFrame& frame) override {
    connection_close_frames_.push_back(frame);
    return true;
  }

  bool OnGoAwayFrame(const QuicGoAwayFrame& frame) override {
    goaway_frames_.push_back(frame);
    return true;
  }

  bool OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) override {
    window_update_frames_.push_back(frame);
    return true;
  }

  bool OnBlockedFrame(const QuicBlockedFrame& frame) override {
    blocked_frames_.push_back(frame);
    return true;
  }

  void OnPacketComplete() override {}

  const QuicPacketHeader& header() const { return header_; }
  const std::vector<QuicAckFrame>& ack_frames() const { return ack_frames_; }
  const std::vector<QuicConnectionCloseFrame>& connection_close_frames() const {
    return connection_close_frames_;
  }
  const std::vector<QuicGoAwayFrame>& goaway_frames() const {
    return goaway_frames_;
  }
  const std::vector<QuicRstStreamFrame>& rst_stream_frames() const {
    return rst_stream_frames_;
  }
  const std::vector<std::unique_ptr<QuicStreamFrame>>& stream_frames() const {
    return stream_frames_;
  }
  const std::vector<QuicStopWaitingFrame>& stop_waiting_frames() const {
    return stop_waiting_frames_;
  }
  const std::vector<QuicPingFrame>& ping_frames() const { return ping_frames_; }
  const std::vector<QuicWindowUpdateFrame>& window_update_frames() const {
    return window_update_frames_;
  }
  const std::vector<QuicPaddingFrame>& padding_frames() const {
    return padding_frames_;
  }
  const QuicVersionNegotiationPacket* version_negotiation_packet() const {
    return version_negotiation_packet_.get();
  }

 private:
  QuicErrorCode error_;
  bool has_header_;
  QuicPacketHeader header_;
  std::unique_ptr<QuicVersionNegotiationPacket> version_negotiation_packet_;
  std::unique_ptr<QuicPublicResetPacket> public_reset_packet_;
  std::vector<QuicAckFrame> ack_frames_;
  std::vector<QuicStopWaitingFrame> stop_waiting_frames_;
  std::vector<QuicPaddingFrame> padding_frames_;
  std::vector<QuicPingFrame> ping_frames_;
  std::vector<std::unique_ptr<QuicStreamFrame>> stream_frames_;
  std::vector<QuicRstStreamFrame> rst_stream_frames_;
  std::vector<QuicGoAwayFrame> goaway_frames_;
  std::vector<QuicConnectionCloseFrame> connection_close_frames_;
  std::vector<QuicWindowUpdateFrame> window_update_frames_;
  std::vector<QuicBlockedFrame> blocked_frames_;
  std::vector<std::unique_ptr<string>> stream_data_;

  DISALLOW_COPY_AND_ASSIGN(SimpleFramerVisitor);
};

SimpleQuicFramer::SimpleQuicFramer()
    : framer_(AllSupportedTransportVersions(),
              QuicTime::Zero(),
              Perspective::IS_SERVER) {}

SimpleQuicFramer::SimpleQuicFramer(
    const QuicTransportVersionVector& supported_versions)
    : framer_(supported_versions, QuicTime::Zero(), Perspective::IS_SERVER) {}

SimpleQuicFramer::SimpleQuicFramer(
    const QuicTransportVersionVector& supported_versions,
    Perspective perspective)
    : framer_(supported_versions, QuicTime::Zero(), perspective) {}

SimpleQuicFramer::~SimpleQuicFramer() {}

bool SimpleQuicFramer::ProcessPacket(const QuicEncryptedPacket& packet) {
  visitor_.reset(new SimpleFramerVisitor);
  framer_.set_visitor(visitor_.get());
  return framer_.ProcessPacket(packet);
}

void SimpleQuicFramer::Reset() {
  visitor_.reset(new SimpleFramerVisitor);
}

const QuicPacketHeader& SimpleQuicFramer::header() const {
  return visitor_->header();
}

const QuicVersionNegotiationPacket*
SimpleQuicFramer::version_negotiation_packet() const {
  return visitor_->version_negotiation_packet();
}

QuicFramer* SimpleQuicFramer::framer() {
  return &framer_;
}

size_t SimpleQuicFramer::num_frames() const {
  return ack_frames().size() + goaway_frames().size() +
         rst_stream_frames().size() + stop_waiting_frames().size() +
         stream_frames().size() + ping_frames().size() +
         connection_close_frames().size() + padding_frames().size();
}

const std::vector<QuicAckFrame>& SimpleQuicFramer::ack_frames() const {
  return visitor_->ack_frames();
}

const std::vector<QuicStopWaitingFrame>& SimpleQuicFramer::stop_waiting_frames()
    const {
  return visitor_->stop_waiting_frames();
}

const std::vector<QuicPingFrame>& SimpleQuicFramer::ping_frames() const {
  return visitor_->ping_frames();
}

const std::vector<QuicWindowUpdateFrame>&
SimpleQuicFramer::window_update_frames() const {
  return visitor_->window_update_frames();
}

const std::vector<std::unique_ptr<QuicStreamFrame>>&
SimpleQuicFramer::stream_frames() const {
  return visitor_->stream_frames();
}

const std::vector<QuicRstStreamFrame>& SimpleQuicFramer::rst_stream_frames()
    const {
  return visitor_->rst_stream_frames();
}

const std::vector<QuicGoAwayFrame>& SimpleQuicFramer::goaway_frames() const {
  return visitor_->goaway_frames();
}

const std::vector<QuicConnectionCloseFrame>&
SimpleQuicFramer::connection_close_frames() const {
  return visitor_->connection_close_frames();
}

const std::vector<QuicPaddingFrame>& SimpleQuicFramer::padding_frames() const {
  return visitor_->padding_frames();
}

}  // namespace test
}  // namespace net

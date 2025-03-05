// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/simple_quic_framer.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/quic_decrypter.h"
#include "quiche/quic/core/crypto/quic_encrypter.h"
#include "quiche/quic/core/frames/quic_immediate_ack_frame.h"
#include "quiche/quic/core/frames/quic_reset_stream_at_frame.h"
#include "quiche/quic/core/quic_types.h"

namespace quic {
namespace test {

class SimpleFramerVisitor : public QuicFramerVisitorInterface {
 public:
  SimpleFramerVisitor() : error_(QUIC_NO_ERROR) {}
  SimpleFramerVisitor(const SimpleFramerVisitor&) = delete;
  SimpleFramerVisitor& operator=(const SimpleFramerVisitor&) = delete;

  ~SimpleFramerVisitor() override {}

  void OnError(QuicFramer* framer) override { error_ = framer->error(); }

  bool OnProtocolVersionMismatch(ParsedQuicVersion /*version*/) override {
    return false;
  }

  void OnPacket() override {}
  void OnVersionNegotiationPacket(
      const QuicVersionNegotiationPacket& packet) override {
    version_negotiation_packet_ =
        std::make_unique<QuicVersionNegotiationPacket>((packet));
  }

  void OnRetryPacket(QuicConnectionId /*original_connection_id*/,
                     QuicConnectionId /*new_connection_id*/,
                     absl::string_view /*retry_token*/,
                     absl::string_view /*retry_integrity_tag*/,
                     absl::string_view /*retry_without_tag*/) override {}

  bool OnUnauthenticatedPublicHeader(
      const QuicPacketHeader& /*header*/) override {
    return true;
  }
  bool OnUnauthenticatedHeader(const QuicPacketHeader& /*header*/) override {
    return true;
  }
  void OnDecryptedPacket(size_t /*length*/, EncryptionLevel level) override {
    last_decrypted_level_ = level;
  }
  bool OnPacketHeader(const QuicPacketHeader& header) override {
    has_header_ = true;
    header_ = header;
    return true;
  }

  void OnCoalescedPacket(const QuicEncryptedPacket& packet) override {
    coalesced_packet_ = packet.Clone();
  }

  void OnUndecryptablePacket(const QuicEncryptedPacket& /*packet*/,
                             EncryptionLevel /*decryption_level*/,
                             bool /*has_decryption_key*/) override {}

  // Returns the types of the frames in the packet so far, in the order they
  // were received.
  const std::vector<QuicFrameType>& frame_types() const { return frame_types_; }

  bool OnStreamFrame(const QuicStreamFrame& frame) override {
    // Save a copy of the data so it is valid after the packet is processed.
    std::string* string_data =
        new std::string(frame.data_buffer, frame.data_length);
    stream_data_.push_back(absl::WrapUnique(string_data));
    // TODO(ianswett): A pointer isn't necessary with emplace_back.
    stream_frames_.push_back(std::make_unique<QuicStreamFrame>(
        frame.stream_id, frame.fin, frame.offset,
        absl::string_view(*string_data)));
    frame_types_.push_back(STREAM_FRAME);
    return true;
  }

  bool OnCryptoFrame(const QuicCryptoFrame& frame) override {
    // Save a copy of the data so it is valid after the packet is processed.
    std::string* string_data =
        new std::string(frame.data_buffer, frame.data_length);
    crypto_data_.push_back(absl::WrapUnique(string_data));
    crypto_frames_.push_back(std::make_unique<QuicCryptoFrame>(
        frame.level, frame.offset, absl::string_view(*string_data)));
    frame_types_.push_back(CRYPTO_FRAME);
    return true;
  }

  bool OnAckFrameStart(QuicPacketNumber largest_acked,
                       QuicTime::Delta ack_delay_time) override {
    QuicAckFrame ack_frame;
    ack_frame.largest_acked = largest_acked;
    ack_frame.ack_delay_time = ack_delay_time;
    ack_frames_.push_back(ack_frame);
    frame_types_.push_back(ACK_FRAME);
    return true;
  }

  bool OnAckRange(QuicPacketNumber start, QuicPacketNumber end) override {
    QUICHE_DCHECK(!ack_frames_.empty());
    ack_frames_[ack_frames_.size() - 1].packets.AddRange(start, end);
    return true;
  }

  bool OnAckTimestamp(QuicPacketNumber /*packet_number*/,
                      QuicTime /*timestamp*/) override {
    return true;
  }

  bool OnAckFrameEnd(
      QuicPacketNumber /*start*/,
      const std::optional<QuicEcnCounts>& /*ecn_counts*/) override {
    return true;
  }

  bool OnStopWaitingFrame(const QuicStopWaitingFrame& frame) override {
    stop_waiting_frames_.push_back(frame);
    frame_types_.push_back(STOP_WAITING_FRAME);
    return true;
  }

  bool OnPaddingFrame(const QuicPaddingFrame& frame) override {
    padding_frames_.push_back(frame);
    frame_types_.push_back(PADDING_FRAME);
    return true;
  }

  bool OnPingFrame(const QuicPingFrame& frame) override {
    ping_frames_.push_back(frame);
    frame_types_.push_back(PING_FRAME);
    return true;
  }

  bool OnRstStreamFrame(const QuicRstStreamFrame& frame) override {
    rst_stream_frames_.push_back(frame);
    frame_types_.push_back(RST_STREAM_FRAME);
    return true;
  }

  bool OnConnectionCloseFrame(const QuicConnectionCloseFrame& frame) override {
    connection_close_frames_.push_back(frame);
    frame_types_.push_back(CONNECTION_CLOSE_FRAME);
    return true;
  }

  bool OnNewConnectionIdFrame(const QuicNewConnectionIdFrame& frame) override {
    new_connection_id_frames_.push_back(frame);
    frame_types_.push_back(NEW_CONNECTION_ID_FRAME);
    return true;
  }

  bool OnRetireConnectionIdFrame(
      const QuicRetireConnectionIdFrame& frame) override {
    retire_connection_id_frames_.push_back(frame);
    frame_types_.push_back(RETIRE_CONNECTION_ID_FRAME);
    return true;
  }

  bool OnNewTokenFrame(const QuicNewTokenFrame& frame) override {
    new_token_frames_.push_back(frame);
    frame_types_.push_back(NEW_TOKEN_FRAME);
    return true;
  }

  bool OnStopSendingFrame(const QuicStopSendingFrame& frame) override {
    stop_sending_frames_.push_back(frame);
    frame_types_.push_back(STOP_SENDING_FRAME);
    return true;
  }

  bool OnPathChallengeFrame(const QuicPathChallengeFrame& frame) override {
    path_challenge_frames_.push_back(frame);
    frame_types_.push_back(PATH_CHALLENGE_FRAME);
    return true;
  }

  bool OnPathResponseFrame(const QuicPathResponseFrame& frame) override {
    path_response_frames_.push_back(frame);
    frame_types_.push_back(PATH_RESPONSE_FRAME);
    return true;
  }

  bool OnGoAwayFrame(const QuicGoAwayFrame& frame) override {
    goaway_frames_.push_back(frame);
    frame_types_.push_back(GOAWAY_FRAME);
    return true;
  }
  bool OnMaxStreamsFrame(const QuicMaxStreamsFrame& frame) override {
    max_streams_frames_.push_back(frame);
    frame_types_.push_back(MAX_STREAMS_FRAME);
    return true;
  }

  bool OnStreamsBlockedFrame(const QuicStreamsBlockedFrame& frame) override {
    streams_blocked_frames_.push_back(frame);
    frame_types_.push_back(STREAMS_BLOCKED_FRAME);
    return true;
  }

  bool OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) override {
    window_update_frames_.push_back(frame);
    frame_types_.push_back(WINDOW_UPDATE_FRAME);
    return true;
  }

  bool OnBlockedFrame(const QuicBlockedFrame& frame) override {
    blocked_frames_.push_back(frame);
    frame_types_.push_back(BLOCKED_FRAME);
    return true;
  }

  bool OnMessageFrame(const QuicMessageFrame& frame) override {
    message_frames_.emplace_back(frame.data, frame.message_length);
    frame_types_.push_back(MESSAGE_FRAME);
    return true;
  }

  bool OnHandshakeDoneFrame(const QuicHandshakeDoneFrame& frame) override {
    handshake_done_frames_.push_back(frame);
    frame_types_.push_back(HANDSHAKE_DONE_FRAME);
    return true;
  }

  bool OnAckFrequencyFrame(const QuicAckFrequencyFrame& frame) override {
    ack_frequency_frames_.push_back(frame);
    frame_types_.push_back(ACK_FREQUENCY_FRAME);
    return true;
  }

  bool OnImmediateAckFrame(const QuicImmediateAckFrame& frame) override {
    immediate_ack_frames_.push_back(frame);
    frame_types_.push_back(IMMEDIATE_ACK_FRAME);
    return true;
  }

  bool OnResetStreamAtFrame(const QuicResetStreamAtFrame& frame) override {
    reset_stream_at_frames_.push_back(frame);
    frame_types_.push_back(RESET_STREAM_AT_FRAME);
    return true;
  }

  void OnPacketComplete() override {}

  bool IsValidStatelessResetToken(
      const StatelessResetToken& /*token*/) const override {
    return false;
  }

  void OnAuthenticatedIetfStatelessResetPacket(
      const QuicIetfStatelessResetPacket& packet) override {
    stateless_reset_packet_ =
        std::make_unique<QuicIetfStatelessResetPacket>(packet);
  }

  void OnKeyUpdate(KeyUpdateReason /*reason*/) override {}
  void OnDecryptedFirstPacketInKeyPhase() override {}
  std::unique_ptr<QuicDecrypter> AdvanceKeysAndCreateCurrentOneRttDecrypter()
      override {
    return nullptr;
  }
  std::unique_ptr<QuicEncrypter> CreateCurrentOneRttEncrypter() override {
    return nullptr;
  }

  const QuicPacketHeader& header() const { return header_; }
  const std::vector<QuicAckFrame>& ack_frames() const { return ack_frames_; }
  const std::vector<QuicConnectionCloseFrame>& connection_close_frames() const {
    return connection_close_frames_;
  }

  const std::vector<QuicGoAwayFrame>& goaway_frames() const {
    return goaway_frames_;
  }
  const std::vector<QuicMaxStreamsFrame>& max_streams_frames() const {
    return max_streams_frames_;
  }
  const std::vector<QuicStreamsBlockedFrame>& streams_blocked_frames() const {
    return streams_blocked_frames_;
  }
  const std::vector<QuicRstStreamFrame>& rst_stream_frames() const {
    return rst_stream_frames_;
  }
  const std::vector<std::unique_ptr<QuicStreamFrame>>& stream_frames() const {
    return stream_frames_;
  }
  const std::vector<std::unique_ptr<QuicCryptoFrame>>& crypto_frames() const {
    return crypto_frames_;
  }
  const std::vector<QuicStopWaitingFrame>& stop_waiting_frames() const {
    return stop_waiting_frames_;
  }
  const std::vector<QuicPingFrame>& ping_frames() const { return ping_frames_; }
  const std::vector<QuicMessageFrame>& message_frames() const {
    return message_frames_;
  }
  const std::vector<QuicWindowUpdateFrame>& window_update_frames() const {
    return window_update_frames_;
  }
  const std::vector<QuicPaddingFrame>& padding_frames() const {
    return padding_frames_;
  }
  const std::vector<QuicPathChallengeFrame>& path_challenge_frames() const {
    return path_challenge_frames_;
  }
  const std::vector<QuicPathResponseFrame>& path_response_frames() const {
    return path_response_frames_;
  }
  const QuicVersionNegotiationPacket* version_negotiation_packet() const {
    return version_negotiation_packet_.get();
  }
  EncryptionLevel last_decrypted_level() const { return last_decrypted_level_; }
  const QuicEncryptedPacket* coalesced_packet() const {
    return coalesced_packet_.get();
  }

 private:
  QuicErrorCode error_;
  bool has_header_;
  QuicPacketHeader header_;
  std::unique_ptr<QuicVersionNegotiationPacket> version_negotiation_packet_;
  std::unique_ptr<QuicIetfStatelessResetPacket> stateless_reset_packet_;
  std::vector<QuicFrameType> frame_types_;
  std::vector<QuicAckFrame> ack_frames_;
  std::vector<QuicStopWaitingFrame> stop_waiting_frames_;
  std::vector<QuicPaddingFrame> padding_frames_;
  std::vector<QuicPingFrame> ping_frames_;
  std::vector<std::unique_ptr<QuicStreamFrame>> stream_frames_;
  std::vector<std::unique_ptr<QuicCryptoFrame>> crypto_frames_;
  std::vector<QuicRstStreamFrame> rst_stream_frames_;
  std::vector<QuicGoAwayFrame> goaway_frames_;
  std::vector<QuicStreamsBlockedFrame> streams_blocked_frames_;
  std::vector<QuicMaxStreamsFrame> max_streams_frames_;
  std::vector<QuicConnectionCloseFrame> connection_close_frames_;
  std::vector<QuicStopSendingFrame> stop_sending_frames_;
  std::vector<QuicPathChallengeFrame> path_challenge_frames_;
  std::vector<QuicPathResponseFrame> path_response_frames_;
  std::vector<QuicWindowUpdateFrame> window_update_frames_;
  std::vector<QuicBlockedFrame> blocked_frames_;
  std::vector<QuicNewConnectionIdFrame> new_connection_id_frames_;
  std::vector<QuicRetireConnectionIdFrame> retire_connection_id_frames_;
  std::vector<QuicNewTokenFrame> new_token_frames_;
  std::vector<QuicMessageFrame> message_frames_;
  std::vector<QuicHandshakeDoneFrame> handshake_done_frames_;
  std::vector<QuicAckFrequencyFrame> ack_frequency_frames_;
  std::vector<QuicImmediateAckFrame> immediate_ack_frames_;
  std::vector<QuicResetStreamAtFrame> reset_stream_at_frames_;
  std::vector<std::unique_ptr<std::string>> stream_data_;
  std::vector<std::unique_ptr<std::string>> crypto_data_;
  EncryptionLevel last_decrypted_level_;
  std::unique_ptr<QuicEncryptedPacket> coalesced_packet_;
};

SimpleQuicFramer::SimpleQuicFramer()
    : framer_(AllSupportedVersions(), QuicTime::Zero(), Perspective::IS_SERVER,
              kQuicDefaultConnectionIdLength) {
  framer_.set_process_reset_stream_at(true);
}

SimpleQuicFramer::SimpleQuicFramer(
    const ParsedQuicVersionVector& supported_versions)
    : framer_(supported_versions, QuicTime::Zero(), Perspective::IS_SERVER,
              kQuicDefaultConnectionIdLength) {
  framer_.set_process_reset_stream_at(true);
}

SimpleQuicFramer::SimpleQuicFramer(
    const ParsedQuicVersionVector& supported_versions, Perspective perspective)
    : framer_(supported_versions, QuicTime::Zero(), perspective,
              kQuicDefaultConnectionIdLength) {
  framer_.set_process_reset_stream_at(true);
}

SimpleQuicFramer::~SimpleQuicFramer() {}

bool SimpleQuicFramer::ProcessPacket(const QuicEncryptedPacket& packet) {
  visitor_ = std::make_unique<SimpleFramerVisitor>();
  framer_.set_visitor(visitor_.get());
  return framer_.ProcessPacket(packet);
}

void SimpleQuicFramer::Reset() {
  visitor_ = std::make_unique<SimpleFramerVisitor>();
}

const QuicPacketHeader& SimpleQuicFramer::header() const {
  return visitor_->header();
}

const QuicVersionNegotiationPacket*
SimpleQuicFramer::version_negotiation_packet() const {
  return visitor_->version_negotiation_packet();
}

EncryptionLevel SimpleQuicFramer::last_decrypted_level() const {
  return visitor_->last_decrypted_level();
}

QuicFramer* SimpleQuicFramer::framer() { return &framer_; }

size_t SimpleQuicFramer::num_frames() const {
  return ack_frames().size() + goaway_frames().size() +
         rst_stream_frames().size() + stop_waiting_frames().size() +
         path_challenge_frames().size() + path_response_frames().size() +
         stream_frames().size() + ping_frames().size() +
         connection_close_frames().size() + padding_frames().size() +
         crypto_frames().size();
}

const std::vector<QuicFrameType>& SimpleQuicFramer::frame_types() const {
  return visitor_->frame_types();
}

const std::vector<QuicAckFrame>& SimpleQuicFramer::ack_frames() const {
  return visitor_->ack_frames();
}

const std::vector<QuicStopWaitingFrame>& SimpleQuicFramer::stop_waiting_frames()
    const {
  return visitor_->stop_waiting_frames();
}

const std::vector<QuicPathChallengeFrame>&
SimpleQuicFramer::path_challenge_frames() const {
  return visitor_->path_challenge_frames();
}
const std::vector<QuicPathResponseFrame>&
SimpleQuicFramer::path_response_frames() const {
  return visitor_->path_response_frames();
}

const std::vector<QuicPingFrame>& SimpleQuicFramer::ping_frames() const {
  return visitor_->ping_frames();
}

const std::vector<QuicMessageFrame>& SimpleQuicFramer::message_frames() const {
  return visitor_->message_frames();
}

const std::vector<QuicWindowUpdateFrame>&
SimpleQuicFramer::window_update_frames() const {
  return visitor_->window_update_frames();
}

const std::vector<std::unique_ptr<QuicStreamFrame>>&
SimpleQuicFramer::stream_frames() const {
  return visitor_->stream_frames();
}

const std::vector<std::unique_ptr<QuicCryptoFrame>>&
SimpleQuicFramer::crypto_frames() const {
  return visitor_->crypto_frames();
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

const QuicEncryptedPacket* SimpleQuicFramer::coalesced_packet() const {
  return visitor_->coalesced_packet();
}

}  // namespace test
}  // namespace quic

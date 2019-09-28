// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/quic_connection_logger.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "base/metrics/histogram_base.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/values.h"
#include "net/base/ip_address.h"
#include "net/cert/x509_certificate.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_values.h"
#include "net/quic/address_utils.h"
#include "net/quic/quic_address_mismatch.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake_message.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_socket_address_coder.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"

using std::string;

namespace net {

namespace {

base::Value NetLogQuicPacketParams(const quic::QuicSocketAddress& self_address,
                                   const quic::QuicSocketAddress& peer_address,
                                   size_t packet_size) {
  base::DictionaryValue dict;
  dict.SetString("self_address", self_address.ToString());
  dict.SetString("peer_address", peer_address.ToString());
  dict.SetInteger("size", packet_size);
  return std::move(dict);
}

base::Value NetLogQuicPacketSentParams(
    const quic::SerializedPacket& serialized_packet,
    quic::TransmissionType transmission_type,
    quic::QuicTime sent_time) {
  base::DictionaryValue dict;
  dict.SetInteger("transmission_type", transmission_type);
  dict.SetKey("packet_number",
              NetLogNumberValue(serialized_packet.packet_number.ToUint64()));
  dict.SetInteger("size", serialized_packet.encrypted_length);
  dict.SetKey("sent_time_us", NetLogNumberValue(sent_time.ToDebuggingValue()));
  return std::move(dict);
}

base::Value NetLogQuicPacketRetransmittedParams(
    quic::QuicPacketNumber old_packet_number,
    quic::QuicPacketNumber new_packet_number) {
  base::DictionaryValue dict;
  dict.SetKey("old_packet_number",
              NetLogNumberValue(old_packet_number.ToUint64()));
  dict.SetKey("new_packet_number",
              NetLogNumberValue(new_packet_number.ToUint64()));
  return std::move(dict);
}

base::Value NetLogQuicPacketLostParams(quic::QuicPacketNumber packet_number,
                                       quic::TransmissionType transmission_type,
                                       quic::QuicTime detection_time) {
  base::DictionaryValue dict;
  dict.SetInteger("transmission_type", transmission_type);
  dict.SetKey("packet_number", NetLogNumberValue(packet_number.ToUint64()));
  dict.SetKey("detection_time_us",
              NetLogNumberValue(detection_time.ToDebuggingValue()));
  return std::move(dict);
}

base::Value NetLogQuicDuplicatePacketParams(
    quic::QuicPacketNumber packet_number) {
  base::DictionaryValue dict;
  dict.SetKey("packet_number", NetLogNumberValue(packet_number.ToUint64()));
  return std::move(dict);
}

base::Value NetLogQuicPacketHeaderParams(const quic::QuicPacketHeader* header) {
  base::DictionaryValue dict;
  dict.SetString("connection_id", header->destination_connection_id.ToString());
  dict.SetInteger("reset_flag", header->reset_flag);
  dict.SetInteger("version_flag", header->version_flag);
  dict.SetKey("packet_number",
              NetLogNumberValue(header->packet_number.ToUint64()));
  return std::move(dict);
}

base::Value NetLogQuicStreamFrameParams(const quic::QuicStreamFrame& frame) {
  base::DictionaryValue dict;
  dict.SetInteger("stream_id", frame.stream_id);
  dict.SetBoolean("fin", frame.fin);
  dict.SetKey("offset", NetLogNumberValue(frame.offset));
  dict.SetInteger("length", frame.data_length);
  return std::move(dict);
}

base::Value NetLogQuicAckFrameParams(const quic::QuicAckFrame* frame) {
  base::DictionaryValue dict;
  dict.SetKey("largest_observed",
              NetLogNumberValue(frame->largest_acked.ToUint64()));
  dict.SetKey("delta_time_largest_observed_us",
              NetLogNumberValue(frame->ack_delay_time.ToMicroseconds()));

  auto missing = std::make_unique<base::ListValue>();
  if (!frame->packets.Empty()) {
    // V34 and above express acked packets, but only print
    // missing packets, because it's typically a shorter list.
    for (quic::QuicPacketNumber packet = frame->packets.Min();
         packet < frame->largest_acked; ++packet) {
      if (!frame->packets.Contains(packet)) {
        missing->GetList().push_back(NetLogNumberValue(packet.ToUint64()));
      }
    }
  }
  dict.Set("missing_packets", std::move(missing));

  auto received = std::make_unique<base::ListValue>();
  const quic::PacketTimeVector& received_times = frame->received_packet_times;
  for (auto it = received_times.begin(); it != received_times.end(); ++it) {
    auto info = std::make_unique<base::DictionaryValue>();
    info->SetKey("packet_number", NetLogNumberValue(it->first.ToUint64()));
    info->SetKey("received", NetLogNumberValue(it->second.ToDebuggingValue()));
    received->Append(std::move(info));
  }
  dict.Set("received_packet_times", std::move(received));

  return std::move(dict);
}

base::Value NetLogQuicRstStreamFrameParams(
    const quic::QuicRstStreamFrame* frame) {
  base::DictionaryValue dict;
  dict.SetInteger("stream_id", frame->stream_id);
  dict.SetInteger("quic_rst_stream_error", frame->error_code);
  return std::move(dict);
}

base::Value NetLogQuicConnectionCloseFrameParams(
    const quic::QuicConnectionCloseFrame* frame) {
  base::DictionaryValue dict;
  dict.SetInteger("quic_error", frame->quic_error_code);
  dict.SetString("details", frame->error_details);
  return std::move(dict);
}

base::Value NetLogQuicWindowUpdateFrameParams(
    const quic::QuicWindowUpdateFrame* frame) {
  base::DictionaryValue dict;
  dict.SetInteger("stream_id", frame->stream_id);
  dict.SetKey("byte_offset", NetLogNumberValue(frame->byte_offset));
  return std::move(dict);
}

base::Value NetLogQuicBlockedFrameParams(const quic::QuicBlockedFrame* frame) {
  base::DictionaryValue dict;
  dict.SetInteger("stream_id", frame->stream_id);
  return std::move(dict);
}

base::Value NetLogQuicGoAwayFrameParams(const quic::QuicGoAwayFrame* frame) {
  base::DictionaryValue dict;
  dict.SetInteger("quic_error", frame->error_code);
  dict.SetInteger("last_good_stream_id", frame->last_good_stream_id);
  dict.SetString("reason_phrase", frame->reason_phrase);
  return std::move(dict);
}

base::Value NetLogQuicStopWaitingFrameParams(
    const quic::QuicStopWaitingFrame* frame) {
  base::DictionaryValue dict;
  auto sent_info = std::make_unique<base::DictionaryValue>();
  sent_info->SetKey("least_unacked",
                    NetLogNumberValue(frame->least_unacked.ToUint64()));
  dict.Set("sent_info", std::move(sent_info));
  return std::move(dict);
}

base::Value NetLogQuicVersionNegotiationPacketParams(
    const quic::QuicVersionNegotiationPacket* packet) {
  base::DictionaryValue dict;
  auto versions = std::make_unique<base::ListValue>();
  for (auto it = packet->versions.begin(); it != packet->versions.end(); ++it) {
    versions->AppendString(ParsedQuicVersionToString(*it));
  }
  dict.Set("versions", std::move(versions));
  return std::move(dict);
}

base::Value NetLogQuicPublicResetPacketParams(
    const IPEndPoint& server_hello_address,
    const quic::QuicSocketAddress& public_reset_address) {
  base::DictionaryValue dict;
  dict.SetString("server_hello_address", server_hello_address.ToString());
  dict.SetString("public_reset_address", public_reset_address.ToString());
  return std::move(dict);
}

base::Value NetLogQuicCryptoHandshakeMessageParams(
    const quic::CryptoHandshakeMessage* message) {
  base::DictionaryValue dict;
  dict.SetString("quic_crypto_handshake_message", message->DebugString());
  return std::move(dict);
}

base::Value NetLogQuicOnConnectionClosedParams(
    quic::QuicErrorCode error,
    string error_details,
    quic::ConnectionCloseSource source) {
  base::DictionaryValue dict;
  dict.SetInteger("quic_error", error);
  dict.SetString("details", error_details);
  dict.SetBoolean("from_peer", source == quic::ConnectionCloseSource::FROM_PEER
                                   ? true
                                   : false);
  return std::move(dict);
}

base::Value NetLogQuicCertificateVerifiedParams(
    scoped_refptr<X509Certificate> cert) {
  // Only the subjects are logged so that we can investigate connection pooling.
  // More fields could be logged in the future.
  std::vector<std::string> dns_names;
  cert->GetSubjectAltName(&dns_names, nullptr);
  base::DictionaryValue dict;
  auto subjects = std::make_unique<base::ListValue>();
  for (auto& dns_name : dns_names) {
    subjects->GetList().emplace_back(std::move(dns_name));
  }
  dict.Set("subjects", std::move(subjects));
  return std::move(dict);
}

void UpdatePublicResetAddressMismatchHistogram(
    const IPEndPoint& server_hello_address,
    const IPEndPoint& public_reset_address) {
  int sample = GetAddressMismatch(server_hello_address, public_reset_address);
  // We are seemingly talking to an older server that does not support the
  // feature, so we can't report the results in the histogram.
  if (sample < 0) {
    return;
  }
  UMA_HISTOGRAM_ENUMERATION("Net.QuicSession.PublicResetAddressMismatch2",
                            static_cast<QuicAddressMismatch>(sample),
                            QUIC_ADDRESS_MISMATCH_MAX);
}

// If |address| is an IPv4-mapped IPv6 address, returns ADDRESS_FAMILY_IPV4
// instead of ADDRESS_FAMILY_IPV6. Othewise, behaves like GetAddressFamily().
AddressFamily GetRealAddressFamily(const IPAddress& address) {
  return address.IsIPv4MappedIPv6() ? ADDRESS_FAMILY_IPV4
                                    : GetAddressFamily(address);
}

}  // namespace

QuicConnectionLogger::QuicConnectionLogger(
    quic::QuicSpdySession* session,
    const char* const connection_description,
    std::unique_ptr<SocketPerformanceWatcher> socket_performance_watcher,
    const NetLogWithSource& net_log)
    : net_log_(net_log),
      session_(session),
      last_received_packet_size_(0),
      no_packet_received_after_ping_(false),
      previous_received_packet_size_(0),
      num_out_of_order_received_packets_(0),
      num_out_of_order_large_received_packets_(0),
      num_packets_received_(0),
      num_frames_received_(0),
      num_duplicate_frames_received_(0),
      num_incorrect_connection_ids_(0),
      num_undecryptable_packets_(0),
      num_duplicate_packets_(0),
      num_blocked_frames_received_(0),
      num_blocked_frames_sent_(0),
      connection_description_(connection_description),
      socket_performance_watcher_(std::move(socket_performance_watcher)),
      net_log_is_capturing_(net_log_.IsCapturing()) {
  timer_.Start(FROM_HERE, base::TimeDelta::FromSeconds(1), this,
               &QuicConnectionLogger::UpdateIsCapturing);
}

QuicConnectionLogger::~QuicConnectionLogger() {
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicSession.OutOfOrderPacketsReceived",
                          num_out_of_order_received_packets_);
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicSession.OutOfOrderLargePacketsReceived",
                          num_out_of_order_large_received_packets_);
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicSession.IncorrectConnectionIDsReceived",
                          num_incorrect_connection_ids_);
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicSession.UndecryptablePacketsReceived",
                          num_undecryptable_packets_);
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicSession.DuplicatePacketsReceived",
                          num_duplicate_packets_);
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicSession.BlockedFrames.Received",
                          num_blocked_frames_received_);
  UMA_HISTOGRAM_COUNTS_1M("Net.QuicSession.BlockedFrames.Sent",
                          num_blocked_frames_sent_);

  const quic::QuicConnectionStats& stats = session_->connection()->GetStats();
  UMA_HISTOGRAM_TIMES("Net.QuicSession.MinRTT",
                      base::TimeDelta::FromMicroseconds(stats.min_rtt_us));
  UMA_HISTOGRAM_TIMES("Net.QuicSession.SmoothedRTT",
                      base::TimeDelta::FromMicroseconds(stats.srtt_us));

  if (num_frames_received_ > 0) {
    int duplicate_stream_frame_per_thousand =
        num_duplicate_frames_received_ * 1000 / num_frames_received_;
    if (num_packets_received_ < 100) {
      UMA_HISTOGRAM_CUSTOM_COUNTS(
          "Net.QuicSession.StreamFrameDuplicatedShortConnection",
          duplicate_stream_frame_per_thousand, 1, 1000, 75);
    } else {
      UMA_HISTOGRAM_CUSTOM_COUNTS(
          "Net.QuicSession.StreamFrameDuplicatedLongConnection",
          duplicate_stream_frame_per_thousand, 1, 1000, 75);
    }
  }

  RecordAggregatePacketLossRate();
}

void QuicConnectionLogger::OnFrameAddedToPacket(const quic::QuicFrame& frame) {
  switch (frame.type) {
    case quic::PADDING_FRAME:
      break;
    case quic::STREAM_FRAME:
      break;
    case quic::ACK_FRAME: {
      break;
    }
    case quic::RST_STREAM_FRAME:
      base::UmaHistogramSparse("Net.QuicSession.RstStreamErrorCodeClient",
                               frame.rst_stream_frame->error_code);
      break;
    case quic::CONNECTION_CLOSE_FRAME:
      break;
    case quic::GOAWAY_FRAME:
      break;
    case quic::WINDOW_UPDATE_FRAME:
      break;
    case quic::BLOCKED_FRAME:
      ++num_blocked_frames_sent_;
      break;
    case quic::STOP_WAITING_FRAME:
      break;
    case quic::PING_FRAME:
      UMA_HISTOGRAM_BOOLEAN("Net.QuicSession.ConnectionFlowControlBlocked",
                            session_->IsConnectionFlowControlBlocked());
      UMA_HISTOGRAM_BOOLEAN("Net.QuicSession.StreamFlowControlBlocked",
                            session_->IsStreamFlowControlBlocked());
      break;
    case quic::MTU_DISCOVERY_FRAME:
      break;
    case quic::NEW_CONNECTION_ID_FRAME:
      break;
    case quic::MAX_STREAMS_FRAME:
      break;
    case quic::STREAMS_BLOCKED_FRAME:
      break;
    case quic::PATH_RESPONSE_FRAME:
      break;
    case quic::PATH_CHALLENGE_FRAME:
      break;
    case quic::STOP_SENDING_FRAME:
      break;
    case quic::MESSAGE_FRAME:
      break;
    case quic::CRYPTO_FRAME:
      break;
    case quic::NEW_TOKEN_FRAME:
      break;
    case quic::RETIRE_CONNECTION_ID_FRAME:
      break;
    default:
      DCHECK(false) << "Illegal frame type: " << frame.type;
  }
  if (!net_log_is_capturing_)
    return;
  switch (frame.type) {
    case quic::PADDING_FRAME:
      break;
    case quic::STREAM_FRAME:
      net_log_.AddEvent(NetLogEventType::QUIC_SESSION_STREAM_FRAME_SENT, [&] {
        return NetLogQuicStreamFrameParams(frame.stream_frame);
      });
      break;
    case quic::ACK_FRAME: {
      net_log_.AddEvent(NetLogEventType::QUIC_SESSION_ACK_FRAME_SENT, [&] {
        return NetLogQuicAckFrameParams(frame.ack_frame);
      });
      break;
    }
    case quic::RST_STREAM_FRAME:
      base::UmaHistogramSparse("Net.QuicSession.RstStreamErrorCodeClient",
                               frame.rst_stream_frame->error_code);
      net_log_.AddEvent(
          NetLogEventType::QUIC_SESSION_RST_STREAM_FRAME_SENT, [&] {
            return NetLogQuicRstStreamFrameParams(frame.rst_stream_frame);
          });
      break;
    case quic::CONNECTION_CLOSE_FRAME:
      net_log_.AddEvent(
          NetLogEventType::QUIC_SESSION_CONNECTION_CLOSE_FRAME_SENT, [&] {
            return NetLogQuicConnectionCloseFrameParams(
                frame.connection_close_frame);
          });
      break;
    case quic::GOAWAY_FRAME:
      net_log_.AddEvent(NetLogEventType::QUIC_SESSION_GOAWAY_FRAME_SENT, [&] {
        return NetLogQuicGoAwayFrameParams(frame.goaway_frame);
      });
      break;
    case quic::WINDOW_UPDATE_FRAME:
      net_log_.AddEvent(
          NetLogEventType::QUIC_SESSION_WINDOW_UPDATE_FRAME_SENT, [&] {
            return NetLogQuicWindowUpdateFrameParams(frame.window_update_frame);
          });
      break;
    case quic::BLOCKED_FRAME:
      net_log_.AddEvent(NetLogEventType::QUIC_SESSION_BLOCKED_FRAME_SENT, [&] {
        return NetLogQuicBlockedFrameParams(frame.blocked_frame);
      });
      break;
    case quic::STOP_WAITING_FRAME:
      net_log_.AddEvent(
          NetLogEventType::QUIC_SESSION_STOP_WAITING_FRAME_SENT, [&] {
            return NetLogQuicStopWaitingFrameParams(&frame.stop_waiting_frame);
          });
      break;
    case quic::PING_FRAME:
      UMA_HISTOGRAM_BOOLEAN("Net.QuicSession.ConnectionFlowControlBlocked",
                            session_->IsConnectionFlowControlBlocked());
      UMA_HISTOGRAM_BOOLEAN("Net.QuicSession.StreamFlowControlBlocked",
                            session_->IsStreamFlowControlBlocked());
      // PingFrame has no contents to log, so just record that it was sent.
      net_log_.AddEvent(NetLogEventType::QUIC_SESSION_PING_FRAME_SENT);
      break;
    case quic::MTU_DISCOVERY_FRAME:
      // MtuDiscoveryFrame is PingFrame on wire, it does not have any payload.
      net_log_.AddEvent(NetLogEventType::QUIC_SESSION_MTU_DISCOVERY_FRAME_SENT);
      break;
    case quic::NEW_CONNECTION_ID_FRAME:
      break;
    case quic::MAX_STREAMS_FRAME:
      break;
    case quic::STREAMS_BLOCKED_FRAME:
      break;
    case quic::PATH_RESPONSE_FRAME:
      break;
    case quic::PATH_CHALLENGE_FRAME:
      break;
    case quic::STOP_SENDING_FRAME:
      break;
    case quic::MESSAGE_FRAME:
      break;
    case quic::CRYPTO_FRAME:
      break;
    case quic::NEW_TOKEN_FRAME:
      break;
    case quic::RETIRE_CONNECTION_ID_FRAME:
      break;
    default:
      DCHECK(false) << "Illegal frame type: " << frame.type;
  }
}

void QuicConnectionLogger::OnPacketSent(
    const quic::SerializedPacket& serialized_packet,
    quic::QuicPacketNumber original_packet_number,
    quic::TransmissionType transmission_type,
    quic::QuicTime sent_time) {
  if (!net_log_is_capturing_)
    return;
  if (!original_packet_number.IsInitialized()) {
    net_log_.AddEvent(NetLogEventType::QUIC_SESSION_PACKET_SENT, [&] {
      return NetLogQuicPacketSentParams(serialized_packet, transmission_type,
                                        sent_time);
    });
  } else {
    net_log_.AddEvent(NetLogEventType::QUIC_SESSION_PACKET_RETRANSMITTED, [&] {
      return NetLogQuicPacketRetransmittedParams(
          original_packet_number, serialized_packet.packet_number);
    });
  }
}

void QuicConnectionLogger::OnPacketLoss(
    quic::QuicPacketNumber lost_packet_number,
    quic::TransmissionType transmission_type,
    quic::QuicTime detection_time) {
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_PACKET_LOST, [&] {
    return NetLogQuicPacketLostParams(lost_packet_number, transmission_type,
                                      detection_time);
  });
}

void QuicConnectionLogger::OnPingSent() {
  no_packet_received_after_ping_ = true;
}

void QuicConnectionLogger::OnPacketReceived(
    const quic::QuicSocketAddress& self_address,
    const quic::QuicSocketAddress& peer_address,
    const quic::QuicEncryptedPacket& packet) {
  if (local_address_from_self_.GetFamily() == ADDRESS_FAMILY_UNSPECIFIED) {
    local_address_from_self_ = ToIPEndPoint(self_address);
    UMA_HISTOGRAM_ENUMERATION(
        "Net.QuicSession.ConnectionTypeFromSelf",
        GetRealAddressFamily(ToIPEndPoint(self_address).address()),
        ADDRESS_FAMILY_LAST);
  }

  previous_received_packet_size_ = last_received_packet_size_;
  last_received_packet_size_ = packet.length();
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_PACKET_RECEIVED, [&] {
    return NetLogQuicPacketParams(self_address, peer_address, packet.length());
  });
}

void QuicConnectionLogger::OnUnauthenticatedHeader(
    const quic::QuicPacketHeader& header) {
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(
      NetLogEventType::QUIC_SESSION_UNAUTHENTICATED_PACKET_HEADER_RECEIVED,
      [&] { return NetLogQuicPacketHeaderParams(&header); });
}

void QuicConnectionLogger::OnIncorrectConnectionId(
    quic::QuicConnectionId connection_id) {
  ++num_incorrect_connection_ids_;
}

void QuicConnectionLogger::OnUndecryptablePacket() {
  ++num_undecryptable_packets_;
}

void QuicConnectionLogger::OnDuplicatePacket(
    quic::QuicPacketNumber packet_number) {
  ++num_duplicate_packets_;
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(
      NetLogEventType::QUIC_SESSION_DUPLICATE_PACKET_RECEIVED,
      [&] { return NetLogQuicDuplicatePacketParams(packet_number); });
}

void QuicConnectionLogger::OnProtocolVersionMismatch(
    quic::ParsedQuicVersion received_version) {
  // TODO(rtenneti): Add logging.
}

void QuicConnectionLogger::OnPacketHeader(
    const quic::QuicPacketHeader& header) {
  if (!first_received_packet_number_.IsInitialized()) {
    first_received_packet_number_ = header.packet_number;
  } else if (header.packet_number < first_received_packet_number_) {
    // Ignore packets with packet numbers less than
    // first_received_packet_number_.
    return;
  }
  ++num_packets_received_;
  if (!largest_received_packet_number_.IsInitialized()) {
    largest_received_packet_number_ = header.packet_number;
  } else if (largest_received_packet_number_ < header.packet_number) {
    uint64_t delta = header.packet_number - largest_received_packet_number_;
    if (delta > 1) {
      // There is a gap between the largest packet previously received and
      // the current packet.  This indicates either loss, or out-of-order
      // delivery.
      UMA_HISTOGRAM_COUNTS_1M(
          "Net.QuicSession.PacketGapReceived",
          static_cast<base::HistogramBase::Sample>(delta - 1));
    }
    largest_received_packet_number_ = header.packet_number;
  }
  if (header.packet_number - first_received_packet_number_ <
      received_packets_.size()) {
    received_packets_[header.packet_number - first_received_packet_number_] =
        true;
  }
  if (last_received_packet_number_.IsInitialized() &&
      header.packet_number < last_received_packet_number_) {
    ++num_out_of_order_received_packets_;
    if (previous_received_packet_size_ < last_received_packet_size_)
      ++num_out_of_order_large_received_packets_;
    UMA_HISTOGRAM_COUNTS_1M(
        "Net.QuicSession.OutOfOrderGapReceived",
        static_cast<base::HistogramBase::Sample>(last_received_packet_number_ -
                                                 header.packet_number));
  } else if (no_packet_received_after_ping_) {
    if (last_received_packet_number_.IsInitialized()) {
      UMA_HISTOGRAM_COUNTS_1M(
          "Net.QuicSession.PacketGapReceivedNearPing",
          static_cast<base::HistogramBase::Sample>(
              header.packet_number - last_received_packet_number_));
    }
    no_packet_received_after_ping_ = false;
  }
  last_received_packet_number_ = header.packet_number;
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_PACKET_AUTHENTICATED);
}

void QuicConnectionLogger::OnStreamFrame(const quic::QuicStreamFrame& frame) {
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_STREAM_FRAME_RECEIVED,
                    [&] { return NetLogQuicStreamFrameParams(frame); });
}

void QuicConnectionLogger::OnIncomingAck(
    quic::QuicPacketNumber ack_packet_number,
    const quic::QuicAckFrame& frame,
    quic::QuicTime ack_receive_time,
    quic::QuicPacketNumber largest_observed,
    bool rtt_updated,
    quic::QuicPacketNumber least_unacked_sent_packet) {
  const size_t kApproximateLargestSoloAckBytes = 100;
  if (last_received_packet_number_ - first_received_packet_number_ <
          received_acks_.size() &&
      last_received_packet_size_ < kApproximateLargestSoloAckBytes) {
    received_acks_[last_received_packet_number_ -
                   first_received_packet_number_] = true;
  }

  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_ACK_FRAME_RECEIVED,
                    [&] { return NetLogQuicAckFrameParams(&frame); });

  // TODO(rch, rtenneti) sort out histograms for QUIC_VERSION_34 and above.
}

void QuicConnectionLogger::OnStopWaitingFrame(
    const quic::QuicStopWaitingFrame& frame) {
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_STOP_WAITING_FRAME_RECEIVED,
                    [&] { return NetLogQuicStopWaitingFrameParams(&frame); });
}

void QuicConnectionLogger::OnRstStreamFrame(
    const quic::QuicRstStreamFrame& frame) {
  base::UmaHistogramSparse("Net.QuicSession.RstStreamErrorCodeServer",
                           frame.error_code);
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_RST_STREAM_FRAME_RECEIVED,
                    [&] { return NetLogQuicRstStreamFrameParams(&frame); });
}

void QuicConnectionLogger::OnConnectionCloseFrame(
    const quic::QuicConnectionCloseFrame& frame) {
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(
      NetLogEventType::QUIC_SESSION_CONNECTION_CLOSE_FRAME_RECEIVED,
      [&] { return NetLogQuicConnectionCloseFrameParams(&frame); });
}

void QuicConnectionLogger::OnWindowUpdateFrame(
    const quic::QuicWindowUpdateFrame& frame,
    const quic::QuicTime& receive_time) {
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_WINDOW_UPDATE_FRAME_RECEIVED,
                    [&] { return NetLogQuicWindowUpdateFrameParams(&frame); });
}

void QuicConnectionLogger::OnBlockedFrame(const quic::QuicBlockedFrame& frame) {
  ++num_blocked_frames_received_;
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_BLOCKED_FRAME_RECEIVED,
                    [&] { return NetLogQuicBlockedFrameParams(&frame); });
}

void QuicConnectionLogger::OnGoAwayFrame(const quic::QuicGoAwayFrame& frame) {
  UMA_HISTOGRAM_BOOLEAN("Net.QuicSession.GoAwayReceivedForConnectionMigration",
                        frame.error_code == quic::QUIC_ERROR_MIGRATING_PORT);

  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_GOAWAY_FRAME_RECEIVED,
                    [&] { return NetLogQuicGoAwayFrameParams(&frame); });
}

void QuicConnectionLogger::OnPingFrame(const quic::QuicPingFrame& frame) {
  // PingFrame has no contents to log, so just record that it was received.
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_PING_FRAME_RECEIVED);
}

void QuicConnectionLogger::OnPublicResetPacket(
    const quic::QuicPublicResetPacket& packet) {
  UpdatePublicResetAddressMismatchHistogram(
      local_address_from_shlo_, ToIPEndPoint(packet.client_address));
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_PUBLIC_RESET_PACKET_RECEIVED,
                    [&] {
                      return NetLogQuicPublicResetPacketParams(
                          local_address_from_shlo_, packet.client_address);
                    });
}

void QuicConnectionLogger::OnVersionNegotiationPacket(
    const quic::QuicVersionNegotiationPacket& packet) {
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(
      NetLogEventType::QUIC_SESSION_VERSION_NEGOTIATION_PACKET_RECEIVED,
      [&] { return NetLogQuicVersionNegotiationPacketParams(&packet); });
}

void QuicConnectionLogger::OnCryptoHandshakeMessageReceived(
    const quic::CryptoHandshakeMessage& message) {
  if (message.tag() == quic::kSHLO) {
    quic::QuicStringPiece address;
    quic::QuicSocketAddressCoder decoder;
    if (message.GetStringPiece(quic::kCADR, &address) &&
        decoder.Decode(address.data(), address.size())) {
      local_address_from_shlo_ =
          IPEndPoint(ToIPAddress(decoder.ip()), decoder.port());
      UMA_HISTOGRAM_ENUMERATION(
          "Net.QuicSession.ConnectionTypeFromPeer",
          GetRealAddressFamily(local_address_from_shlo_.address()),
          ADDRESS_FAMILY_LAST);

      int sample = GetAddressMismatch(local_address_from_shlo_,
                                      local_address_from_self_);
      // We are seemingly talking to an older server that does not support the
      // feature, so we can't report the results in the histogram.
      if (sample >= 0) {
        UMA_HISTOGRAM_ENUMERATION("Net.QuicSession.SelfShloAddressMismatch",
                                  static_cast<QuicAddressMismatch>(sample),
                                  QUIC_ADDRESS_MISMATCH_MAX);
      }
    }
  }
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(
      NetLogEventType::QUIC_SESSION_CRYPTO_HANDSHAKE_MESSAGE_RECEIVED,
      [&] { return NetLogQuicCryptoHandshakeMessageParams(&message); });
}

void QuicConnectionLogger::OnCryptoHandshakeMessageSent(
    const quic::CryptoHandshakeMessage& message) {
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(
      NetLogEventType::QUIC_SESSION_CRYPTO_HANDSHAKE_MESSAGE_SENT,
      [&] { return NetLogQuicCryptoHandshakeMessageParams(&message); });
}

void QuicConnectionLogger::OnConnectionClosed(
    const quic::QuicConnectionCloseFrame& frame,
    quic::ConnectionCloseSource source) {
  if (!net_log_is_capturing_)
    return;
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_CLOSED, [&] {
    return NetLogQuicOnConnectionClosedParams(frame.quic_error_code,
                                              frame.error_details, source);
  });
}

void QuicConnectionLogger::OnSuccessfulVersionNegotiation(
    const quic::ParsedQuicVersion& version) {
  if (!net_log_is_capturing_)
    return;
  string quic_version = QuicVersionToString(version.transport_version);
  net_log_.AddEventWithStringParams(
      NetLogEventType::QUIC_SESSION_VERSION_NEGOTIATED, "version",
      quic_version);
}

void QuicConnectionLogger::UpdateReceivedFrameCounts(
    quic::QuicStreamId stream_id,
    int num_frames_received,
    int num_duplicate_frames_received) {
  if (!quic::QuicUtils::IsCryptoStreamId(session_->transport_version(),
                                         stream_id)) {
    num_frames_received_ += num_frames_received;
    num_duplicate_frames_received_ += num_duplicate_frames_received;
  }
}

void QuicConnectionLogger::OnCertificateVerified(
    const CertVerifyResult& result) {
  if (!net_log_is_capturing_)
    return;
  if (result.cert_status == CERT_STATUS_INVALID) {
    net_log_.AddEvent(NetLogEventType::QUIC_SESSION_CERTIFICATE_VERIFY_FAILED);
    return;
  }
  net_log_.AddEvent(NetLogEventType::QUIC_SESSION_CERTIFICATE_VERIFIED, [&] {
    return NetLogQuicCertificateVerifiedParams(result.verified_cert);
  });
}

base::HistogramBase* QuicConnectionLogger::Get6PacketHistogram(
    const char* which_6) const {
  // This histogram takes a binary encoding of the 6 consecutive packets
  // received.  As a result, there are 64 possible sample-patterns.
  string prefix("Net.QuicSession.6PacketsPatternsReceived_");
  return base::LinearHistogram::FactoryGet(
      prefix + which_6 + connection_description_, 1, 64, 65,
      base::HistogramBase::kUmaTargetedHistogramFlag);
}

float QuicConnectionLogger::ReceivedPacketLossRate() const {
  if (!largest_received_packet_number_.IsInitialized())
    return 0.0f;
  float num_packets =
      largest_received_packet_number_ - first_received_packet_number_ + 1;
  float num_missing = num_packets - num_packets_received_;
  return num_missing / num_packets;
}

void QuicConnectionLogger::OnRttChanged(quic::QuicTime::Delta rtt) const {
  // Notify socket performance watcher of the updated RTT value.
  if (!socket_performance_watcher_)
    return;

  int64_t microseconds = rtt.ToMicroseconds();
  if (microseconds != 0 &&
      socket_performance_watcher_->ShouldNotifyUpdatedRTT()) {
    socket_performance_watcher_->OnUpdatedRTTAvailable(
        base::TimeDelta::FromMicroseconds(rtt.ToMicroseconds()));
  }
}

void QuicConnectionLogger::RecordAggregatePacketLossRate() const {
  // For short connections under 22 packets in length, we'll rely on the
  // Net.QuicSession.21CumulativePacketsReceived_* histogram to indicate packet
  // loss rates.  This way we avoid tremendously anomalous contributions to our
  // histogram.  (e.g., if we only got 5 packets, but lost 1, we'd otherwise
  // record a 20% loss in this histogram!). We may still get some strange data
  // (1 loss in 22 is still high :-/).
  if (!largest_received_packet_number_.IsInitialized() ||
      largest_received_packet_number_ - first_received_packet_number_ < 22)
    return;

  string prefix("Net.QuicSession.PacketLossRate_");
  base::HistogramBase* histogram = base::Histogram::FactoryGet(
      prefix + connection_description_, 1, 1000, 75,
      base::HistogramBase::kUmaTargetedHistogramFlag);
  histogram->Add(static_cast<base::HistogramBase::Sample>(
      ReceivedPacketLossRate() * 1000));
}

void QuicConnectionLogger::UpdateIsCapturing() {
  net_log_is_capturing_ = net_log_.IsCapturing();
}

}  // namespace net

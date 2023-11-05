// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_connection.h"

#include <string.h>
#include <sys/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/congestion_control/rtt_stats.h"
#include "quiche/quic/core/congestion_control/send_algorithm_interface.h"
#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/core/crypto/crypto_utils.h"
#include "quiche/quic/core/crypto/quic_decrypter.h"
#include "quiche/quic/core/crypto/quic_encrypter.h"
#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_packet_creator.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_path_validator.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_client_stats.h"
#include "quiche/quic/platform/api/quic_exported_stats.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_flag_utils.h"
#include "quiche/common/platform/api/quiche_testvalue.h"
#include "quiche/common/quiche_text_utils.h"

namespace quic {

class QuicDecrypter;
class QuicEncrypter;

namespace {

// Maximum number of consecutive sent nonretransmittable packets.
const QuicPacketCount kMaxConsecutiveNonRetransmittablePackets = 19;

// The minimum release time into future in ms.
const int kMinReleaseTimeIntoFutureMs = 1;

// The maximum number of recorded client addresses.
const size_t kMaxReceivedClientAddressSize = 20;

// An arbitrary limit on the number of PTOs before giving up on ECN, if no ECN-
// marked packet is acked. Avoids abandoning ECN because of one burst loss,
// but doesn't allow multiple RTTs of user delay in the hope of using ECN.
const uint8_t kEcnPtoLimit = 2;

// Base class of all alarms owned by a QuicConnection.
class QuicConnectionAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit QuicConnectionAlarmDelegate(QuicConnection* connection)
      : connection_(connection) {}
  QuicConnectionAlarmDelegate(const QuicConnectionAlarmDelegate&) = delete;
  QuicConnectionAlarmDelegate& operator=(const QuicConnectionAlarmDelegate&) =
      delete;

  QuicConnectionContext* GetConnectionContext() override {
    return (connection_ == nullptr) ? nullptr : connection_->context();
  }

 protected:
  QuicConnection* connection_;
};

// An alarm that is scheduled to send an ack if a timeout occurs.
class AckAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->ack_frame_updated());
    QUICHE_DCHECK(connection_->connected());
    QuicConnection::ScopedPacketFlusher flusher(connection_);
    if (connection_->SupportsMultiplePacketNumberSpaces()) {
      connection_->SendAllPendingAcks();
    } else {
      connection_->SendAck();
    }
  }
};

// This alarm will be scheduled any time a data-bearing packet is sent out.
// When the alarm goes off, the connection checks to see if the oldest packets
// have been acked, and retransmit them if they have not.
class RetransmissionAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    connection_->OnRetransmissionTimeout();
  }
};

// An alarm that is scheduled when the SentPacketManager requires a delay
// before sending packets and fires when the packet may be sent.
class SendAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    connection_->OnSendAlarm();
  }
};

class MtuDiscoveryAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    connection_->DiscoverMtu();
  }
};

class ProcessUndecryptablePacketsAlarmDelegate
    : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    QuicConnection::ScopedPacketFlusher flusher(connection_);
    connection_->MaybeProcessUndecryptablePackets();
  }
};

class DiscardPreviousOneRttKeysAlarmDelegate
    : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    connection_->DiscardPreviousOneRttKeys();
  }
};

class DiscardZeroRttDecryptionKeysAlarmDelegate
    : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    QUIC_DLOG(INFO) << "0-RTT discard alarm fired";
    connection_->RemoveDecrypter(ENCRYPTION_ZERO_RTT);
    connection_->RetireOriginalDestinationConnectionId();
  }
};

class MultiPortProbingAlarmDelegate : public QuicConnectionAlarmDelegate {
 public:
  using QuicConnectionAlarmDelegate::QuicConnectionAlarmDelegate;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    QUIC_DLOG(INFO) << "Alternative path probing alarm fired";
    connection_->MaybeProbeMultiPortPath();
  }
};

// When the clearer goes out of scope, the coalesced packet gets cleared.
class ScopedCoalescedPacketClearer {
 public:
  explicit ScopedCoalescedPacketClearer(QuicCoalescedPacket* coalesced)
      : coalesced_(coalesced) {}
  ~ScopedCoalescedPacketClearer() { coalesced_->Clear(); }

 private:
  QuicCoalescedPacket* coalesced_;  // Unowned.
};

// Whether this incoming packet is allowed to replace our connection ID.
bool PacketCanReplaceServerConnectionId(const QuicPacketHeader& header,
                                        Perspective perspective) {
  return perspective == Perspective::IS_CLIENT &&
         header.form == IETF_QUIC_LONG_HEADER_PACKET &&
         header.version.IsKnown() &&
         header.version.AllowsVariableLengthConnectionIds() &&
         (header.long_packet_type == INITIAL ||
          header.long_packet_type == RETRY);
}

// Due to a lost Initial packet, a Handshake packet might use a new connection
// ID we haven't seen before. We shouldn't update the connection ID based on
// this, but should buffer the packet in case it works out.
bool NewServerConnectionIdMightBeValid(const QuicPacketHeader& header,
                                       Perspective perspective,
                                       bool connection_id_already_replaced) {
  return perspective == Perspective::IS_CLIENT &&
         header.form == IETF_QUIC_LONG_HEADER_PACKET &&
         header.version.IsKnown() &&
         header.version.AllowsVariableLengthConnectionIds() &&
         header.long_packet_type == HANDSHAKE &&
         !connection_id_already_replaced;
}

CongestionControlType GetDefaultCongestionControlType() {
  if (GetQuicReloadableFlag(quic_default_to_bbr_v2)) {
    return kBBRv2;
  }

  if (GetQuicReloadableFlag(quic_default_to_bbr)) {
    return kBBR;
  }

  return kCubicBytes;
}

bool ContainsNonProbingFrame(const SerializedPacket& packet) {
  for (const QuicFrame& frame : packet.nonretransmittable_frames) {
    if (!QuicUtils::IsProbingFrame(frame.type)) {
      return true;
    }
  }
  for (const QuicFrame& frame : packet.retransmittable_frames) {
    if (!QuicUtils::IsProbingFrame(frame.type)) {
      return true;
    }
  }
  return false;
}

}  // namespace

#define ENDPOINT \
  (perspective_ == Perspective::IS_SERVER ? "Server: " : "Client: ")

QuicConnection::QuicConnection(
    QuicConnectionId server_connection_id,
    QuicSocketAddress initial_self_address,
    QuicSocketAddress initial_peer_address,
    QuicConnectionHelperInterface* helper, QuicAlarmFactory* alarm_factory,
    QuicPacketWriter* writer, bool owns_writer, Perspective perspective,
    const ParsedQuicVersionVector& supported_versions,
    ConnectionIdGeneratorInterface& generator)
    : framer_(supported_versions, helper->GetClock()->ApproximateNow(),
              perspective, server_connection_id.length()),
      current_packet_content_(NO_FRAMES_RECEIVED),
      is_current_packet_connectivity_probing_(false),
      has_path_challenge_in_current_packet_(false),
      current_effective_peer_migration_type_(NO_CHANGE),
      helper_(helper),
      alarm_factory_(alarm_factory),
      per_packet_options_(nullptr),
      writer_(writer),
      owns_writer_(owns_writer),
      encryption_level_(ENCRYPTION_INITIAL),
      clock_(helper->GetClock()),
      random_generator_(helper->GetRandomGenerator()),
      client_connection_id_is_set_(false),
      direct_peer_address_(initial_peer_address),
      default_path_(initial_self_address, QuicSocketAddress(),
                    /*client_connection_id=*/EmptyQuicConnectionId(),
                    server_connection_id,
                    /*stateless_reset_token=*/absl::nullopt),
      active_effective_peer_migration_type_(NO_CHANGE),
      support_key_update_for_connection_(false),
      current_packet_data_(nullptr),
      should_last_packet_instigate_acks_(false),
      max_undecryptable_packets_(0),
      max_tracked_packets_(GetQuicFlag(quic_max_tracked_packet_count)),
      idle_timeout_connection_close_behavior_(
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET),
      num_rtos_for_blackhole_detection_(0),
      uber_received_packet_manager_(&stats_),
      pending_retransmission_alarm_(false),
      defer_send_in_response_to_packets_(false),
      arena_(),
      ack_alarm_(alarm_factory_->CreateAlarm(arena_.New<AckAlarmDelegate>(this),
                                             &arena_)),
      retransmission_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<RetransmissionAlarmDelegate>(this), &arena_)),
      send_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<SendAlarmDelegate>(this), &arena_)),
      mtu_discovery_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<MtuDiscoveryAlarmDelegate>(this), &arena_)),
      process_undecryptable_packets_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<ProcessUndecryptablePacketsAlarmDelegate>(this), &arena_)),
      discard_previous_one_rtt_keys_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<DiscardPreviousOneRttKeysAlarmDelegate>(this), &arena_)),
      discard_zero_rtt_decryption_keys_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<DiscardZeroRttDecryptionKeysAlarmDelegate>(this),
          &arena_)),
      multi_port_probing_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<MultiPortProbingAlarmDelegate>(this), &arena_)),
      visitor_(nullptr),
      debug_visitor_(nullptr),
      packet_creator_(server_connection_id, &framer_, random_generator_, this),
      last_received_packet_info_(clock_->ApproximateNow()),
      sent_packet_manager_(perspective, clock_, random_generator_, &stats_,
                           GetDefaultCongestionControlType()),
      version_negotiated_(false),
      perspective_(perspective),
      connected_(true),
      can_truncate_connection_ids_(perspective == Perspective::IS_SERVER),
      mtu_probe_count_(0),
      previous_validated_mtu_(0),
      peer_max_packet_size_(kDefaultMaxPacketSizeTransportParam),
      largest_received_packet_size_(0),
      write_error_occurred_(false),
      consecutive_num_packets_with_no_retransmittable_frames_(0),
      max_consecutive_num_packets_with_no_retransmittable_frames_(
          kMaxConsecutiveNonRetransmittablePackets),
      bundle_retransmittable_with_pto_ack_(false),
      last_control_frame_id_(kInvalidControlFrameId),
      is_path_degrading_(false),
      processing_ack_frame_(false),
      supports_release_time_(false),
      release_time_into_future_(QuicTime::Delta::Zero()),
      blackhole_detector_(this, &arena_, alarm_factory_, &context_),
      idle_network_detector_(this, clock_->ApproximateNow(), &arena_,
                             alarm_factory_, &context_),
      path_validator_(alarm_factory_, &arena_, this, random_generator_, clock_,
                      &context_),
      ping_manager_(perspective, this, &arena_, alarm_factory_, &context_),
      multi_port_probing_interval_(kDefaultMultiPortProbingInterval),
      connection_id_generator_(generator),
      received_client_addresses_cache_(kMaxReceivedClientAddressSize) {
  QUICHE_DCHECK(perspective_ == Perspective::IS_CLIENT ||
                default_path_.self_address.IsInitialized());

  QUIC_DLOG(INFO) << ENDPOINT << "Created connection with server connection ID "
                  << server_connection_id
                  << " and version: " << ParsedQuicVersionToString(version());

  QUIC_BUG_IF(quic_bug_12714_2, !QuicUtils::IsConnectionIdValidForVersion(
                                    server_connection_id, transport_version()))
      << "QuicConnection: attempted to use server connection ID "
      << server_connection_id << " which is invalid with version " << version();
  framer_.set_visitor(this);
  stats_.connection_creation_time = clock_->ApproximateNow();
  // TODO(ianswett): Supply the NetworkChangeVisitor as a constructor argument
  // and make it required non-null, because it's always used.
  sent_packet_manager_.SetNetworkChangeVisitor(this);
  if (GetQuicRestartFlag(quic_offload_pacing_to_usps2)) {
    sent_packet_manager_.SetPacingAlarmGranularity(QuicTime::Delta::Zero());
    release_time_into_future_ =
        QuicTime::Delta::FromMilliseconds(kMinReleaseTimeIntoFutureMs);
  }
  // Allow the packet writer to potentially reduce the packet size to a value
  // even smaller than kDefaultMaxPacketSize.
  SetMaxPacketLength(perspective_ == Perspective::IS_SERVER
                         ? kDefaultServerMaxPacketSize
                         : kDefaultMaxPacketSize);
  uber_received_packet_manager_.set_max_ack_ranges(255);
  MaybeEnableMultiplePacketNumberSpacesSupport();
  QUICHE_DCHECK(perspective_ == Perspective::IS_CLIENT ||
                supported_versions.size() == 1);
  InstallInitialCrypters(default_path_.server_connection_id);

  // On the server side, version negotiation has been done by the dispatcher,
  // and the server connection is created with the right version.
  if (perspective_ == Perspective::IS_SERVER) {
    version_negotiated_ = true;
  }
  if (default_enable_5rto_blackhole_detection_) {
    num_rtos_for_blackhole_detection_ = 5;
    if (GetQuicReloadableFlag(quic_disable_server_blackhole_detection) &&
        perspective_ == Perspective::IS_SERVER) {
      QUIC_RELOADABLE_FLAG_COUNT(quic_disable_server_blackhole_detection);
      blackhole_detection_disabled_ = true;
    }
  }
  if (perspective_ == Perspective::IS_CLIENT) {
    AddKnownServerAddress(initial_peer_address);
  }
  packet_creator_.SetDefaultPeerAddress(initial_peer_address);
  if (ignore_duplicate_new_cid_frame_) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_ignore_duplicate_new_cid_frame);
  }
}

void QuicConnection::InstallInitialCrypters(QuicConnectionId connection_id) {
  CrypterPair crypters;
  CryptoUtils::CreateInitialObfuscators(perspective_, version(), connection_id,
                                        &crypters);
  SetEncrypter(ENCRYPTION_INITIAL, std::move(crypters.encrypter));
  if (version().KnowsWhichDecrypterToUse()) {
    InstallDecrypter(ENCRYPTION_INITIAL, std::move(crypters.decrypter));
  } else {
    SetDecrypter(ENCRYPTION_INITIAL, std::move(crypters.decrypter));
  }
}

QuicConnection::~QuicConnection() {
  QUICHE_DCHECK_GE(stats_.max_egress_mtu, long_term_mtu_);
  if (owns_writer_) {
    delete writer_;
  }
  ClearQueuedPackets();
  if (stats_
          .num_tls_server_zero_rtt_packets_received_after_discarding_decrypter >
      0) {
    QUIC_CODE_COUNT_N(
        quic_server_received_tls_zero_rtt_packet_after_discarding_decrypter, 2,
        3);
  } else {
    QUIC_CODE_COUNT_N(
        quic_server_received_tls_zero_rtt_packet_after_discarding_decrypter, 3,
        3);
  }
}

void QuicConnection::ClearQueuedPackets() { buffered_packets_.clear(); }

bool QuicConnection::ValidateConfigConnectionIds(const QuicConfig& config) {
  QUICHE_DCHECK(config.negotiated());
  if (!version().UsesTls()) {
    // QUIC+TLS is required to transmit connection ID transport parameters.
    return true;
  }
  // This function validates connection IDs as defined in IETF draft-28 and
  // later.

  // Validate initial_source_connection_id.
  QuicConnectionId expected_initial_source_connection_id;
  if (perspective_ == Perspective::IS_CLIENT) {
    expected_initial_source_connection_id = default_path_.server_connection_id;
  } else {
    expected_initial_source_connection_id = default_path_.client_connection_id;
  }
  if (!config.HasReceivedInitialSourceConnectionId() ||
      config.ReceivedInitialSourceConnectionId() !=
          expected_initial_source_connection_id) {
    std::string received_value;
    if (config.HasReceivedInitialSourceConnectionId()) {
      received_value = config.ReceivedInitialSourceConnectionId().ToString();
    } else {
      received_value = "none";
    }
    std::string error_details =
        absl::StrCat("Bad initial_source_connection_id: expected ",
                     expected_initial_source_connection_id.ToString(),
                     ", received ", received_value);
    CloseConnection(IETF_QUIC_PROTOCOL_VIOLATION, error_details,
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  if (perspective_ == Perspective::IS_CLIENT) {
    // Validate original_destination_connection_id.
    if (!config.HasReceivedOriginalConnectionId() ||
        config.ReceivedOriginalConnectionId() !=
            GetOriginalDestinationConnectionId()) {
      std::string received_value;
      if (config.HasReceivedOriginalConnectionId()) {
        received_value = config.ReceivedOriginalConnectionId().ToString();
      } else {
        received_value = "none";
      }
      std::string error_details =
          absl::StrCat("Bad original_destination_connection_id: expected ",
                       GetOriginalDestinationConnectionId().ToString(),
                       ", received ", received_value);
      CloseConnection(IETF_QUIC_PROTOCOL_VIOLATION, error_details,
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return false;
    }
    // Validate retry_source_connection_id.
    if (retry_source_connection_id_.has_value()) {
      // We received a RETRY packet, validate that the retry source
      // connection ID from the config matches the one from the RETRY.
      if (!config.HasReceivedRetrySourceConnectionId() ||
          config.ReceivedRetrySourceConnectionId() !=
              retry_source_connection_id_.value()) {
        std::string received_value;
        if (config.HasReceivedRetrySourceConnectionId()) {
          received_value = config.ReceivedRetrySourceConnectionId().ToString();
        } else {
          received_value = "none";
        }
        std::string error_details =
            absl::StrCat("Bad retry_source_connection_id: expected ",
                         retry_source_connection_id_.value().ToString(),
                         ", received ", received_value);
        CloseConnection(IETF_QUIC_PROTOCOL_VIOLATION, error_details,
                        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
        return false;
      }
    } else {
      // We did not receive a RETRY packet, make sure we did not receive the
      // retry_source_connection_id transport parameter.
      if (config.HasReceivedRetrySourceConnectionId()) {
        std::string error_details = absl::StrCat(
            "Bad retry_source_connection_id: did not receive RETRY but "
            "received ",
            config.ReceivedRetrySourceConnectionId().ToString());
        CloseConnection(IETF_QUIC_PROTOCOL_VIOLATION, error_details,
                        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
        return false;
      }
    }
  }
  return true;
}

void QuicConnection::SetFromConfig(const QuicConfig& config) {
  if (config.negotiated()) {
    // Handshake complete, set handshake timeout to Infinite.
    SetNetworkTimeouts(QuicTime::Delta::Infinite(),
                       config.IdleNetworkTimeout());
    idle_timeout_connection_close_behavior_ =
        ConnectionCloseBehavior::SILENT_CLOSE;
    if (perspective_ == Perspective::IS_SERVER) {
      idle_timeout_connection_close_behavior_ = ConnectionCloseBehavior::
          SILENT_CLOSE_WITH_CONNECTION_CLOSE_PACKET_SERIALIZED;
    }
    if (config.HasClientRequestedIndependentOption(kNSLC, perspective_)) {
      idle_timeout_connection_close_behavior_ =
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET;
    }
    if (!ValidateConfigConnectionIds(config)) {
      return;
    }
    support_key_update_for_connection_ = version().UsesTls();
    framer_.SetKeyUpdateSupportForConnection(
        support_key_update_for_connection_);
  } else {
    SetNetworkTimeouts(config.max_time_before_crypto_handshake(),
                       config.max_idle_time_before_crypto_handshake());
  }

  if (version().HasIetfQuicFrames() &&
      config.HasReceivedPreferredAddressConnectionIdAndToken()) {
    QuicNewConnectionIdFrame frame;
    std::tie(frame.connection_id, frame.stateless_reset_token) =
        config.ReceivedPreferredAddressConnectionIdAndToken();
    frame.sequence_number = 1u;
    frame.retire_prior_to = 0u;
    OnNewConnectionIdFrameInner(frame);
  }

  if (config.DisableConnectionMigration()) {
    active_migration_disabled_ = true;
  }

  sent_packet_manager_.SetFromConfig(config);
  if (perspective_ == Perspective::IS_SERVER &&
      config.HasClientSentConnectionOption(kAFF2, perspective_)) {
    send_ack_frequency_on_handshake_completion_ = true;
  }
  if (config.HasReceivedBytesForConnectionId() &&
      can_truncate_connection_ids_) {
    packet_creator_.SetServerConnectionIdLength(
        config.ReceivedBytesForConnectionId());
  }
  max_undecryptable_packets_ = config.max_undecryptable_packets();

  if (!GetQuicReloadableFlag(quic_enable_mtu_discovery_at_server)) {
    if (config.HasClientRequestedIndependentOption(kMTUH, perspective_)) {
      SetMtuDiscoveryTarget(kMtuDiscoveryTargetPacketSizeHigh);
    }
  }
  if (config.HasClientRequestedIndependentOption(kMTUL, perspective_)) {
    SetMtuDiscoveryTarget(kMtuDiscoveryTargetPacketSizeLow);
  }
  if (default_enable_5rto_blackhole_detection_) {
    if (config.HasClientRequestedIndependentOption(kCBHD, perspective_)) {
      QUIC_CODE_COUNT(quic_client_only_blackhole_detection);
      blackhole_detection_disabled_ = true;
    }
    if (config.HasClientSentConnectionOption(kNBHD, perspective_)) {
      blackhole_detection_disabled_ = true;
    }
  }

  if (config.HasClientRequestedIndependentOption(kFIDT, perspective_)) {
    idle_network_detector_.enable_shorter_idle_timeout_on_sent_packet();
  }
  if (perspective_ == Perspective::IS_CLIENT && version().HasIetfQuicFrames()) {
    // Only conduct those experiments in IETF QUIC because random packets may
    // elicit reset and gQUIC PUBLIC_RESET will cause connection close.
    if (config.HasClientRequestedIndependentOption(kROWF, perspective_)) {
      retransmittable_on_wire_behavior_ = SEND_FIRST_FORWARD_SECURE_PACKET;
    }
    if (config.HasClientRequestedIndependentOption(kROWR, perspective_)) {
      retransmittable_on_wire_behavior_ = SEND_RANDOM_BYTES;
    }
  }
  if (config.HasClientRequestedIndependentOption(k3AFF, perspective_)) {
    anti_amplification_factor_ = 3;
  }
  if (config.HasClientRequestedIndependentOption(k10AF, perspective_)) {
    anti_amplification_factor_ = 10;
  }

  if (GetQuicReloadableFlag(quic_enable_server_on_wire_ping) &&
      perspective_ == Perspective::IS_SERVER &&
      config.HasClientSentConnectionOption(kSRWP, perspective_)) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_enable_server_on_wire_ping);
    set_initial_retransmittable_on_wire_timeout(
        QuicTime::Delta::FromMilliseconds(200));
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnSetFromConfig(config);
  }
  uber_received_packet_manager_.SetFromConfig(config, perspective_);
  if (config.HasClientSentConnectionOption(k5RTO, perspective_)) {
    num_rtos_for_blackhole_detection_ = 5;
  }
  if (config.HasClientSentConnectionOption(k6PTO, perspective_) ||
      config.HasClientSentConnectionOption(k7PTO, perspective_) ||
      config.HasClientSentConnectionOption(k8PTO, perspective_)) {
    num_rtos_for_blackhole_detection_ = 5;
  }
  if (config.HasReceivedStatelessResetToken()) {
    default_path_.stateless_reset_token = config.ReceivedStatelessResetToken();
  }
  if (config.HasReceivedAckDelayExponent()) {
    framer_.set_peer_ack_delay_exponent(config.ReceivedAckDelayExponent());
  }
  if (config.HasClientSentConnectionOption(kEACK, perspective_)) {
    bundle_retransmittable_with_pto_ack_ = true;
  }
  if (config.HasClientSentConnectionOption(kDFER, perspective_)) {
    defer_send_in_response_to_packets_ = false;
  }

  if (config.HasClientRequestedIndependentOption(kINVC, perspective_)) {
    send_connection_close_for_invalid_version_ = true;
  }

  if (version().HasIetfQuicFrames() &&
      config.HasReceivedPreferredAddressConnectionIdAndToken() &&
      config.HasClientSentConnectionOption(kSPAD, perspective_)) {
    if (self_address().host().IsIPv4() &&
        config.HasReceivedIPv4AlternateServerAddress()) {
      received_server_preferred_address_ =
          config.ReceivedIPv4AlternateServerAddress();
    } else if (self_address().host().IsIPv6() &&
               config.HasReceivedIPv6AlternateServerAddress()) {
      received_server_preferred_address_ =
          config.ReceivedIPv6AlternateServerAddress();
    }
    if (received_server_preferred_address_.IsInitialized()) {
      QUICHE_DLOG(INFO) << ENDPOINT << "Received server preferred address: "
                        << received_server_preferred_address_;
      if (config.HasClientRequestedIndependentOption(kSPA2, perspective_)) {
        accelerated_server_preferred_address_ = true;
        visitor_->OnServerPreferredAddressAvailable(
            received_server_preferred_address_);
      }
    }
  }

  if (config.HasReceivedMaxPacketSize()) {
    peer_max_packet_size_ = config.ReceivedMaxPacketSize();
    packet_creator_.SetMaxPacketLength(
        GetLimitedMaxPacketSize(packet_creator_.max_packet_length()));
  }
  if (config.HasReceivedMaxDatagramFrameSize()) {
    packet_creator_.SetMaxDatagramFrameSize(
        config.ReceivedMaxDatagramFrameSize());
  }

  supports_release_time_ =
      writer_ != nullptr && writer_->SupportsReleaseTime() &&
      !config.HasClientSentConnectionOption(kNPCO, perspective_);

  if (supports_release_time_) {
    UpdateReleaseTimeIntoFuture();
  }

  if (perspective_ == Perspective::IS_CLIENT && version().HasIetfQuicFrames() &&
      config.HasClientRequestedIndependentOption(kMPQC, perspective_)) {
    multi_port_stats_ = std::make_unique<MultiPortStats>();
    if (config.HasClientRequestedIndependentOption(kMPQM, perspective_)) {
      multi_port_migration_enabled_ = true;
    }
  }
}

bool QuicConnection::MaybeTestLiveness() {
  QUICHE_DCHECK_EQ(perspective_, Perspective::IS_CLIENT);
  if (liveness_testing_disabled_ ||
      encryption_level_ != ENCRYPTION_FORWARD_SECURE) {
    return false;
  }
  const QuicTime idle_network_deadline =
      idle_network_detector_.GetIdleNetworkDeadline();
  if (!idle_network_deadline.IsInitialized()) {
    return false;
  }
  const QuicTime now = clock_->ApproximateNow();
  if (now > idle_network_deadline) {
    QUIC_DLOG(WARNING) << "Idle network deadline has passed";
    return false;
  }
  const QuicTime::Delta timeout = idle_network_deadline - now;
  if (2 * timeout > idle_network_detector_.idle_network_timeout()) {
    // Do not test liveness if timeout is > half timeout. This is used to
    // prevent an infinite loop for short idle timeout.
    return false;
  }
  if (!sent_packet_manager_.IsLessThanThreePTOs(timeout)) {
    return false;
  }
  QUIC_LOG_EVERY_N_SEC(INFO, 60)
      << "Testing liveness, idle_network_timeout: "
      << idle_network_detector_.idle_network_timeout()
      << ", timeout: " << timeout
      << ", Pto delay: " << sent_packet_manager_.GetPtoDelay()
      << ", smoothed_rtt: "
      << sent_packet_manager_.GetRttStats()->smoothed_rtt()
      << ", mean deviation: "
      << sent_packet_manager_.GetRttStats()->mean_deviation();
  SendConnectivityProbingPacket(writer_, peer_address());
  return true;
}

void QuicConnection::ApplyConnectionOptions(
    const QuicTagVector& connection_options) {
  sent_packet_manager_.ApplyConnectionOptions(connection_options);
}

void QuicConnection::OnSendConnectionState(
    const CachedNetworkParameters& cached_network_params) {
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnSendConnectionState(cached_network_params);
  }
}

void QuicConnection::OnReceiveConnectionState(
    const CachedNetworkParameters& cached_network_params) {
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnReceiveConnectionState(cached_network_params);
  }
}

void QuicConnection::ResumeConnectionState(
    const CachedNetworkParameters& cached_network_params,
    bool max_bandwidth_resumption) {
  sent_packet_manager_.ResumeConnectionState(cached_network_params,
                                             max_bandwidth_resumption);
}

void QuicConnection::SetMaxPacingRate(QuicBandwidth max_pacing_rate) {
  sent_packet_manager_.SetMaxPacingRate(max_pacing_rate);
}

void QuicConnection::AdjustNetworkParameters(
    const SendAlgorithmInterface::NetworkParams& params) {
  sent_packet_manager_.AdjustNetworkParameters(params);
}

void QuicConnection::SetLossDetectionTuner(
    std::unique_ptr<LossDetectionTunerInterface> tuner) {
  sent_packet_manager_.SetLossDetectionTuner(std::move(tuner));
}

void QuicConnection::OnConfigNegotiated() {
  sent_packet_manager_.OnConfigNegotiated();

  if (GetQuicReloadableFlag(quic_enable_mtu_discovery_at_server) &&
      perspective_ == Perspective::IS_SERVER) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_enable_mtu_discovery_at_server);
    SetMtuDiscoveryTarget(kMtuDiscoveryTargetPacketSizeHigh);
  }
}

QuicBandwidth QuicConnection::MaxPacingRate() const {
  return sent_packet_manager_.MaxPacingRate();
}

bool QuicConnection::SelectMutualVersion(
    const ParsedQuicVersionVector& available_versions) {
  // Try to find the highest mutual version by iterating over supported
  // versions, starting with the highest, and breaking out of the loop once we
  // find a matching version in the provided available_versions vector.
  const ParsedQuicVersionVector& supported_versions =
      framer_.supported_versions();
  for (size_t i = 0; i < supported_versions.size(); ++i) {
    const ParsedQuicVersion& version = supported_versions[i];
    if (std::find(available_versions.begin(), available_versions.end(),
                  version) != available_versions.end()) {
      framer_.set_version(version);
      return true;
    }
  }

  return false;
}

void QuicConnection::OnError(QuicFramer* framer) {
  // Packets that we can not or have not decrypted are dropped.
  // TODO(rch): add stats to measure this.
  if (!connected_ || !last_received_packet_info_.decrypted) {
    return;
  }
  CloseConnection(framer->error(), framer->detailed_error(),
                  ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicConnection::OnPacket() {
  last_received_packet_info_.decrypted = false;
}

bool QuicConnection::OnProtocolVersionMismatch(
    ParsedQuicVersion received_version) {
  QUIC_DLOG(INFO) << ENDPOINT << "Received packet with mismatched version "
                  << ParsedQuicVersionToString(received_version);
  if (perspective_ == Perspective::IS_CLIENT) {
    const std::string error_details = "Protocol version mismatch.";
    QUIC_BUG(quic_bug_10511_3) << ENDPOINT << error_details;
    CloseConnection(QUIC_INTERNAL_ERROR, error_details,
                    ConnectionCloseBehavior::SILENT_CLOSE);
  }

  // Server drops old packets that were sent by the client before the version
  // was negotiated.
  return false;
}

// Handles version negotiation for client connection.
void QuicConnection::OnVersionNegotiationPacket(
    const QuicVersionNegotiationPacket& packet) {
  // Check that any public reset packet with a different connection ID that was
  // routed to this QuicConnection has been redirected before control reaches
  // here.  (Check for a bug regression.)
  QUICHE_DCHECK_EQ(default_path_.server_connection_id, packet.connection_id);
  if (perspective_ == Perspective::IS_SERVER) {
    const std::string error_details =
        "Server received version negotiation packet.";
    QUIC_BUG(quic_bug_10511_4) << error_details;
    QUIC_CODE_COUNT(quic_tear_down_local_connection_on_version_negotiation);
    CloseConnection(QUIC_INTERNAL_ERROR, error_details,
                    ConnectionCloseBehavior::SILENT_CLOSE);
    return;
  }
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnVersionNegotiationPacket(packet);
  }

  if (version_negotiated_) {
    // Possibly a duplicate version negotiation packet.
    return;
  }

  if (std::find(packet.versions.begin(), packet.versions.end(), version()) !=
      packet.versions.end()) {
    const std::string error_details = absl::StrCat(
        "Server already supports client's version ",
        ParsedQuicVersionToString(version()),
        " and should have accepted the connection instead of sending {",
        ParsedQuicVersionVectorToString(packet.versions), "}.");
    QUIC_DLOG(WARNING) << error_details;
    CloseConnection(QUIC_INVALID_VERSION_NEGOTIATION_PACKET, error_details,
                    ConnectionCloseBehavior::SILENT_CLOSE);
    return;
  }

  server_supported_versions_ = packet.versions;
  CloseConnection(
      QUIC_INVALID_VERSION,
      absl::StrCat(
          "Client may support one of the versions in the server's list, but "
          "it's going to close the connection anyway. Supported versions: {",
          ParsedQuicVersionVectorToString(framer_.supported_versions()),
          "}, peer supported versions: {",
          ParsedQuicVersionVectorToString(packet.versions), "}"),
      send_connection_close_for_invalid_version_
          ? ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET
          : ConnectionCloseBehavior::SILENT_CLOSE);
}

// Handles retry for client connection.
void QuicConnection::OnRetryPacket(QuicConnectionId original_connection_id,
                                   QuicConnectionId new_connection_id,
                                   absl::string_view retry_token,
                                   absl::string_view retry_integrity_tag,
                                   absl::string_view retry_without_tag) {
  QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, perspective_);
  if (version().UsesTls()) {
    if (!CryptoUtils::ValidateRetryIntegrityTag(
            version(), default_path_.server_connection_id, retry_without_tag,
            retry_integrity_tag)) {
      QUIC_DLOG(ERROR) << "Ignoring RETRY with invalid integrity tag";
      return;
    }
  } else {
    if (original_connection_id != default_path_.server_connection_id) {
      QUIC_DLOG(ERROR) << "Ignoring RETRY with original connection ID "
                       << original_connection_id << " not matching expected "
                       << default_path_.server_connection_id << " token "
                       << absl::BytesToHexString(retry_token);
      return;
    }
  }
  framer_.set_drop_incoming_retry_packets(true);
  stats_.retry_packet_processed = true;
  QUIC_DLOG(INFO) << "Received RETRY, replacing connection ID "
                  << default_path_.server_connection_id << " with "
                  << new_connection_id << ", received token "
                  << absl::BytesToHexString(retry_token);
  if (!original_destination_connection_id_.has_value()) {
    original_destination_connection_id_ = default_path_.server_connection_id;
  }
  QUICHE_DCHECK(!retry_source_connection_id_.has_value())
      << retry_source_connection_id_.value();
  retry_source_connection_id_ = new_connection_id;
  ReplaceInitialServerConnectionId(new_connection_id);
  packet_creator_.SetRetryToken(retry_token);

  // Reinstall initial crypters because the connection ID changed.
  InstallInitialCrypters(default_path_.server_connection_id);

  sent_packet_manager_.MarkInitialPacketsForRetransmission();
}

void QuicConnection::SetOriginalDestinationConnectionId(
    const QuicConnectionId& original_destination_connection_id) {
  QUIC_DLOG(INFO) << "Setting original_destination_connection_id to "
                  << original_destination_connection_id
                  << " on connection with server_connection_id "
                  << default_path_.server_connection_id;
  QUICHE_DCHECK_NE(original_destination_connection_id,
                   default_path_.server_connection_id);
  InstallInitialCrypters(original_destination_connection_id);
  QUICHE_DCHECK(!original_destination_connection_id_.has_value())
      << original_destination_connection_id_.value();
  original_destination_connection_id_ = original_destination_connection_id;
  original_destination_connection_id_replacement_ =
      default_path_.server_connection_id;
}

QuicConnectionId QuicConnection::GetOriginalDestinationConnectionId() const {
  if (original_destination_connection_id_.has_value()) {
    return original_destination_connection_id_.value();
  }
  return default_path_.server_connection_id;
}

void QuicConnection::RetireOriginalDestinationConnectionId() {
  if (original_destination_connection_id_.has_value()) {
    visitor_->OnServerConnectionIdRetired(*original_destination_connection_id_);
    original_destination_connection_id_.reset();
  }
}

bool QuicConnection::ValidateServerConnectionId(
    const QuicPacketHeader& header) const {
  if (perspective_ == Perspective::IS_CLIENT &&
      header.form == IETF_QUIC_SHORT_HEADER_PACKET) {
    return true;
  }

  QuicConnectionId server_connection_id =
      GetServerConnectionIdAsRecipient(header, perspective_);

  if (server_connection_id == default_path_.server_connection_id ||
      server_connection_id == original_destination_connection_id_) {
    return true;
  }

  if (PacketCanReplaceServerConnectionId(header, perspective_)) {
    QUIC_DLOG(INFO) << ENDPOINT << "Accepting packet with new connection ID "
                    << server_connection_id << " instead of "
                    << default_path_.server_connection_id;
    return true;
  }

  if (version().HasIetfQuicFrames() && perspective_ == Perspective::IS_SERVER &&
      self_issued_cid_manager_ != nullptr &&
      self_issued_cid_manager_->IsConnectionIdInUse(server_connection_id)) {
    return true;
  }

  if (NewServerConnectionIdMightBeValid(
          header, perspective_, server_connection_id_replaced_by_initial_)) {
    return true;
  }

  return false;
}

bool QuicConnection::OnUnauthenticatedPublicHeader(
    const QuicPacketHeader& header) {
  last_received_packet_info_.destination_connection_id =
      header.destination_connection_id;
  // If last packet destination connection ID is the original server
  // connection ID chosen by client, replaces it with the connection ID chosen
  // by server.
  if (perspective_ == Perspective::IS_SERVER &&
      original_destination_connection_id_.has_value() &&
      last_received_packet_info_.destination_connection_id ==
          *original_destination_connection_id_) {
    last_received_packet_info_.destination_connection_id =
        original_destination_connection_id_replacement_;
  }

  // As soon as we receive an initial we start ignoring subsequent retries.
  if (header.version_flag && header.long_packet_type == INITIAL) {
    framer_.set_drop_incoming_retry_packets(true);
  }

  if (!ValidateServerConnectionId(header)) {
    ++stats_.packets_dropped;
    QuicConnectionId server_connection_id =
        GetServerConnectionIdAsRecipient(header, perspective_);
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Ignoring packet from unexpected server connection ID "
                    << server_connection_id << " instead of "
                    << default_path_.server_connection_id;
    if (debug_visitor_ != nullptr) {
      debug_visitor_->OnIncorrectConnectionId(server_connection_id);
    }
    QUICHE_DCHECK_NE(Perspective::IS_SERVER, perspective_);
    return false;
  }

  if (!version().SupportsClientConnectionIds()) {
    return true;
  }

  if (perspective_ == Perspective::IS_SERVER &&
      header.form == IETF_QUIC_SHORT_HEADER_PACKET) {
    return true;
  }

  QuicConnectionId client_connection_id =
      GetClientConnectionIdAsRecipient(header, perspective_);

  if (client_connection_id == default_path_.client_connection_id) {
    return true;
  }

  if (!client_connection_id_is_set_ && perspective_ == Perspective::IS_SERVER) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Setting client connection ID from first packet to "
                    << client_connection_id;
    set_client_connection_id(client_connection_id);
    return true;
  }

  if (version().HasIetfQuicFrames() && perspective_ == Perspective::IS_CLIENT &&
      self_issued_cid_manager_ != nullptr &&
      self_issued_cid_manager_->IsConnectionIdInUse(client_connection_id)) {
    return true;
  }

  ++stats_.packets_dropped;
  QUIC_DLOG(INFO) << ENDPOINT
                  << "Ignoring packet from unexpected client connection ID "
                  << client_connection_id << " instead of "
                  << default_path_.client_connection_id;
  return false;
}

bool QuicConnection::OnUnauthenticatedHeader(const QuicPacketHeader& header) {
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnUnauthenticatedHeader(header);
  }

  // Sanity check on the server connection ID in header.
  QUICHE_DCHECK(ValidateServerConnectionId(header));

  if (packet_creator_.HasPendingFrames()) {
    // Incoming packets may change a queued ACK frame.
    const std::string error_details =
        "Pending frames must be serialized before incoming packets are "
        "processed.";
    QUIC_BUG(quic_pending_frames_not_serialized)
        << error_details << ", received header: " << header;
    CloseConnection(QUIC_INTERNAL_ERROR, error_details,
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  return true;
}

void QuicConnection::OnSuccessfulVersionNegotiation() {
  visitor_->OnSuccessfulVersionNegotiation(version());
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnSuccessfulVersionNegotiation(version());
  }
}

void QuicConnection::OnSuccessfulMigration(bool is_port_change) {
  QUICHE_DCHECK_EQ(perspective_, Perspective::IS_CLIENT);
  if (IsPathDegrading() && !multi_port_stats_) {
    // If path was previously degrading, and migration is successful after
    // probing, restart the path degrading and blackhole detection.
    // In the case of multi-port, since the alt-path state is inferred from
    // historical data, we can't trust it until we receive data on the new path.
    OnForwardProgressMade();
  }
  if (IsAlternativePath(default_path_.self_address,
                        default_path_.peer_address)) {
    // Reset alternative path state even if it is still under validation.
    alternative_path_.Clear();
  }
  // TODO(b/159074035): notify SentPacketManger with RTT sample from probing.
  if (version().HasIetfQuicFrames() && !is_port_change) {
    sent_packet_manager_.OnConnectionMigration(/*reset_send_algorithm=*/true);
  }
}

void QuicConnection::OnTransportParametersSent(
    const TransportParameters& transport_parameters) const {
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnTransportParametersSent(transport_parameters);
  }
}

void QuicConnection::OnTransportParametersReceived(
    const TransportParameters& transport_parameters) const {
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnTransportParametersReceived(transport_parameters);
  }
}

void QuicConnection::OnTransportParametersResumed(
    const TransportParameters& transport_parameters) const {
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnTransportParametersResumed(transport_parameters);
  }
}

void QuicConnection::OnEncryptedClientHelloSent(
    absl::string_view client_hello) const {
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnEncryptedClientHelloSent(client_hello);
  }
}

void QuicConnection::OnEncryptedClientHelloReceived(
    absl::string_view client_hello) const {
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnEncryptedClientHelloReceived(client_hello);
  }
}

bool QuicConnection::HasPendingAcks() const { return ack_alarm_->IsSet(); }

void QuicConnection::OnUserAgentIdKnown(const std::string& /*user_agent_id*/) {
  sent_packet_manager_.OnUserAgentIdKnown();
}

void QuicConnection::OnDecryptedPacket(size_t /*length*/,
                                       EncryptionLevel level) {
  last_received_packet_info_.decrypted_level = level;
  last_received_packet_info_.decrypted = true;
  if (level == ENCRYPTION_FORWARD_SECURE &&
      !have_decrypted_first_one_rtt_packet_) {
    have_decrypted_first_one_rtt_packet_ = true;
    if (version().UsesTls() && perspective_ == Perspective::IS_SERVER) {
      // Servers MAY temporarily retain 0-RTT keys to allow decrypting reordered
      // packets without requiring their contents to be retransmitted with 1-RTT
      // keys. After receiving a 1-RTT packet, servers MUST discard 0-RTT keys
      // within a short time; the RECOMMENDED time period is three times the
      // Probe Timeout.
      // https://quicwg.org/base-drafts/draft-ietf-quic-tls.html#name-discarding-0-rtt-keys
      discard_zero_rtt_decryption_keys_alarm_->Set(
          clock_->ApproximateNow() + sent_packet_manager_.GetPtoDelay() * 3);
    }
  }
  if (EnforceAntiAmplificationLimit() && !IsHandshakeConfirmed() &&
      (level == ENCRYPTION_HANDSHAKE || level == ENCRYPTION_FORWARD_SECURE)) {
    // Address is validated by successfully processing a HANDSHAKE or 1-RTT
    // packet.
    default_path_.validated = true;
    stats_.address_validated_via_decrypting_packet = true;
  }
  idle_network_detector_.OnPacketReceived(
      last_received_packet_info_.receipt_time);

  visitor_->OnPacketDecrypted(level);
}

QuicSocketAddress QuicConnection::GetEffectivePeerAddressFromCurrentPacket()
    const {
  // By default, the connection is not proxied, and the effective peer address
  // is the packet's source address, i.e. the direct peer address.
  return last_received_packet_info_.source_address;
}

bool QuicConnection::OnPacketHeader(const QuicPacketHeader& header) {
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPacketHeader(header, clock_->ApproximateNow(),
                                   last_received_packet_info_.decrypted_level);
  }

  // Will be decremented below if we fall through to return true.
  ++stats_.packets_dropped;

  if (!ProcessValidatedPacket(header)) {
    return false;
  }

  // Initialize the current packet content state.
  current_packet_content_ = NO_FRAMES_RECEIVED;
  is_current_packet_connectivity_probing_ = false;
  has_path_challenge_in_current_packet_ = false;
  current_effective_peer_migration_type_ = NO_CHANGE;

  if (perspective_ == Perspective::IS_CLIENT) {
    if (!GetLargestReceivedPacket().IsInitialized() ||
        header.packet_number > GetLargestReceivedPacket()) {
      if (version().HasIetfQuicFrames()) {
        // Client processes packets from any known server address, but only
        // updates peer address on initialization and/or to validated server
        // preferred address.
      } else {
        // Update direct_peer_address_ and default path peer_address immediately
        // for client connections.
        // TODO(fayang): only change peer addresses in application data packet
        // number space.
        UpdatePeerAddress(last_received_packet_info_.source_address);
        default_path_.peer_address = GetEffectivePeerAddressFromCurrentPacket();
      }
    }
  } else {
    // At server, remember the address change type of effective_peer_address
    // in current_effective_peer_migration_type_. But this variable alone
    // doesn't necessarily starts a migration. A migration will be started
    // later, once the current packet is confirmed to meet the following
    // conditions:
    // 1) current_effective_peer_migration_type_ is not NO_CHANGE.
    // 2) The current packet is not a connectivity probing.
    // 3) The current packet is not reordered, i.e. its packet number is the
    //    largest of this connection so far.
    // Once the above conditions are confirmed, a new migration will start
    // even if there is an active migration underway.
    current_effective_peer_migration_type_ =
        QuicUtils::DetermineAddressChangeType(
            default_path_.peer_address,
            GetEffectivePeerAddressFromCurrentPacket());

    if (version().HasIetfQuicFrames()) {
      auto effective_peer_address = GetEffectivePeerAddressFromCurrentPacket();
      // Since server does not send new connection ID to client before handshake
      // completion and source connection ID is omitted in short header packet,
      // the server_connection_id on PathState on the server side does not
      // affect the packets server writes after handshake completion. On the
      // other hand, it is still desirable to have the "correct" server
      // connection ID set on path.
      // 1) If client uses 1 unique server connection ID per path and the packet
      // is received from an existing path, then
      // last_received_packet_info_.destination_connection_id will always be the
      // same as the server connection ID on path. Server side will maintain the
      // 1-to-1 mapping from server connection ID to path. 2) If client uses
      // multiple server connection IDs on the same path, compared to the
      // server_connection_id on path,
      // last_received_packet_info_.destination_connection_id has the advantage
      // that it is still present in the session map since the packet can be
      // routed here regardless of packet reordering.
      if (IsDefaultPath(last_received_packet_info_.destination_address,
                        effective_peer_address)) {
        default_path_.server_connection_id =
            last_received_packet_info_.destination_connection_id;
      } else if (IsAlternativePath(
                     last_received_packet_info_.destination_address,
                     effective_peer_address)) {
        alternative_path_.server_connection_id =
            last_received_packet_info_.destination_connection_id;
      }
    }

    if (last_received_packet_info_.destination_connection_id !=
            default_path_.server_connection_id &&
        (!original_destination_connection_id_.has_value() ||
         last_received_packet_info_.destination_connection_id !=
             *original_destination_connection_id_)) {
      QUIC_CODE_COUNT(quic_connection_id_change);
    }

    QUIC_DLOG_IF(INFO, current_effective_peer_migration_type_ != NO_CHANGE)
        << ENDPOINT << "Effective peer's ip:port changed from "
        << default_path_.peer_address.ToString() << " to "
        << GetEffectivePeerAddressFromCurrentPacket().ToString()
        << ", active_effective_peer_migration_type is "
        << active_effective_peer_migration_type_;
  }

  --stats_.packets_dropped;
  QUIC_DVLOG(1) << ENDPOINT << "Received packet header: " << header;
  last_received_packet_info_.header = header;
  if (!stats_.first_decrypted_packet.IsInitialized()) {
    stats_.first_decrypted_packet =
        last_received_packet_info_.header.packet_number;
  }

  switch (last_received_packet_info_.ecn_codepoint) {
    case ECN_NOT_ECT:
      break;
    case ECN_ECT0:
      stats_.num_ecn_marks_received.ect0++;
      break;
    case ECN_ECT1:
      stats_.num_ecn_marks_received.ect1++;
      break;
    case ECN_CE:
      stats_.num_ecn_marks_received.ce++;
      break;
  }

  // Record packet receipt to populate ack info before processing stream
  // frames, since the processing may result in sending a bundled ack.
  QuicTime receipt_time = idle_network_detector_.time_of_last_received_packet();
  if (SupportsMultiplePacketNumberSpaces()) {
    receipt_time = last_received_packet_info_.receipt_time;
  }
  uber_received_packet_manager_.RecordPacketReceived(
      last_received_packet_info_.decrypted_level,
      last_received_packet_info_.header, receipt_time,
      last_received_packet_info_.ecn_codepoint);
  if (EnforceAntiAmplificationLimit() && !IsHandshakeConfirmed() &&
      !header.retry_token.empty() &&
      visitor_->ValidateToken(header.retry_token)) {
    QUIC_DLOG(INFO) << ENDPOINT << "Address validated via token.";
    QUIC_CODE_COUNT(quic_address_validated_via_token);
    default_path_.validated = true;
    stats_.address_validated_via_token = true;
  }
  QUICHE_DCHECK(connected_);
  return true;
}

bool QuicConnection::OnStreamFrame(const QuicStreamFrame& frame) {
  QUIC_BUG_IF(quic_bug_12714_3, !connected_)
      << "Processing STREAM frame when connection is closed. Received packet "
         "info: "
      << last_received_packet_info_;

  // Since a stream frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  if (!UpdatePacketContent(STREAM_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnStreamFrame(frame);
  }
  if (!QuicUtils::IsCryptoStreamId(transport_version(), frame.stream_id) &&
      last_received_packet_info_.decrypted_level == ENCRYPTION_INITIAL) {
    if (MaybeConsiderAsMemoryCorruption(frame)) {
      CloseConnection(QUIC_MAYBE_CORRUPTED_MEMORY,
                      "Received crypto frame on non crypto stream.",
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return false;
    }

    QUIC_PEER_BUG(quic_peer_bug_10511_6)
        << ENDPOINT << "Received an unencrypted data frame: closing connection"
        << " packet_number:" << last_received_packet_info_.header.packet_number
        << " stream_id:" << frame.stream_id
        << " received_packets:" << ack_frame();
    CloseConnection(QUIC_UNENCRYPTED_STREAM_DATA,
                    "Unencrypted stream data seen.",
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  // TODO(fayang): Consider moving UpdatePacketContent and
  // MaybeUpdateAckTimeout to a stand-alone function instead of calling them for
  // all frames.
  MaybeUpdateAckTimeout();
  visitor_->OnStreamFrame(frame);
  stats_.stream_bytes_received += frame.data_length;
  ping_manager_.reset_consecutive_retransmittable_on_wire_count();
  return connected_;
}

bool QuicConnection::OnCryptoFrame(const QuicCryptoFrame& frame) {
  QUIC_BUG_IF(quic_bug_12714_4, !connected_)
      << "Processing CRYPTO frame when connection is closed. Received packet "
         "info: "
      << last_received_packet_info_;

  // Since a CRYPTO frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  if (!UpdatePacketContent(CRYPTO_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnCryptoFrame(frame);
  }
  MaybeUpdateAckTimeout();
  visitor_->OnCryptoFrame(frame);
  return connected_;
}

bool QuicConnection::OnAckFrameStart(QuicPacketNumber largest_acked,
                                     QuicTime::Delta ack_delay_time) {
  QUIC_BUG_IF(quic_bug_12714_5, !connected_)
      << "Processing ACK frame start when connection is closed. Received "
         "packet info: "
      << last_received_packet_info_;

  if (processing_ack_frame_) {
    CloseConnection(QUIC_INVALID_ACK_DATA,
                    "Received a new ack while processing an ack frame.",
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  // Since an ack frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  if (!UpdatePacketContent(ACK_FRAME)) {
    return false;
  }

  QUIC_DVLOG(1) << ENDPOINT
                << "OnAckFrameStart, largest_acked: " << largest_acked;

  if (GetLargestReceivedPacketWithAck().IsInitialized() &&
      last_received_packet_info_.header.packet_number <=
          GetLargestReceivedPacketWithAck()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received an old ack frame: ignoring";
    return true;
  }

  if (!sent_packet_manager_.GetLargestSentPacket().IsInitialized() ||
      largest_acked > sent_packet_manager_.GetLargestSentPacket()) {
    QUIC_DLOG(WARNING) << ENDPOINT
                       << "Peer's observed unsent packet:" << largest_acked
                       << " vs " << sent_packet_manager_.GetLargestSentPacket()
                       << ". SupportsMultiplePacketNumberSpaces():"
                       << SupportsMultiplePacketNumberSpaces()
                       << ", last_received_packet_info_.decrypted_level:"
                       << last_received_packet_info_.decrypted_level;
    // We got an ack for data we have not sent.
    CloseConnection(QUIC_INVALID_ACK_DATA, "Largest observed too high.",
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  processing_ack_frame_ = true;
  sent_packet_manager_.OnAckFrameStart(
      largest_acked, ack_delay_time,
      idle_network_detector_.time_of_last_received_packet());
  return true;
}

bool QuicConnection::OnAckRange(QuicPacketNumber start, QuicPacketNumber end) {
  QUIC_BUG_IF(quic_bug_12714_6, !connected_)
      << "Processing ACK frame range when connection is closed. Received "
         "packet info: "
      << last_received_packet_info_;
  QUIC_DVLOG(1) << ENDPOINT << "OnAckRange: [" << start << ", " << end << ")";

  if (GetLargestReceivedPacketWithAck().IsInitialized() &&
      last_received_packet_info_.header.packet_number <=
          GetLargestReceivedPacketWithAck()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received an old ack frame: ignoring";
    return true;
  }

  sent_packet_manager_.OnAckRange(start, end);
  return true;
}

bool QuicConnection::OnAckTimestamp(QuicPacketNumber packet_number,
                                    QuicTime timestamp) {
  QUIC_BUG_IF(quic_bug_10511_7, !connected_)
      << "Processing ACK frame time stamp when connection is closed. Received "
         "packet info: "
      << last_received_packet_info_;
  QUIC_DVLOG(1) << ENDPOINT << "OnAckTimestamp: [" << packet_number << ", "
                << timestamp.ToDebuggingValue() << ")";

  if (GetLargestReceivedPacketWithAck().IsInitialized() &&
      last_received_packet_info_.header.packet_number <=
          GetLargestReceivedPacketWithAck()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received an old ack frame: ignoring";
    return true;
  }

  sent_packet_manager_.OnAckTimestamp(packet_number, timestamp);
  return true;
}

bool QuicConnection::OnAckFrameEnd(
    QuicPacketNumber start, const absl::optional<QuicEcnCounts>& ecn_counts) {
  QUIC_BUG_IF(quic_bug_12714_7, !connected_)
      << "Processing ACK frame end when connection is closed. Received packet "
         "info: "
      << last_received_packet_info_;
  QUIC_DVLOG(1) << ENDPOINT << "OnAckFrameEnd, start: " << start;

  if (GetLargestReceivedPacketWithAck().IsInitialized() &&
      last_received_packet_info_.header.packet_number <=
          GetLargestReceivedPacketWithAck()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received an old ack frame: ignoring";
    return true;
  }
  const bool one_rtt_packet_was_acked =
      sent_packet_manager_.one_rtt_packet_acked();
  const bool zero_rtt_packet_was_acked =
      sent_packet_manager_.zero_rtt_packet_acked();
  const AckResult ack_result = sent_packet_manager_.OnAckFrameEnd(
      idle_network_detector_.time_of_last_received_packet(),
      last_received_packet_info_.header.packet_number,
      last_received_packet_info_.decrypted_level, ecn_counts);
  if (ack_result != PACKETS_NEWLY_ACKED &&
      ack_result != NO_PACKETS_NEWLY_ACKED) {
    // Error occurred (e.g., this ACK tries to ack packets in wrong packet
    // number space), and this would cause the connection to be closed.
    QUIC_DLOG(ERROR) << ENDPOINT
                     << "Error occurred when processing an ACK frame: "
                     << QuicUtils::AckResultToString(ack_result);
    return false;
  }
  if (SupportsMultiplePacketNumberSpaces() && !one_rtt_packet_was_acked &&
      sent_packet_manager_.one_rtt_packet_acked()) {
    visitor_->OnOneRttPacketAcknowledged();
  }
  if (debug_visitor_ != nullptr && version().UsesTls() &&
      !zero_rtt_packet_was_acked &&
      sent_packet_manager_.zero_rtt_packet_acked()) {
    debug_visitor_->OnZeroRttPacketAcked();
  }
  // Cancel the send alarm because new packets likely have been acked, which
  // may change the congestion window and/or pacing rate.  Canceling the alarm
  // causes CanWrite to recalculate the next send time.
  if (send_alarm_->IsSet()) {
    send_alarm_->Cancel();
  }
  if (supports_release_time_) {
    // Update pace time into future because smoothed RTT is likely updated.
    UpdateReleaseTimeIntoFuture();
  }
  SetLargestReceivedPacketWithAck(
      last_received_packet_info_.header.packet_number);
  PostProcessAfterAckFrame(ack_result == PACKETS_NEWLY_ACKED);
  processing_ack_frame_ = false;
  return connected_;
}

bool QuicConnection::OnStopWaitingFrame(const QuicStopWaitingFrame& /*frame*/) {
  QUIC_BUG_IF(quic_bug_12714_8, !connected_)
      << "Processing STOP_WAITING frame when connection is closed. Received "
         "packet info: "
      << last_received_packet_info_;

  // Since a stop waiting frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  if (!UpdatePacketContent(STOP_WAITING_FRAME)) {
    return false;
  }
  return connected_;
}

bool QuicConnection::OnPaddingFrame(const QuicPaddingFrame& frame) {
  QUIC_BUG_IF(quic_bug_12714_9, !connected_)
      << "Processing PADDING frame when connection is closed. Received packet "
         "info: "
      << last_received_packet_info_;
  if (!UpdatePacketContent(PADDING_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPaddingFrame(frame);
  }
  return true;
}

bool QuicConnection::OnPingFrame(const QuicPingFrame& frame) {
  QUIC_BUG_IF(quic_bug_12714_10, !connected_)
      << "Processing PING frame when connection is closed. Received packet "
         "info: "
      << last_received_packet_info_;
  if (!UpdatePacketContent(PING_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    QuicTime::Delta ping_received_delay = QuicTime::Delta::Zero();
    const QuicTime now = clock_->ApproximateNow();
    if (now > stats_.connection_creation_time) {
      ping_received_delay = now - stats_.connection_creation_time;
    }
    debug_visitor_->OnPingFrame(frame, ping_received_delay);
  }
  MaybeUpdateAckTimeout();
  return true;
}

bool QuicConnection::OnRstStreamFrame(const QuicRstStreamFrame& frame) {
  QUIC_BUG_IF(quic_bug_12714_11, !connected_)
      << "Processing RST_STREAM frame when connection is closed. Received "
         "packet info: "
      << last_received_packet_info_;

  // Since a reset stream frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  if (!UpdatePacketContent(RST_STREAM_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnRstStreamFrame(frame);
  }
  QUIC_DLOG(INFO) << ENDPOINT
                  << "RST_STREAM_FRAME received for stream: " << frame.stream_id
                  << " with error: "
                  << QuicRstStreamErrorCodeToString(frame.error_code);
  MaybeUpdateAckTimeout();
  visitor_->OnRstStream(frame);
  return connected_;
}

bool QuicConnection::OnStopSendingFrame(const QuicStopSendingFrame& frame) {
  QUIC_BUG_IF(quic_bug_12714_12, !connected_)
      << "Processing STOP_SENDING frame when connection is closed. Received "
         "packet info: "
      << last_received_packet_info_;

  // Since a reset stream frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  if (!UpdatePacketContent(STOP_SENDING_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnStopSendingFrame(frame);
  }

  QUIC_DLOG(INFO) << ENDPOINT << "STOP_SENDING frame received for stream: "
                  << frame.stream_id
                  << " with error: " << frame.ietf_error_code;
  MaybeUpdateAckTimeout();
  visitor_->OnStopSendingFrame(frame);
  return connected_;
}

class ReversePathValidationContext : public QuicPathValidationContext {
 public:
  ReversePathValidationContext(const QuicSocketAddress& self_address,
                               const QuicSocketAddress& peer_address,
                               const QuicSocketAddress& effective_peer_address,
                               QuicConnection* connection)
      : QuicPathValidationContext(self_address, peer_address,
                                  effective_peer_address),
        connection_(connection) {}

  QuicPacketWriter* WriterToUse() override { return connection_->writer(); }

 private:
  QuicConnection* connection_;
};

bool QuicConnection::OnPathChallengeFrame(const QuicPathChallengeFrame& frame) {
  QUIC_BUG_IF(quic_bug_10511_8, !connected_)
      << "Processing PATH_CHALLENGE frame when connection is closed. Received "
         "packet info: "
      << last_received_packet_info_;
  if (has_path_challenge_in_current_packet_) {
    // Only respond to the 1st PATH_CHALLENGE in the packet.
    return true;
  }
  should_proactively_validate_peer_address_on_path_challenge_ = false;
  // UpdatePacketContent() may start reverse path validation.
  if (!UpdatePacketContent(PATH_CHALLENGE_FRAME)) {
    return false;
  }
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPathChallengeFrame(frame);
  }
  // On the server side, send response to the source address of the current
  // incoming packet according to RFC9000.
  // On the client side, send response to the default peer address which
  // should be on an existing path with a pre-assigned a destination CID.
  const QuicSocketAddress effective_peer_address_to_respond =
      perspective_ == Perspective::IS_CLIENT
          ? effective_peer_address()
          : GetEffectivePeerAddressFromCurrentPacket();
  const QuicSocketAddress direct_peer_address_to_respond =
      perspective_ == Perspective::IS_CLIENT
          ? direct_peer_address_
          : last_received_packet_info_.source_address;
  QuicConnectionId client_cid, server_cid;
  FindOnPathConnectionIds(last_received_packet_info_.destination_address,
                          effective_peer_address_to_respond, &client_cid,
                          &server_cid);
  {
    QuicPacketCreator::ScopedPeerAddressContext context(
        &packet_creator_, direct_peer_address_to_respond, client_cid,
        server_cid);
    if (should_proactively_validate_peer_address_on_path_challenge_) {
      // Conditions to proactively validate peer address:
      // The perspective is server
      // The PATH_CHALLENGE is received on an unvalidated alternative path.
      // The connection isn't validating migrated peer address, which is of
      // higher prority.
      QUIC_DVLOG(1) << "Proactively validate the effective peer address "
                    << effective_peer_address_to_respond;
      QUIC_CODE_COUNT_N(quic_kick_off_client_address_validation, 2, 6);
      ValidatePath(
          std::make_unique<ReversePathValidationContext>(
              default_path_.self_address, direct_peer_address_to_respond,
              effective_peer_address_to_respond, this),
          std::make_unique<ReversePathValidationResultDelegate>(this,
                                                                peer_address()),
          PathValidationReason::kReversePathValidation);
    }
    has_path_challenge_in_current_packet_ = true;
    MaybeUpdateAckTimeout();
    // Queue or send PATH_RESPONSE.
    if (!SendPathResponse(frame.data_buffer, direct_peer_address_to_respond,
                          effective_peer_address_to_respond)) {
      QUIC_CODE_COUNT(quic_failed_to_send_path_response);
    }
    // TODO(b/150095588): change the stats to
    // num_valid_path_challenge_received.
    ++stats_.num_connectivity_probing_received;

    // Flushing packet creator might cause connection to be closed.
  }
  return connected_;
}

bool QuicConnection::OnPathResponseFrame(const QuicPathResponseFrame& frame) {
  QUIC_BUG_IF(quic_bug_10511_9, !connected_)
      << "Processing PATH_RESPONSE frame when connection is closed. Received "
         "packet info: "
      << last_received_packet_info_;
  ++stats_.num_path_response_received;
  if (!UpdatePacketContent(PATH_RESPONSE_FRAME)) {
    return false;
  }
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPathResponseFrame(frame);
  }
  MaybeUpdateAckTimeout();
  path_validator_.OnPathResponse(
      frame.data_buffer, last_received_packet_info_.destination_address);
  return connected_;
}

bool QuicConnection::OnConnectionCloseFrame(
    const QuicConnectionCloseFrame& frame) {
  QUIC_BUG_IF(quic_bug_10511_10, !connected_)
      << "Processing CONNECTION_CLOSE frame when connection is closed. "
         "Received packet info: "
      << last_received_packet_info_;

  // Since a connection close frame was received, this is not a connectivity
  // probe. A probe only contains a PING and full padding.
  if (!UpdatePacketContent(CONNECTION_CLOSE_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnConnectionCloseFrame(frame);
  }
  switch (frame.close_type) {
    case GOOGLE_QUIC_CONNECTION_CLOSE:
      QUIC_DLOG(INFO) << ENDPOINT << "Received ConnectionClose for connection: "
                      << connection_id() << ", with error: "
                      << QuicErrorCodeToString(frame.quic_error_code) << " ("
                      << frame.error_details << ")";
      break;
    case IETF_QUIC_TRANSPORT_CONNECTION_CLOSE:
      QUIC_DLOG(INFO) << ENDPOINT
                      << "Received Transport ConnectionClose for connection: "
                      << connection_id() << ", with error: "
                      << QuicErrorCodeToString(frame.quic_error_code) << " ("
                      << frame.error_details << ")"
                      << ", transport error code: "
                      << QuicIetfTransportErrorCodeString(
                             static_cast<QuicIetfTransportErrorCodes>(
                                 frame.wire_error_code))
                      << ", error frame type: "
                      << frame.transport_close_frame_type;
      break;
    case IETF_QUIC_APPLICATION_CONNECTION_CLOSE:
      QUIC_DLOG(INFO) << ENDPOINT
                      << "Received Application ConnectionClose for connection: "
                      << connection_id() << ", with error: "
                      << QuicErrorCodeToString(frame.quic_error_code) << " ("
                      << frame.error_details << ")"
                      << ", application error code: " << frame.wire_error_code;
      break;
  }

  if (frame.quic_error_code == QUIC_BAD_MULTIPATH_FLAG) {
    QUIC_LOG_FIRST_N(ERROR, 10)
        << "Unexpected QUIC_BAD_MULTIPATH_FLAG error."
        << " last_received_header: " << last_received_packet_info_.header
        << " encryption_level: " << encryption_level_;
  }
  TearDownLocalConnectionState(frame, ConnectionCloseSource::FROM_PEER);
  return connected_;
}

bool QuicConnection::OnMaxStreamsFrame(const QuicMaxStreamsFrame& frame) {
  QUIC_BUG_IF(quic_bug_12714_13, !connected_)
      << "Processing MAX_STREAMS frame when connection is closed. Received "
         "packet info: "
      << last_received_packet_info_;
  if (!UpdatePacketContent(MAX_STREAMS_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnMaxStreamsFrame(frame);
  }
  MaybeUpdateAckTimeout();
  return visitor_->OnMaxStreamsFrame(frame) && connected_;
}

bool QuicConnection::OnStreamsBlockedFrame(
    const QuicStreamsBlockedFrame& frame) {
  QUIC_BUG_IF(quic_bug_10511_11, !connected_)
      << "Processing STREAMS_BLOCKED frame when connection is closed. Received "
         "packet info: "
      << last_received_packet_info_;
  if (!UpdatePacketContent(STREAMS_BLOCKED_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnStreamsBlockedFrame(frame);
  }
  MaybeUpdateAckTimeout();
  return visitor_->OnStreamsBlockedFrame(frame) && connected_;
}

bool QuicConnection::OnGoAwayFrame(const QuicGoAwayFrame& frame) {
  QUIC_BUG_IF(quic_bug_12714_14, !connected_)
      << "Processing GOAWAY frame when connection is closed. Received packet "
         "info: "
      << last_received_packet_info_;

  // Since a go away frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  if (!UpdatePacketContent(GOAWAY_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnGoAwayFrame(frame);
  }
  QUIC_DLOG(INFO) << ENDPOINT << "GOAWAY_FRAME received with last good stream: "
                  << frame.last_good_stream_id
                  << " and error: " << QuicErrorCodeToString(frame.error_code)
                  << " and reason: " << frame.reason_phrase;
  MaybeUpdateAckTimeout();
  visitor_->OnGoAway(frame);
  return connected_;
}

bool QuicConnection::OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) {
  QUIC_BUG_IF(quic_bug_10511_12, !connected_)
      << "Processing WINDOW_UPDATE frame when connection is closed. Received "
         "packet info: "
      << last_received_packet_info_;

  // Since a window update frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  if (!UpdatePacketContent(WINDOW_UPDATE_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnWindowUpdateFrame(
        frame, idle_network_detector_.time_of_last_received_packet());
  }
  QUIC_DVLOG(1) << ENDPOINT << "WINDOW_UPDATE_FRAME received " << frame;
  MaybeUpdateAckTimeout();
  visitor_->OnWindowUpdateFrame(frame);
  return connected_;
}

void QuicConnection::OnClientConnectionIdAvailable() {
  QUICHE_DCHECK(perspective_ == Perspective::IS_SERVER);
  if (!peer_issued_cid_manager_->HasUnusedConnectionId()) {
    return;
  }
  if (default_path_.client_connection_id.IsEmpty()) {
    const QuicConnectionIdData* unused_cid_data =
        peer_issued_cid_manager_->ConsumeOneUnusedConnectionId();
    QUIC_DVLOG(1) << ENDPOINT << "Patch connection ID "
                  << unused_cid_data->connection_id << " to default path";
    default_path_.client_connection_id = unused_cid_data->connection_id;
    default_path_.stateless_reset_token =
        unused_cid_data->stateless_reset_token;
    QUICHE_DCHECK(!packet_creator_.HasPendingFrames());
    QUICHE_DCHECK(packet_creator_.GetDestinationConnectionId().IsEmpty());
    packet_creator_.SetClientConnectionId(default_path_.client_connection_id);
    return;
  }
  if (alternative_path_.peer_address.IsInitialized() &&
      alternative_path_.client_connection_id.IsEmpty()) {
    const QuicConnectionIdData* unused_cid_data =
        peer_issued_cid_manager_->ConsumeOneUnusedConnectionId();
    QUIC_DVLOG(1) << ENDPOINT << "Patch connection ID "
                  << unused_cid_data->connection_id << " to alternative path";
    alternative_path_.client_connection_id = unused_cid_data->connection_id;
    alternative_path_.stateless_reset_token =
        unused_cid_data->stateless_reset_token;
  }
}

NewConnectionIdResult QuicConnection::OnNewConnectionIdFrameInner(
    const QuicNewConnectionIdFrame& frame) {
  if (peer_issued_cid_manager_ == nullptr) {
    CloseConnection(
        IETF_QUIC_PROTOCOL_VIOLATION,
        "Receives NEW_CONNECTION_ID while peer uses zero length connection ID",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return NewConnectionIdResult::kProtocolViolation;
  }
  std::string error_detail;
  bool duplicate_new_connection_id = false;
  QuicErrorCode error = peer_issued_cid_manager_->OnNewConnectionIdFrame(
      frame, &error_detail, &duplicate_new_connection_id);
  if (error != QUIC_NO_ERROR) {
    CloseConnection(error, error_detail,
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return NewConnectionIdResult::kProtocolViolation;
  }
  if (duplicate_new_connection_id && ignore_duplicate_new_cid_frame_) {
    return NewConnectionIdResult::kDuplicateFrame;
  }
  if (perspective_ == Perspective::IS_SERVER) {
    OnClientConnectionIdAvailable();
  }
  MaybeUpdateAckTimeout();
  return NewConnectionIdResult::kOk;
}

bool QuicConnection::OnNewConnectionIdFrame(
    const QuicNewConnectionIdFrame& frame) {
  QUICHE_DCHECK(version().HasIetfQuicFrames());
  QUIC_BUG_IF(quic_bug_10511_13, !connected_)
      << "Processing NEW_CONNECTION_ID frame when connection is closed. "
         "Received packet info: "
      << last_received_packet_info_;
  if (!UpdatePacketContent(NEW_CONNECTION_ID_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnNewConnectionIdFrame(frame);
  }

  NewConnectionIdResult result = OnNewConnectionIdFrameInner(frame);
  switch (result) {
    case NewConnectionIdResult::kOk:
      if (multi_port_stats_ != nullptr) {
        MaybeCreateMultiPortPath();
      }
      break;
    case NewConnectionIdResult::kProtocolViolation:
      return false;
    case NewConnectionIdResult::kDuplicateFrame:
      break;
  }
  return true;
}

bool QuicConnection::OnRetireConnectionIdFrame(
    const QuicRetireConnectionIdFrame& frame) {
  QUICHE_DCHECK(version().HasIetfQuicFrames());
  QUIC_BUG_IF(quic_bug_10511_14, !connected_)
      << "Processing RETIRE_CONNECTION_ID frame when connection is closed. "
         "Received packet info: "
      << last_received_packet_info_;
  if (!UpdatePacketContent(RETIRE_CONNECTION_ID_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnRetireConnectionIdFrame(frame);
  }
  if (self_issued_cid_manager_ == nullptr) {
    CloseConnection(
        IETF_QUIC_PROTOCOL_VIOLATION,
        "Receives RETIRE_CONNECTION_ID while new connection ID is never issued",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  std::string error_detail;
  QuicErrorCode error = self_issued_cid_manager_->OnRetireConnectionIdFrame(
      frame, sent_packet_manager_.GetPtoDelay(), &error_detail);
  if (error != QUIC_NO_ERROR) {
    CloseConnection(error, error_detail,
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  // Count successfully received RETIRE_CONNECTION_ID frames.
  MaybeUpdateAckTimeout();
  return true;
}

bool QuicConnection::OnNewTokenFrame(const QuicNewTokenFrame& frame) {
  QUIC_BUG_IF(quic_bug_12714_15, !connected_)
      << "Processing NEW_TOKEN frame when connection is closed. Received "
         "packet info: "
      << last_received_packet_info_;
  if (!UpdatePacketContent(NEW_TOKEN_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnNewTokenFrame(frame);
  }
  if (perspective_ == Perspective::IS_SERVER) {
    CloseConnection(QUIC_INVALID_NEW_TOKEN, "Server received new token frame.",
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  // NEW_TOKEN frame should insitgate ACKs.
  MaybeUpdateAckTimeout();
  visitor_->OnNewTokenReceived(frame.token);
  return true;
}

bool QuicConnection::OnMessageFrame(const QuicMessageFrame& frame) {
  QUIC_BUG_IF(quic_bug_12714_16, !connected_)
      << "Processing MESSAGE frame when connection is closed. Received packet "
         "info: "
      << last_received_packet_info_;

  // Since a message frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  if (!UpdatePacketContent(MESSAGE_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnMessageFrame(frame);
  }
  MaybeUpdateAckTimeout();
  visitor_->OnMessageReceived(
      absl::string_view(frame.data, frame.message_length));
  return connected_;
}

bool QuicConnection::OnHandshakeDoneFrame(const QuicHandshakeDoneFrame& frame) {
  QUIC_BUG_IF(quic_bug_10511_15, !connected_)
      << "Processing HANDSHAKE_DONE frame when connection "
         "is closed. Received packet "
         "info: "
      << last_received_packet_info_;
  if (!version().UsesTls()) {
    CloseConnection(IETF_QUIC_PROTOCOL_VIOLATION,
                    "Handshake done frame is unsupported",
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  if (perspective_ == Perspective::IS_SERVER) {
    CloseConnection(IETF_QUIC_PROTOCOL_VIOLATION,
                    "Server received handshake done frame.",
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  // Since a handshake done frame was received, this is not a connectivity
  // probe. A probe only contains a PING and full padding.
  if (!UpdatePacketContent(HANDSHAKE_DONE_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnHandshakeDoneFrame(frame);
  }
  MaybeUpdateAckTimeout();
  visitor_->OnHandshakeDoneReceived();
  return connected_;
}

bool QuicConnection::OnAckFrequencyFrame(const QuicAckFrequencyFrame& frame) {
  QUIC_BUG_IF(quic_bug_10511_16, !connected_)
      << "Processing ACK_FREQUENCY frame when connection "
         "is closed. Received packet "
         "info: "
      << last_received_packet_info_;
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnAckFrequencyFrame(frame);
  }
  if (!UpdatePacketContent(ACK_FREQUENCY_FRAME)) {
    return false;
  }

  if (!can_receive_ack_frequency_frame_) {
    QUIC_LOG_EVERY_N_SEC(ERROR, 120) << "Get unexpected AckFrequencyFrame.";
    return false;
  }
  if (auto packet_number_space =
          QuicUtils::GetPacketNumberSpace(
              last_received_packet_info_.decrypted_level) == APPLICATION_DATA) {
    uber_received_packet_manager_.OnAckFrequencyFrame(frame);
  } else {
    QUIC_LOG_EVERY_N_SEC(ERROR, 120)
        << "Get AckFrequencyFrame in packet number space "
        << packet_number_space;
  }
  MaybeUpdateAckTimeout();
  return true;
}

bool QuicConnection::OnBlockedFrame(const QuicBlockedFrame& frame) {
  QUIC_BUG_IF(quic_bug_12714_17, !connected_)
      << "Processing BLOCKED frame when connection is closed. Received packet "
         "info: "
      << last_received_packet_info_;

  // Since a blocked frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  if (!UpdatePacketContent(BLOCKED_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnBlockedFrame(frame);
  }
  QUIC_DLOG(INFO) << ENDPOINT
                  << "BLOCKED_FRAME received for stream: " << frame.stream_id;
  MaybeUpdateAckTimeout();
  visitor_->OnBlockedFrame(frame);
  stats_.blocked_frames_received++;
  return connected_;
}

void QuicConnection::OnPacketComplete() {
  // Don't do anything if this packet closed the connection.
  if (!connected_) {
    ClearLastFrames();
    return;
  }

  if (IsCurrentPacketConnectivityProbing()) {
    QUICHE_DCHECK(!version().HasIetfQuicFrames() && !ignore_gquic_probing_);
    ++stats_.num_connectivity_probing_received;
  }

  QUIC_DVLOG(1) << ENDPOINT << "Got"
                << (SupportsMultiplePacketNumberSpaces()
                        ? (" " +
                           EncryptionLevelToString(
                               last_received_packet_info_.decrypted_level))
                        : "")
                << " packet " << last_received_packet_info_.header.packet_number
                << " for "
                << GetServerConnectionIdAsRecipient(
                       last_received_packet_info_.header, perspective_);

  QUIC_DLOG_IF(INFO, current_packet_content_ == SECOND_FRAME_IS_PADDING)
      << ENDPOINT << "Received a padded PING packet. is_probing: "
      << IsCurrentPacketConnectivityProbing();

  if (!version().HasIetfQuicFrames() && !ignore_gquic_probing_) {
    MaybeRespondToConnectivityProbingOrMigration();
  }

  current_effective_peer_migration_type_ = NO_CHANGE;

  // For IETF QUIC, it is guaranteed that TLS will give connection the
  // corresponding write key before read key. In other words, connection should
  // never process a packet while an ACK for it cannot be encrypted.
  if (!should_last_packet_instigate_acks_) {
    uber_received_packet_manager_.MaybeUpdateAckTimeout(
        should_last_packet_instigate_acks_,
        last_received_packet_info_.decrypted_level,
        last_received_packet_info_.header.packet_number,
        last_received_packet_info_.receipt_time, clock_->ApproximateNow(),
        sent_packet_manager_.GetRttStats());
  }

  ClearLastFrames();
  CloseIfTooManyOutstandingSentPackets();
}

void QuicConnection::MaybeRespondToConnectivityProbingOrMigration() {
  QUICHE_DCHECK(!version().HasIetfQuicFrames());
  if (IsCurrentPacketConnectivityProbing()) {
    visitor_->OnPacketReceived(last_received_packet_info_.destination_address,
                               last_received_packet_info_.source_address,
                               /*is_connectivity_probe=*/true);
    return;
  }
  if (perspective_ == Perspective::IS_CLIENT) {
    // This node is a client, notify that a speculative connectivity probing
    // packet has been received anyway.
    QUIC_DVLOG(1) << ENDPOINT
                  << "Received a speculative connectivity probing packet for "
                  << GetServerConnectionIdAsRecipient(
                         last_received_packet_info_.header, perspective_)
                  << " from ip:port: "
                  << last_received_packet_info_.source_address.ToString()
                  << " to ip:port: "
                  << last_received_packet_info_.destination_address.ToString();
    visitor_->OnPacketReceived(last_received_packet_info_.destination_address,
                               last_received_packet_info_.source_address,
                               /*is_connectivity_probe=*/false);
    return;
  }
}

bool QuicConnection::IsValidStatelessResetToken(
    const StatelessResetToken& token) const {
  QUICHE_DCHECK_EQ(perspective_, Perspective::IS_CLIENT);
  return default_path_.stateless_reset_token.has_value() &&
         QuicUtils::AreStatelessResetTokensEqual(
             token, *default_path_.stateless_reset_token);
}

void QuicConnection::OnAuthenticatedIetfStatelessResetPacket(
    const QuicIetfStatelessResetPacket& /*packet*/) {
  // TODO(fayang): Add OnAuthenticatedIetfStatelessResetPacket to
  // debug_visitor_.
  QUICHE_DCHECK_EQ(perspective_, Perspective::IS_CLIENT);

  if (!IsDefaultPath(last_received_packet_info_.destination_address,
                     last_received_packet_info_.source_address)) {
    // This packet is received on a probing path. Do not close connection.
    if (IsAlternativePath(last_received_packet_info_.destination_address,
                          GetEffectivePeerAddressFromCurrentPacket())) {
      QUIC_BUG_IF(quic_bug_12714_18, alternative_path_.validated)
          << "STATELESS_RESET received on alternate path after it's "
             "validated.";
      path_validator_.CancelPathValidation();
    } else {
      QUIC_BUG(quic_bug_10511_17)
          << "Received Stateless Reset on unknown socket.";
    }
    return;
  }

  const std::string error_details = "Received stateless reset.";
  QUIC_CODE_COUNT(quic_tear_down_local_connection_on_stateless_reset);
  TearDownLocalConnectionState(QUIC_PUBLIC_RESET, NO_IETF_QUIC_ERROR,
                               error_details, ConnectionCloseSource::FROM_PEER);
}

void QuicConnection::OnKeyUpdate(KeyUpdateReason reason) {
  QUICHE_DCHECK(support_key_update_for_connection_);
  QUIC_DLOG(INFO) << ENDPOINT << "Key phase updated for " << reason;

  lowest_packet_sent_in_current_key_phase_.Clear();
  stats_.key_update_count++;

  // If another key update triggers while the previous
  // discard_previous_one_rtt_keys_alarm_ hasn't fired yet, cancel it since the
  // old keys would already be discarded.
  discard_previous_one_rtt_keys_alarm_->Cancel();

  visitor_->OnKeyUpdate(reason);
}

void QuicConnection::OnDecryptedFirstPacketInKeyPhase() {
  QUIC_DLOG(INFO) << ENDPOINT << "OnDecryptedFirstPacketInKeyPhase";
  // An endpoint SHOULD retain old read keys for no more than three times the
  // PTO after having received a packet protected using the new keys. After this
  // period, old read keys and their corresponding secrets SHOULD be discarded.
  //
  // Note that this will cause an unnecessary
  // discard_previous_one_rtt_keys_alarm_ on the first packet in the 1RTT
  // encryption level, but this is harmless.
  discard_previous_one_rtt_keys_alarm_->Set(
      clock_->ApproximateNow() + sent_packet_manager_.GetPtoDelay() * 3);
}

std::unique_ptr<QuicDecrypter>
QuicConnection::AdvanceKeysAndCreateCurrentOneRttDecrypter() {
  QUIC_DLOG(INFO) << ENDPOINT << "AdvanceKeysAndCreateCurrentOneRttDecrypter";
  return visitor_->AdvanceKeysAndCreateCurrentOneRttDecrypter();
}

std::unique_ptr<QuicEncrypter> QuicConnection::CreateCurrentOneRttEncrypter() {
  QUIC_DLOG(INFO) << ENDPOINT << "CreateCurrentOneRttEncrypter";
  return visitor_->CreateCurrentOneRttEncrypter();
}

void QuicConnection::ClearLastFrames() {
  should_last_packet_instigate_acks_ = false;
}

void QuicConnection::CloseIfTooManyOutstandingSentPackets() {
  // This occurs if we don't discard old packets we've seen fast enough. It's
  // possible largest observed is less than leaset unacked.
  const bool should_close =
      sent_packet_manager_.GetLargestSentPacket().IsInitialized() &&
      sent_packet_manager_.GetLargestSentPacket() >
          sent_packet_manager_.GetLeastUnacked() + max_tracked_packets_;

  if (should_close) {
    CloseConnection(
        QUIC_TOO_MANY_OUTSTANDING_SENT_PACKETS,
        absl::StrCat("More than ", max_tracked_packets_,
                     " outstanding, least_unacked: ",
                     sent_packet_manager_.GetLeastUnacked().ToUint64(),
                     ", packets_processed: ", stats_.packets_processed,
                     ", last_decrypted_packet_level: ",
                     EncryptionLevelToString(
                         last_received_packet_info_.decrypted_level)),
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }
}

const QuicFrame QuicConnection::GetUpdatedAckFrame() {
  QUICHE_DCHECK(!uber_received_packet_manager_.IsAckFrameEmpty(
      QuicUtils::GetPacketNumberSpace(encryption_level_)))
      << "Try to retrieve an empty ACK frame";
  return uber_received_packet_manager_.GetUpdatedAckFrame(
      QuicUtils::GetPacketNumberSpace(encryption_level_),
      clock_->ApproximateNow());
}

QuicPacketNumber QuicConnection::GetLeastUnacked() const {
  return sent_packet_manager_.GetLeastUnacked();
}

bool QuicConnection::HandleWriteBlocked() {
  if (!writer_->IsWriteBlocked()) {
    return false;
  }

  visitor_->OnWriteBlocked();
  return true;
}

void QuicConnection::MaybeSendInResponseToPacket() {
  if (!connected_) {
    return;
  }

  if (IsMissingDestinationConnectionID()) {
    return;
  }

  // If the writer is blocked, don't attempt to send packets now or in the send
  // alarm. When the writer unblocks, OnCanWrite() will be called for this
  // connection to send.
  if (HandleWriteBlocked()) {
    return;
  }

  if (!GetQuicReloadableFlag(quic_no_send_alarm_unless_necessary)) {
    // Now that we have received an ack, we might be able to send packets which
    // are queued locally, or drain streams which are blocked.
    if (defer_send_in_response_to_packets_) {
      send_alarm_->Update(clock_->ApproximateNow() +
                              sent_packet_manager_.GetDeferredSendAlarmDelay(),
                          QuicTime::Delta::Zero());
    } else {
      WriteIfNotBlocked();
    }
    return;
  }

  if (!defer_send_in_response_to_packets_) {
    WriteIfNotBlocked();
    return;
  }

  if (!visitor_->WillingAndAbleToWrite()) {
    QUIC_DVLOG(1)
        << "No send alarm after processing packet. !WillingAndAbleToWrite.";
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_no_send_alarm_unless_necessary, 1, 7);
    return;
  }

  // If the send alarm is already armed. Record its deadline in |max_deadline|
  // and cancel the alarm temporarily. The rest of this function will ensure
  // the alarm deadline is no later than |max_deadline| when the function exits.
  QuicTime max_deadline = QuicTime::Infinite();
  if (send_alarm_->IsSet()) {
    QUIC_DVLOG(1) << "Send alarm already set to " << send_alarm_->deadline();
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_no_send_alarm_unless_necessary, 2, 7);
    max_deadline = send_alarm_->deadline();
    send_alarm_->Cancel();
  }

  if (CanWrite(HAS_RETRANSMITTABLE_DATA)) {
    // Some data can be written immediately. Register for immediate resumption
    // so we'll keep writing after other connections.
    QUIC_BUG_IF(quic_send_alarm_set_with_data_to_send, send_alarm_->IsSet());
    QUIC_DVLOG(1) << "Immediate send alarm scheduled after processing packet.";
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_no_send_alarm_unless_necessary, 3, 7);
    send_alarm_->Set(clock_->ApproximateNow() +
                     sent_packet_manager_.GetDeferredSendAlarmDelay());
    return;
  }

  if (send_alarm_->IsSet()) {
    // Pacing limited: CanWrite returned false, and it has scheduled a send
    // alarm before it returns.
    if (send_alarm_->deadline() > max_deadline) {
      QUIC_BUG(quic_send_alarm_postponed)
          << "previous deadline:" << max_deadline
          << ", deadline from CanWrite:" << send_alarm_->deadline();
      QUIC_DVLOG(1) << "Send alarm restored after processing packet.";
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_no_send_alarm_unless_necessary, 4, 7);
      // Restore to the previous, earlier deadline.
      send_alarm_->Update(max_deadline, QuicTime::Delta::Zero());
    } else {
      QUIC_DVLOG(1) << "Future send alarm scheduled after processing packet.";
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_no_send_alarm_unless_necessary, 5, 7);
    }
    return;
  }

  if (max_deadline != QuicTime::Infinite()) {
    QUIC_DVLOG(1) << "Send alarm restored after processing packet.";
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_no_send_alarm_unless_necessary, 6, 7);
    send_alarm_->Set(max_deadline);
    return;
  }
  // Can not send data due to other reasons: congestion blocked, anti
  // amplification throttled, etc.
  QUIC_DVLOG(1) << "No send alarm after processing packet. Other reasons.";
  QUIC_RELOADABLE_FLAG_COUNT_N(quic_no_send_alarm_unless_necessary, 7, 7);
}

size_t QuicConnection::SendCryptoData(EncryptionLevel level,
                                      size_t write_length,
                                      QuicStreamOffset offset) {
  if (write_length == 0) {
    QUIC_BUG(quic_bug_10511_18) << "Attempt to send empty crypto frame";
    return 0;
  }
  ScopedPacketFlusher flusher(this);
  return packet_creator_.ConsumeCryptoData(level, write_length, offset);
}

QuicConsumedData QuicConnection::SendStreamData(QuicStreamId id,
                                                size_t write_length,
                                                QuicStreamOffset offset,
                                                StreamSendingState state) {
  if (state == NO_FIN && write_length == 0) {
    QUIC_BUG(quic_bug_10511_19) << "Attempt to send empty stream frame";
    return QuicConsumedData(0, false);
  }

  if (perspective_ == Perspective::IS_SERVER &&
      version().CanSendCoalescedPackets() && !IsHandshakeConfirmed()) {
    if (in_probe_time_out_ && coalesced_packet_.NumberOfPackets() == 0u) {
      // PTO fires while handshake is not confirmed. Do not preempt handshake
      // data with stream data.
      QUIC_CODE_COUNT(quic_try_to_send_half_rtt_data_when_pto_fires);
      return QuicConsumedData(0, false);
    }
    if (coalesced_packet_.ContainsPacketOfEncryptionLevel(ENCRYPTION_INITIAL) &&
        coalesced_packet_.NumberOfPackets() == 1u) {
      // Handshake is not confirmed yet, if there is only an initial packet in
      // the coalescer, try to bundle an ENCRYPTION_HANDSHAKE packet before
      // sending stream data.
      sent_packet_manager_.RetransmitDataOfSpaceIfAny(HANDSHAKE_DATA);
    }
  }
  // Opportunistically bundle an ack with every outgoing packet.
  // Particularly, we want to bundle with handshake packets since we don't
  // know which decrypter will be used on an ack packet following a handshake
  // packet (a handshake packet from client to server could result in a REJ or
  // a SHLO from the server, leading to two different decrypters at the
  // server.)
  ScopedPacketFlusher flusher(this);
  return packet_creator_.ConsumeData(id, write_length, offset, state);
}

bool QuicConnection::SendControlFrame(const QuicFrame& frame) {
  if (SupportsMultiplePacketNumberSpaces() &&
      (encryption_level_ == ENCRYPTION_INITIAL ||
       encryption_level_ == ENCRYPTION_HANDSHAKE) &&
      frame.type != PING_FRAME) {
    // Allow PING frame to be sent without APPLICATION key. For example, when
    // anti-amplification limit is used, client needs to send something to avoid
    // handshake deadlock.
    QUIC_DVLOG(1) << ENDPOINT << "Failed to send control frame: " << frame
                  << " at encryption level: " << encryption_level_;
    return false;
  }
  ScopedPacketFlusher flusher(this);
  const bool consumed =
      packet_creator_.ConsumeRetransmittableControlFrame(frame);
  if (!consumed) {
    QUIC_DVLOG(1) << ENDPOINT << "Failed to send control frame: " << frame;
    return false;
  }
  if (frame.type == PING_FRAME) {
    // Flush PING frame immediately.
    packet_creator_.FlushCurrentPacket();
    stats_.ping_frames_sent++;
    if (debug_visitor_ != nullptr) {
      debug_visitor_->OnPingSent();
    }
  }
  if (frame.type == BLOCKED_FRAME) {
    stats_.blocked_frames_sent++;
  }
  return true;
}

void QuicConnection::OnStreamReset(QuicStreamId id,
                                   QuicRstStreamErrorCode error) {
  if (error == QUIC_STREAM_NO_ERROR) {
    // All data for streams which are reset with QUIC_STREAM_NO_ERROR must
    // be received by the peer.
    return;
  }
  // Flush stream frames of reset stream.
  if (packet_creator_.HasPendingStreamFramesOfStream(id)) {
    ScopedPacketFlusher flusher(this);
    packet_creator_.FlushCurrentPacket();
  }
  // TODO(ianswett): Consider checking for 3 RTOs when the last stream is
  // cancelled as well.
}

const QuicConnectionStats& QuicConnection::GetStats() {
  const RttStats* rtt_stats = sent_packet_manager_.GetRttStats();

  // Update rtt and estimated bandwidth.
  QuicTime::Delta min_rtt = rtt_stats->min_rtt();
  if (min_rtt.IsZero()) {
    // If min RTT has not been set, use initial RTT instead.
    min_rtt = rtt_stats->initial_rtt();
  }
  stats_.min_rtt_us = min_rtt.ToMicroseconds();

  QuicTime::Delta srtt = rtt_stats->SmoothedOrInitialRtt();
  stats_.srtt_us = srtt.ToMicroseconds();

  stats_.estimated_bandwidth = sent_packet_manager_.BandwidthEstimate();
  sent_packet_manager_.GetSendAlgorithm()->PopulateConnectionStats(&stats_);
  stats_.egress_mtu = long_term_mtu_;
  stats_.ingress_mtu = largest_received_packet_size_;
  return stats_;
}

void QuicConnection::OnCoalescedPacket(const QuicEncryptedPacket& packet) {
  QueueCoalescedPacket(packet);
}

void QuicConnection::OnUndecryptablePacket(const QuicEncryptedPacket& packet,
                                           EncryptionLevel decryption_level,
                                           bool has_decryption_key) {
  QUIC_DVLOG(1) << ENDPOINT << "Received undecryptable packet of length "
                << packet.length() << " with"
                << (has_decryption_key ? "" : "out") << " key at level "
                << decryption_level
                << " while connection is at encryption level "
                << encryption_level_;
  QUICHE_DCHECK(EncryptionLevelIsValid(decryption_level));
  if (encryption_level_ != ENCRYPTION_FORWARD_SECURE) {
    ++stats_.undecryptable_packets_received_before_handshake_complete;
  }

  const bool should_enqueue =
      ShouldEnqueueUnDecryptablePacket(decryption_level, has_decryption_key);
  if (should_enqueue) {
    QueueUndecryptablePacket(packet, decryption_level);
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnUndecryptablePacket(decryption_level,
                                          /*dropped=*/!should_enqueue);
  }

  if (has_decryption_key) {
    stats_.num_failed_authentication_packets_received++;
    if (version().UsesTls()) {
      // Should always be non-null if has_decryption_key is true.
      QUICHE_DCHECK(framer_.GetDecrypter(decryption_level));
      const QuicPacketCount integrity_limit =
          framer_.GetDecrypter(decryption_level)->GetIntegrityLimit();
      QUIC_DVLOG(2) << ENDPOINT << "Checking AEAD integrity limits:"
                    << " num_failed_authentication_packets_received="
                    << stats_.num_failed_authentication_packets_received
                    << " integrity_limit=" << integrity_limit;
      if (stats_.num_failed_authentication_packets_received >=
          integrity_limit) {
        const std::string error_details = absl::StrCat(
            "decrypter integrity limit reached:"
            " num_failed_authentication_packets_received=",
            stats_.num_failed_authentication_packets_received,
            " integrity_limit=", integrity_limit);
        CloseConnection(QUIC_AEAD_LIMIT_REACHED, error_details,
                        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      }
    }
  }

  if (version().UsesTls() && perspective_ == Perspective::IS_SERVER &&
      decryption_level == ENCRYPTION_ZERO_RTT && !has_decryption_key &&
      had_zero_rtt_decrypter_) {
    QUIC_CODE_COUNT_N(
        quic_server_received_tls_zero_rtt_packet_after_discarding_decrypter, 1,
        3);
    stats_
        .num_tls_server_zero_rtt_packets_received_after_discarding_decrypter++;
  }
}

bool QuicConnection::ShouldEnqueueUnDecryptablePacket(
    EncryptionLevel decryption_level, bool has_decryption_key) const {
  if (has_decryption_key) {
    // We already have the key for this decryption level, therefore no
    // future keys will allow it be decrypted.
    return false;
  }
  if (IsHandshakeComplete()) {
    // We do not expect to install any further keys.
    return false;
  }
  if (undecryptable_packets_.size() >= max_undecryptable_packets_) {
    // We do not queue more than max_undecryptable_packets_ packets.
    return false;
  }
  if (version().KnowsWhichDecrypterToUse() &&
      decryption_level == ENCRYPTION_INITIAL) {
    // When the corresponding decryption key is not available, all
    // non-Initial packets should be buffered until the handshake is complete.
    return false;
  }
  if (perspective_ == Perspective::IS_CLIENT && version().UsesTls() &&
      decryption_level == ENCRYPTION_ZERO_RTT) {
    // Only clients send Zero RTT packets in IETF QUIC.
    QUIC_PEER_BUG(quic_peer_bug_client_received_zero_rtt)
        << "Client received a Zero RTT packet, not buffering.";
    return false;
  }
  return true;
}

std::string QuicConnection::UndecryptablePacketsInfo() const {
  std::string info = absl::StrCat(
      "num_undecryptable_packets: ", undecryptable_packets_.size(), " {");
  for (const auto& packet : undecryptable_packets_) {
    absl::StrAppend(&info, "[",
                    EncryptionLevelToString(packet.encryption_level), ", ",
                    packet.packet->length(), "]");
  }
  absl::StrAppend(&info, "}");
  return info;
}

void QuicConnection::ProcessUdpPacket(const QuicSocketAddress& self_address,
                                      const QuicSocketAddress& peer_address,
                                      const QuicReceivedPacket& packet) {
  if (!connected_) {
    return;
  }
  QUIC_DVLOG(2) << ENDPOINT << "Received encrypted " << packet.length()
                << " bytes:" << std::endl
                << quiche::QuicheTextUtils::HexDump(
                       absl::string_view(packet.data(), packet.length()));
  QUIC_BUG_IF(quic_bug_12714_21, current_packet_data_ != nullptr)
      << "ProcessUdpPacket must not be called while processing a packet.";
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPacketReceived(self_address, peer_address, packet);
  }
  last_received_packet_info_ =
      ReceivedPacketInfo(self_address, peer_address, packet.receipt_time(),
                         packet.length(), packet.ecn_codepoint());
  current_packet_data_ = packet.data();

  if (!default_path_.self_address.IsInitialized()) {
    default_path_.self_address = last_received_packet_info_.destination_address;
  } else if (default_path_.self_address != self_address &&
             sent_server_preferred_address_.IsInitialized() &&
             self_address.Normalized() ==
                 sent_server_preferred_address_.Normalized()) {
    // If the packet is received at the preferred address, treat it as if it is
    // received on the original server address.
    last_received_packet_info_.destination_address = default_path_.self_address;
    last_received_packet_info_.actual_destination_address = self_address;
  }

  if (!direct_peer_address_.IsInitialized()) {
    if (perspective_ == Perspective::IS_CLIENT) {
      AddKnownServerAddress(last_received_packet_info_.source_address);
    }
    UpdatePeerAddress(last_received_packet_info_.source_address);
  }

  if (!default_path_.peer_address.IsInitialized()) {
    const QuicSocketAddress effective_peer_addr =
        GetEffectivePeerAddressFromCurrentPacket();

    // The default path peer_address must be initialized at the beginning of the
    // first packet processed(here). If effective_peer_addr is uninitialized,
    // just set effective_peer_address_ to the direct peer address.
    default_path_.peer_address = effective_peer_addr.IsInitialized()
                                     ? effective_peer_addr
                                     : direct_peer_address_;
  }

  stats_.bytes_received += packet.length();
  ++stats_.packets_received;
  if (IsDefaultPath(last_received_packet_info_.destination_address,
                    last_received_packet_info_.source_address) &&
      EnforceAntiAmplificationLimit()) {
    last_received_packet_info_.received_bytes_counted = true;
    default_path_.bytes_received_before_address_validation +=
        last_received_packet_info_.length;
  }

  // Ensure the time coming from the packet reader is within 2 minutes of now.
  if (std::abs((packet.receipt_time() - clock_->ApproximateNow()).ToSeconds()) >
      2 * 60) {
    QUIC_LOG(WARNING) << "(Formerly quic_bug_10511_21): Packet receipt time: "
                      << packet.receipt_time().ToDebuggingValue()
                      << " too far from current time: "
                      << clock_->ApproximateNow().ToDebuggingValue();
  }
  QUIC_DVLOG(1) << ENDPOINT << "time of last received packet: "
                << packet.receipt_time().ToDebuggingValue() << " from peer "
                << last_received_packet_info_.source_address << ", to "
                << last_received_packet_info_.destination_address;

  ScopedPacketFlusher flusher(this);
  if (!framer_.ProcessPacket(packet)) {
    // If we are unable to decrypt this packet, it might be
    // because the CHLO or SHLO packet was lost.
    QUIC_DVLOG(1) << ENDPOINT
                  << "Unable to process packet.  Last packet processed: "
                  << last_received_packet_info_.header.packet_number;
    current_packet_data_ = nullptr;
    is_current_packet_connectivity_probing_ = false;

    MaybeProcessCoalescedPackets();
    return;
  }

  ++stats_.packets_processed;

  QUIC_DLOG_IF(INFO, active_effective_peer_migration_type_ != NO_CHANGE)
      << "sent_packet_manager_.GetLargestObserved() = "
      << sent_packet_manager_.GetLargestObserved()
      << ", highest_packet_sent_before_effective_peer_migration_ = "
      << highest_packet_sent_before_effective_peer_migration_;
  if (!framer_.version().HasIetfQuicFrames() &&
      active_effective_peer_migration_type_ != NO_CHANGE &&
      sent_packet_manager_.GetLargestObserved().IsInitialized() &&
      (!highest_packet_sent_before_effective_peer_migration_.IsInitialized() ||
       sent_packet_manager_.GetLargestObserved() >
           highest_packet_sent_before_effective_peer_migration_)) {
    if (perspective_ == Perspective::IS_SERVER) {
      OnEffectivePeerMigrationValidated(/*is_migration_linkable=*/true);
    }
  }

  if (!MaybeProcessCoalescedPackets()) {
    MaybeProcessUndecryptablePackets();
    MaybeSendInResponseToPacket();
  }
  SetPingAlarm();
  RetirePeerIssuedConnectionIdsNoLongerOnPath();
  current_packet_data_ = nullptr;
  is_current_packet_connectivity_probing_ = false;
}

void QuicConnection::OnBlockedWriterCanWrite() {
  writer_->SetWritable();
  OnCanWrite();
}

void QuicConnection::OnCanWrite() {
  if (!connected_) {
    return;
  }
  if (writer_->IsWriteBlocked()) {
    const std::string error_details =
        "Writer is blocked while calling OnCanWrite.";
    QUIC_BUG(quic_bug_10511_22) << ENDPOINT << error_details;
    CloseConnection(QUIC_INTERNAL_ERROR, error_details,
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  ScopedPacketFlusher flusher(this);

  WriteQueuedPackets();
  const QuicTime ack_timeout =
      uber_received_packet_manager_.GetEarliestAckTimeout();
  if (ack_timeout.IsInitialized() && ack_timeout <= clock_->ApproximateNow()) {
    // Send an ACK now because either 1) we were write blocked when we last
    // tried to send an ACK, or 2) both ack alarm and send alarm were set to
    // go off together.
    if (SupportsMultiplePacketNumberSpaces()) {
      SendAllPendingAcks();
    } else {
      SendAck();
    }
  }

  // Sending queued packets may have caused the socket to become write blocked,
  // or the congestion manager to prohibit sending.
  if (!CanWrite(HAS_RETRANSMITTABLE_DATA)) {
    return;
  }

  // Tell the session it can write.
  visitor_->OnCanWrite();

  // After the visitor writes, it may have caused the socket to become write
  // blocked or the congestion manager to prohibit sending, so check again.
  if (visitor_->WillingAndAbleToWrite() && !send_alarm_->IsSet() &&
      CanWrite(HAS_RETRANSMITTABLE_DATA)) {
    // We're not write blocked, but some data wasn't written. Register for
    // 'immediate' resumption so we'll keep writing after other connections.
    send_alarm_->Set(clock_->ApproximateNow());
  }
}

void QuicConnection::OnSendAlarm() { WriteIfNotBlocked(); }

void QuicConnection::WriteIfNotBlocked() {
  if (framer().is_processing_packet()) {
    QUIC_BUG(connection_write_mid_packet_processing)
        << ENDPOINT << "Tried to write in mid of packet processing";
    return;
  }
  if (IsMissingDestinationConnectionID()) {
    return;
  }
  if (!HandleWriteBlocked()) {
    OnCanWrite();
  }
}

void QuicConnection::MaybeClearQueuedPacketsOnPathChange() {
  if (version().HasIetfQuicFrames() && peer_issued_cid_manager_ != nullptr &&
      HasQueuedPackets()) {
    // Discard packets serialized with the connection ID on the old code path.
    // It is possible to clear queued packets only if connection ID changes.
    // However, the case where connection ID is unchanged and queued packets are
    // non-empty is quite rare.
    ClearQueuedPackets();
  }
}

void QuicConnection::ReplaceInitialServerConnectionId(
    const QuicConnectionId& new_server_connection_id) {
  QUICHE_DCHECK(perspective_ == Perspective::IS_CLIENT);
  if (version().HasIetfQuicFrames()) {
    if (new_server_connection_id.IsEmpty()) {
      peer_issued_cid_manager_ = nullptr;
    } else {
      if (peer_issued_cid_manager_ != nullptr) {
        QUIC_BUG_IF(quic_bug_12714_22,
                    !peer_issued_cid_manager_->IsConnectionIdActive(
                        default_path_.server_connection_id))
            << "Connection ID replaced header is no longer active. old id: "
            << default_path_.server_connection_id
            << " new_id: " << new_server_connection_id;
        peer_issued_cid_manager_->ReplaceConnectionId(
            default_path_.server_connection_id, new_server_connection_id);
      } else {
        peer_issued_cid_manager_ =
            std::make_unique<QuicPeerIssuedConnectionIdManager>(
                kMinNumOfActiveConnectionIds, new_server_connection_id, clock_,
                alarm_factory_, this, context());
      }
    }
  }
  default_path_.server_connection_id = new_server_connection_id;
  packet_creator_.SetServerConnectionId(default_path_.server_connection_id);
}

void QuicConnection::FindMatchingOrNewClientConnectionIdOrToken(
    const PathState& default_path, const PathState& alternative_path,
    const QuicConnectionId& server_connection_id,
    QuicConnectionId* client_connection_id,
    absl::optional<StatelessResetToken>* stateless_reset_token) {
  QUICHE_DCHECK(perspective_ == Perspective::IS_SERVER &&
                version().HasIetfQuicFrames());
  if (peer_issued_cid_manager_ == nullptr ||
      server_connection_id == default_path.server_connection_id) {
    *client_connection_id = default_path.client_connection_id;
    *stateless_reset_token = default_path.stateless_reset_token;
    return;
  }
  if (server_connection_id == alternative_path_.server_connection_id) {
    *client_connection_id = alternative_path.client_connection_id;
    *stateless_reset_token = alternative_path.stateless_reset_token;
    return;
  }
  auto* connection_id_data =
      peer_issued_cid_manager_->ConsumeOneUnusedConnectionId();
  if (connection_id_data == nullptr) {
    return;
  }
  *client_connection_id = connection_id_data->connection_id;
  *stateless_reset_token = connection_id_data->stateless_reset_token;
}

bool QuicConnection::FindOnPathConnectionIds(
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address,
    QuicConnectionId* client_connection_id,
    QuicConnectionId* server_connection_id) const {
  if (IsDefaultPath(self_address, peer_address)) {
    *client_connection_id = default_path_.client_connection_id,
    *server_connection_id = default_path_.server_connection_id;
    return true;
  }
  if (IsAlternativePath(self_address, peer_address)) {
    *client_connection_id = alternative_path_.client_connection_id,
    *server_connection_id = alternative_path_.server_connection_id;
    return true;
  }
  // Client should only send packets on either default or alternative path, so
  // it shouldn't fail here. If the server fail to find CID to use, no packet
  // will be generated on this path.
  // TODO(danzh) fix SendPathResponse() to respond to probes from a different
  // client port with non-Zero client CID.
  QUIC_BUG_IF(failed to find on path connection ids,
              perspective_ == Perspective::IS_CLIENT)
      << "Fails to find on path connection IDs";
  return false;
}

void QuicConnection::SetDefaultPathState(PathState new_path_state) {
  QUICHE_DCHECK(version().HasIetfQuicFrames());
  default_path_ = std::move(new_path_state);
  packet_creator_.SetClientConnectionId(default_path_.client_connection_id);
  packet_creator_.SetServerConnectionId(default_path_.server_connection_id);
}

bool QuicConnection::ProcessValidatedPacket(const QuicPacketHeader& header) {
  if (perspective_ == Perspective::IS_CLIENT && version().HasIetfQuicFrames() &&
      direct_peer_address_.IsInitialized() &&
      last_received_packet_info_.source_address.IsInitialized() &&
      direct_peer_address_ != last_received_packet_info_.source_address &&
      !IsKnownServerAddress(last_received_packet_info_.source_address)) {
    // Discard packets received from unseen server addresses.
    return false;
  }

  if (perspective_ == Perspective::IS_SERVER &&
      default_path_.self_address.IsInitialized() &&
      last_received_packet_info_.destination_address.IsInitialized() &&
      default_path_.self_address !=
          last_received_packet_info_.destination_address) {
    // Allow change between pure IPv4 and equivalent mapped IPv4 address.
    if (default_path_.self_address.port() !=
            last_received_packet_info_.destination_address.port() ||
        default_path_.self_address.host().Normalized() !=
            last_received_packet_info_.destination_address.host()
                .Normalized()) {
      if (!visitor_->AllowSelfAddressChange()) {
        const std::string error_details = absl::StrCat(
            "Self address migration is not supported at the server, current "
            "address: ",
            default_path_.self_address.ToString(),
            ", server preferred address: ",
            sent_server_preferred_address_.ToString(),
            ", received packet address: ",
            last_received_packet_info_.destination_address.ToString(),
            ", size: ", last_received_packet_info_.length,
            ", packet number: ", header.packet_number.ToString(),
            ", encryption level: ",
            EncryptionLevelToString(
                last_received_packet_info_.decrypted_level));
        QUIC_LOG_EVERY_N_SEC(INFO, 100) << error_details;
        QUIC_CODE_COUNT(quic_dropped_packets_with_changed_server_address);
        return false;
      }
    }
    default_path_.self_address = last_received_packet_info_.destination_address;
  }

  if (GetQuicReloadableFlag(quic_use_received_client_addresses_cache) &&
      perspective_ == Perspective::IS_SERVER &&
      !last_received_packet_info_.actual_destination_address.IsInitialized() &&
      last_received_packet_info_.source_address.IsInitialized()) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_use_received_client_addresses_cache);
    // Record client address of packets received on server original address.
    received_client_addresses_cache_.Insert(
        last_received_packet_info_.source_address,
        std::make_unique<bool>(true));
  }

  if (perspective_ == Perspective::IS_SERVER &&
      last_received_packet_info_.actual_destination_address.IsInitialized() &&
      !IsHandshakeConfirmed() &&
      GetEffectivePeerAddressFromCurrentPacket() !=
          default_path_.peer_address) {
    // Our client implementation has an optimization to spray packets from
    // different sockets to the server's preferred address before handshake
    // gets confirmed. In this case, do not kick off client address migration
    // detection.
    QUICHE_DCHECK(sent_server_preferred_address_.IsInitialized());
    last_received_packet_info_.source_address = direct_peer_address_;
  }

  if (PacketCanReplaceServerConnectionId(header, perspective_) &&
      default_path_.server_connection_id != header.source_connection_id) {
    QUICHE_DCHECK_EQ(header.long_packet_type, INITIAL);
    if (server_connection_id_replaced_by_initial_) {
      QUIC_DLOG(ERROR) << ENDPOINT << "Refusing to replace connection ID "
                       << default_path_.server_connection_id << " with "
                       << header.source_connection_id;
      return false;
    }
    server_connection_id_replaced_by_initial_ = true;
    QUIC_DLOG(INFO) << ENDPOINT << "Replacing connection ID "
                    << default_path_.server_connection_id << " with "
                    << header.source_connection_id;
    if (!original_destination_connection_id_.has_value()) {
      original_destination_connection_id_ = default_path_.server_connection_id;
    }
    ReplaceInitialServerConnectionId(header.source_connection_id);
  }

  if (!ValidateReceivedPacketNumber(header.packet_number)) {
    return false;
  }

  if (!version_negotiated_) {
    if (perspective_ == Perspective::IS_CLIENT) {
      QUICHE_DCHECK(!header.version_flag || header.form != GOOGLE_QUIC_PACKET);
      version_negotiated_ = true;
      OnSuccessfulVersionNegotiation();
    }
  }

  if (last_received_packet_info_.length > largest_received_packet_size_) {
    largest_received_packet_size_ = last_received_packet_info_.length;
  }

  if (perspective_ == Perspective::IS_SERVER &&
      encryption_level_ == ENCRYPTION_INITIAL &&
      last_received_packet_info_.length > packet_creator_.max_packet_length()) {
    if (GetQuicFlag(quic_use_lower_server_response_mtu_for_test)) {
      SetMaxPacketLength(
          std::min(last_received_packet_info_.length, QuicByteCount(1250)));
    } else {
      SetMaxPacketLength(last_received_packet_info_.length);
    }
  }
  return true;
}

bool QuicConnection::ValidateReceivedPacketNumber(
    QuicPacketNumber packet_number) {
  // If this packet has already been seen, or the sender has told us that it
  // will not be retransmitted, then stop processing the packet.
  if (!uber_received_packet_manager_.IsAwaitingPacket(
          last_received_packet_info_.decrypted_level, packet_number)) {
    QUIC_DLOG(INFO) << ENDPOINT << "Packet " << packet_number
                    << " no longer being waited for at level "
                    << static_cast<int>(
                           last_received_packet_info_.decrypted_level)
                    << ".  Discarding.";
    if (debug_visitor_ != nullptr) {
      debug_visitor_->OnDuplicatePacket(packet_number);
    }
    return false;
  }

  return true;
}

void QuicConnection::WriteQueuedPackets() {
  QUICHE_DCHECK(!writer_->IsWriteBlocked());
  QUIC_CLIENT_HISTOGRAM_COUNTS("QuicSession.NumQueuedPacketsBeforeWrite",
                               buffered_packets_.size(), 1, 1000, 50, "");

  while (!buffered_packets_.empty()) {
    if (HandleWriteBlocked()) {
      break;
    }
    const BufferedPacket& packet = buffered_packets_.front();
    WriteResult result = SendPacketToWriter(
        packet.data.get(), packet.length, packet.self_address.host(),
        packet.peer_address, writer_, packet.ecn_codepoint);
    QUIC_DVLOG(1) << ENDPOINT << "Sending buffered packet, result: " << result;
    if (IsMsgTooBig(writer_, result) && packet.length > long_term_mtu_) {
      // When MSG_TOO_BIG is returned, the system typically knows what the
      // actual MTU is, so there is no need to probe further.
      // TODO(wub): Reduce max packet size to a safe default, or the actual MTU.
      mtu_discoverer_.Disable();
      mtu_discovery_alarm_->Cancel();
      buffered_packets_.pop_front();
      continue;
    }
    if (IsWriteError(result.status)) {
      OnWriteError(result.error_code);
      break;
    }
    if (result.status == WRITE_STATUS_OK ||
        result.status == WRITE_STATUS_BLOCKED_DATA_BUFFERED) {
      buffered_packets_.pop_front();
    }
    if (IsWriteBlockedStatus(result.status)) {
      visitor_->OnWriteBlocked();
      break;
    }
  }
}

void QuicConnection::MarkZeroRttPacketsForRetransmission(int reject_reason) {
  sent_packet_manager_.MarkZeroRttPacketsForRetransmission();
  if (debug_visitor_ != nullptr && version().UsesTls()) {
    debug_visitor_->OnZeroRttRejected(reject_reason);
  }
}

void QuicConnection::NeuterUnencryptedPackets() {
  sent_packet_manager_.NeuterUnencryptedPackets();
  // This may have changed the retransmission timer, so re-arm it.
  SetRetransmissionAlarm();
  if (default_enable_5rto_blackhole_detection_) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_default_enable_5rto_blackhole_detection2,
                                 1, 3);
    // Consider this as forward progress since this is called when initial key
    // gets discarded (or previous unencrypted data is not needed anymore).
    OnForwardProgressMade();
  }
  if (SupportsMultiplePacketNumberSpaces()) {
    // Stop sending ack of initial packet number space.
    uber_received_packet_manager_.ResetAckStates(ENCRYPTION_INITIAL);
    // Re-arm ack alarm.
    ack_alarm_->Update(uber_received_packet_manager_.GetEarliestAckTimeout(),
                       kAlarmGranularity);
  }
}

bool QuicConnection::IsMissingDestinationConnectionID() const {
  return peer_issued_cid_manager_ != nullptr &&
         packet_creator_.GetDestinationConnectionId().IsEmpty();
}

bool QuicConnection::ShouldGeneratePacket(
    HasRetransmittableData retransmittable, IsHandshake handshake) {
  QUICHE_DCHECK(handshake != IS_HANDSHAKE ||
                QuicVersionUsesCryptoFrames(transport_version()))
      << ENDPOINT
      << "Handshake in STREAM frames should not check ShouldGeneratePacket";
  if (IsMissingDestinationConnectionID()) {
    QUICHE_DCHECK(version().HasIetfQuicFrames());
    QUIC_CODE_COUNT(quic_generate_packet_blocked_by_no_connection_id);
    QUIC_BUG_IF(quic_bug_90265_1, perspective_ == Perspective::IS_CLIENT);
    QUIC_DLOG(INFO) << ENDPOINT
                    << "There is no destination connection ID available to "
                       "generate packet.";
    return false;
  }
  if (IsDefaultPath(default_path_.self_address,
                    packet_creator_.peer_address())) {
    return CanWrite(retransmittable);
  }
  // This is checking on the alternative path with a different peer address. The
  // self address and the writer used are the same as the default path. In the
  // case of different self address and writer, writing packet would use a
  // differnt code path without checking the states of the default writer.
  return connected_ && !HandleWriteBlocked();
}

const QuicFrames QuicConnection::MaybeBundleOpportunistically() {
  if (!ack_frequency_sent_ && sent_packet_manager_.CanSendAckFrequency()) {
    if (packet_creator_.NextSendingPacketNumber() >=
        FirstSendingPacketNumber() + kMinReceivedBeforeAckDecimation) {
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_can_send_ack_frequency, 3, 3);
      ack_frequency_sent_ = true;
      auto frame = sent_packet_manager_.GetUpdatedAckFrequencyFrame();
      visitor_->SendAckFrequency(frame);
    }
  }

  if (GetQuicRestartFlag(quic_opport_bundle_qpack_decoder_data)) {
    QUIC_RESTART_FLAG_COUNT_N(quic_opport_bundle_qpack_decoder_data, 1, 3);
    visitor_->MaybeBundleOpportunistically();
  }

  if (packet_creator_.flush_ack_in_maybe_bundle() &&
      (packet_creator_.has_ack() || !CanWrite(NO_RETRANSMITTABLE_DATA))) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_flush_ack_in_maybe_bundle, 2, 3);
    return {};
  }

  QuicFrames frames;
  const bool has_pending_ack =
      uber_received_packet_manager_
          .GetAckTimeout(QuicUtils::GetPacketNumberSpace(encryption_level_))
          .IsInitialized();
  if (!has_pending_ack) {
    // No need to send an ACK.
    return frames;
  }
  ResetAckStates();

  QUIC_DVLOG(1) << ENDPOINT << "Bundle an ACK opportunistically";
  QuicFrame updated_ack_frame = GetUpdatedAckFrame();
  QUIC_BUG_IF(quic_bug_12714_23, updated_ack_frame.ack_frame->packets.Empty())
      << ENDPOINT << "Attempted to opportunistically bundle an empty "
      << encryption_level_ << " ACK, " << (has_pending_ack ? "" : "!")
      << "has_pending_ack";
  frames.push_back(updated_ack_frame);
  if (packet_creator_.flush_ack_in_maybe_bundle()) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_flush_ack_in_maybe_bundle, 3, 3);
    const bool flushed = packet_creator_.FlushAckFrame(frames);
    QUIC_BUG_IF(failed_to_flush_ack, !flushed)
        << ENDPOINT << "Failed to flush ACK frame";
    return {};
  }
  // TODO(wub): remove return value when deprecating
  // quic_flush_ack_in_maybe_bundle.
  return frames;
}

bool QuicConnection::CanWrite(HasRetransmittableData retransmittable) {
  if (!connected_) {
    return false;
  }

  if (IsMissingDestinationConnectionID()) {
    return false;
  }

  if (version().CanSendCoalescedPackets() &&
      framer_.HasEncrypterOfEncryptionLevel(ENCRYPTION_INITIAL) &&
      framer_.is_processing_packet()) {
    // While we still have initial keys, suppress sending in mid of packet
    // processing.
    // TODO(fayang): always suppress sending while in the mid of packet
    // processing.
    QUIC_DVLOG(1) << ENDPOINT
                  << "Suppress sending in the mid of packet processing";
    return false;
  }

  if (fill_coalesced_packet_) {
    // Try to coalesce packet, only allow to write when creator is on soft max
    // packet length. Given the next created packet is going to fill current
    // coalesced packet, do not check amplification factor.
    return packet_creator_.HasSoftMaxPacketLength();
  }

  if (sent_packet_manager_.pending_timer_transmission_count() > 0) {
    // Allow sending if there are pending tokens, which occurs when:
    // 1) firing PTO,
    // 2) bundling CRYPTO data with ACKs,
    // 3) coalescing CRYPTO data of higher space.
    return true;
  }

  if (LimitedByAmplificationFactor(packet_creator_.max_packet_length())) {
    // Server is constrained by the amplification restriction.
    QUIC_CODE_COUNT(quic_throttled_by_amplification_limit);
    QUIC_DVLOG(1) << ENDPOINT
                  << "Constrained by amplification restriction to peer address "
                  << default_path_.peer_address << " bytes received "
                  << default_path_.bytes_received_before_address_validation
                  << ", bytes sent"
                  << default_path_.bytes_sent_before_address_validation;
    ++stats_.num_amplification_throttling;
    return false;
  }

  if (HandleWriteBlocked()) {
    return false;
  }

  // Allow acks and probing frames to be sent immediately.
  if (retransmittable == NO_RETRANSMITTABLE_DATA) {
    return true;
  }
  // If the send alarm is set, wait for it to fire.
  if (send_alarm_->IsSet()) {
    return false;
  }

  QuicTime now = clock_->Now();
  QuicTime::Delta delay = sent_packet_manager_.TimeUntilSend(now);
  if (delay.IsInfinite()) {
    send_alarm_->Cancel();
    return false;
  }

  // Scheduler requires a delay.
  if (!delay.IsZero()) {
    if (delay <= release_time_into_future_) {
      // Required delay is within pace time into future, send now.
      return true;
    }
    // Cannot send packet now because delay is too far in the future.
    send_alarm_->Update(now + delay, kAlarmGranularity);
    QUIC_DVLOG(1) << ENDPOINT << "Delaying sending " << delay.ToMilliseconds()
                  << "ms";
    return false;
  }
  return true;
}

QuicTime QuicConnection::CalculatePacketSentTime() {
  const QuicTime now = clock_->Now();
  if (!supports_release_time_) {
    // Don't change the release delay.
    return now;
  }

  auto next_release_time_result = sent_packet_manager_.GetNextReleaseTime();

  // Release before |now| is impossible.
  QuicTime next_release_time =
      std::max(now, next_release_time_result.release_time);
  packet_writer_params_.release_time_delay = next_release_time - now;
  packet_writer_params_.allow_burst = next_release_time_result.allow_burst;
  return next_release_time;
}

bool QuicConnection::WritePacket(SerializedPacket* packet) {
  if (sent_packet_manager_.GetLargestSentPacket().IsInitialized() &&
      packet->packet_number < sent_packet_manager_.GetLargestSentPacket()) {
    QUIC_BUG(quic_bug_10511_23)
        << "Attempt to write packet:" << packet->packet_number
        << " after:" << sent_packet_manager_.GetLargestSentPacket();
    CloseConnection(QUIC_INTERNAL_ERROR, "Packet written out of order.",
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return true;
  }
  const bool is_mtu_discovery = QuicUtils::ContainsFrameType(
      packet->nonretransmittable_frames, MTU_DISCOVERY_FRAME);
  const SerializedPacketFate fate = packet->fate;
  // Termination packets are encrypted and saved, so don't exit early.
  QuicErrorCode error_code = QUIC_NO_ERROR;
  const bool is_termination_packet = IsTerminationPacket(*packet, &error_code);
  QuicPacketNumber packet_number = packet->packet_number;
  QuicPacketLength encrypted_length = packet->encrypted_length;
  // Termination packets are eventually owned by TimeWaitListManager.
  // Others are deleted at the end of this call.
  if (is_termination_packet) {
    if (termination_packets_ == nullptr) {
      termination_packets_.reset(
          new std::vector<std::unique_ptr<QuicEncryptedPacket>>);
    }
    // Copy the buffer so it's owned in the future.
    char* buffer_copy = CopyBuffer(*packet);
    termination_packets_->emplace_back(
        new QuicEncryptedPacket(buffer_copy, encrypted_length, true));
    if (error_code == QUIC_SILENT_IDLE_TIMEOUT) {
      QUICHE_DCHECK_EQ(Perspective::IS_SERVER, perspective_);
      // TODO(fayang): populate histogram indicating the time elapsed from this
      // connection gets closed to following client packets get received.
      QUIC_DVLOG(1) << ENDPOINT
                    << "Added silent connection close to termination packets, "
                       "num of termination packets: "
                    << termination_packets_->size();
      return true;
    }
  }

  QUICHE_DCHECK_LE(encrypted_length, kMaxOutgoingPacketSize);
  QUICHE_DCHECK(is_mtu_discovery ||
                encrypted_length <= packet_creator_.max_packet_length())
      << " encrypted_length=" << encrypted_length
      << " > packet_creator max_packet_length="
      << packet_creator_.max_packet_length();
  QUIC_DVLOG(1) << ENDPOINT << "Sending packet " << packet_number << " : "
                << (IsRetransmittable(*packet) == HAS_RETRANSMITTABLE_DATA
                        ? "data bearing "
                        : " ack or probing only ")
                << ", encryption level: " << packet->encryption_level
                << ", encrypted length:" << encrypted_length
                << ", fate: " << fate << " to peer " << packet->peer_address;
  QUIC_DVLOG(2) << ENDPOINT << packet->encryption_level << " packet number "
                << packet_number << " of length " << encrypted_length << ": "
                << std::endl
                << quiche::QuicheTextUtils::HexDump(absl::string_view(
                       packet->encrypted_buffer, encrypted_length));

  // Measure the RTT from before the write begins to avoid underestimating the
  // min_rtt_, especially in cases where the thread blocks or gets swapped out
  // during the WritePacket below.
  QuicTime packet_send_time = CalculatePacketSentTime();
  WriteResult result(WRITE_STATUS_OK, encrypted_length);
  QuicSocketAddress send_to_address = packet->peer_address;
  QuicSocketAddress send_from_address = self_address();
  if (perspective_ == Perspective::IS_SERVER &&
      sent_server_preferred_address_.IsInitialized() &&
      received_client_addresses_cache_.Lookup(send_to_address) ==
          received_client_addresses_cache_.end()) {
    // Given server has not received packets from send_to_address to
    // self_address(), most NATs do not allow packets from self_address() to
    // send_to_address to go through. Override packet's self address to
    // sent_server_preferred_address_.
    // TODO(b/262386897): server should validate reverse path before changing
    // self address of packets to send.
    send_from_address = sent_server_preferred_address_;
  }
  // Self address is always the default self address on this code path.
  const bool send_on_current_path = send_to_address == peer_address();
  if (!send_on_current_path) {
    QUIC_BUG_IF(quic_send_non_probing_frames_on_alternative_path,
                ContainsNonProbingFrame(*packet))
        << "Packet " << packet->packet_number
        << " with non-probing frames was sent on alternative path: "
           "nonretransmittable_frames: "
        << QuicFramesToString(packet->nonretransmittable_frames)
        << " retransmittable_frames: "
        << QuicFramesToString(packet->retransmittable_frames);
  }
  switch (fate) {
    case DISCARD:
      ++stats_.packets_discarded;
      if (debug_visitor_ != nullptr) {
        debug_visitor_->OnPacketDiscarded(*packet);
      }
      return true;
    case COALESCE:
      QUIC_BUG_IF(quic_bug_12714_24,
                  !version().CanSendCoalescedPackets() || coalescing_done_);
      if (!coalesced_packet_.MaybeCoalescePacket(
              *packet, send_from_address, send_to_address,
              helper_->GetStreamSendBufferAllocator(),
              packet_creator_.max_packet_length(),
              GetEcnCodepointToSend(send_to_address))) {
        // Failed to coalesce packet, flush current coalesced packet.
        if (!FlushCoalescedPacket()) {
          QUIC_BUG_IF(quic_connection_connected_after_flush_coalesced_failure,
                      connected_)
              << "QUIC connection is still connected after failing to flush "
                 "coalesced packet.";
          // Failed to flush coalesced packet, write error has been handled.
          return false;
        }
        if (!coalesced_packet_.MaybeCoalescePacket(
                *packet, send_from_address, send_to_address,
                helper_->GetStreamSendBufferAllocator(),
                packet_creator_.max_packet_length(),
                GetEcnCodepointToSend(send_to_address))) {
          // Failed to coalesce packet even it is the only packet, raise a write
          // error.
          QUIC_DLOG(ERROR) << ENDPOINT << "Failed to coalesce packet";
          result.error_code = WRITE_STATUS_FAILED_TO_COALESCE_PACKET;
          break;
        }
      }
      if (coalesced_packet_.length() < coalesced_packet_.max_packet_length()) {
        QUIC_DVLOG(1) << ENDPOINT << "Trying to set soft max packet length to "
                      << coalesced_packet_.max_packet_length() -
                             coalesced_packet_.length();
        packet_creator_.SetSoftMaxPacketLength(
            coalesced_packet_.max_packet_length() - coalesced_packet_.length());
      }
      last_ecn_codepoint_sent_ = coalesced_packet_.ecn_codepoint();
      break;
    case BUFFER:
      QUIC_DVLOG(1) << ENDPOINT << "Adding packet: " << packet->packet_number
                    << " to buffered packets";
      last_ecn_codepoint_sent_ = GetEcnCodepointToSend(send_to_address);
      buffered_packets_.emplace_back(*packet, send_from_address,
                                     send_to_address, last_ecn_codepoint_sent_);
      break;
    case SEND_TO_WRITER:
      // Stop using coalescer from now on.
      coalescing_done_ = true;
      // At this point, packet->release_encrypted_buffer is either nullptr,
      // meaning |packet->encrypted_buffer| is a stack buffer, or not-nullptr,
      /// meaning it's a writer-allocated buffer. Note that connectivity probing
      // packets do not use this function, so setting release_encrypted_buffer
      // to nullptr will not cause probing packets to be leaked.
      //
      // writer_->WritePacket transfers buffer ownership back to the writer.
      packet->release_encrypted_buffer = nullptr;
      result = SendPacketToWriter(
          packet->encrypted_buffer, encrypted_length, send_from_address.host(),
          send_to_address, writer_, GetEcnCodepointToSend(send_to_address));
      // This is a work around for an issue with linux UDP GSO batch writers.
      // When sending a GSO packet with 2 segments, if the first segment is
      // larger than the path MTU, instead of EMSGSIZE, the linux kernel returns
      // EINVAL, which translates to WRITE_STATUS_ERROR and causes conneciton to
      // be closed. By manually flush the writer here, the MTU probe is sent in
      // a normal(non-GSO) packet, so the kernel can return EMSGSIZE and we will
      // not close the connection.
      if (is_mtu_discovery && writer_->IsBatchMode()) {
        result = writer_->Flush();
      }
      break;
    default:
      QUICHE_DCHECK(false);
      break;
  }

  QUIC_HISTOGRAM_ENUM(
      "QuicConnection.WritePacketStatus", result.status,
      WRITE_STATUS_NUM_VALUES,
      "Status code returned by writer_->WritePacket() in QuicConnection.");

  if (IsWriteBlockedStatus(result.status)) {
    // Ensure the writer is still write blocked, otherwise QUIC may continue
    // trying to write when it will not be able to.
    QUICHE_DCHECK(writer_->IsWriteBlocked());
    visitor_->OnWriteBlocked();
    // If the socket buffers the data, then the packet should not
    // be queued and sent again, which would result in an unnecessary
    // duplicate packet being sent.  The helper must call OnCanWrite
    // when the write completes, and OnWriteError if an error occurs.
    if (result.status != WRITE_STATUS_BLOCKED_DATA_BUFFERED) {
      QUIC_DVLOG(1) << ENDPOINT << "Adding packet: " << packet->packet_number
                    << " to buffered packets";
      buffered_packets_.emplace_back(*packet, send_from_address,
                                     send_to_address, last_ecn_codepoint_sent_);
    }
  }

  // In some cases, an MTU probe can cause EMSGSIZE. This indicates that the
  // MTU discovery is permanently unsuccessful.
  if (IsMsgTooBig(writer_, result)) {
    if (is_mtu_discovery) {
      // When MSG_TOO_BIG is returned, the system typically knows what the
      // actual MTU is, so there is no need to probe further.
      // TODO(wub): Reduce max packet size to a safe default, or the actual MTU.
      QUIC_DVLOG(1) << ENDPOINT
                    << " MTU probe packet too big, size:" << encrypted_length
                    << ", long_term_mtu_:" << long_term_mtu_;
      mtu_discoverer_.Disable();
      mtu_discovery_alarm_->Cancel();
      // The write failed, but the writer is not blocked, so return true.
      return true;
    }
    if (!send_on_current_path) {
      // Only handle MSG_TOO_BIG as error on current path.
      return true;
    }
  }

  if (IsWriteError(result.status)) {
    QUIC_LOG_FIRST_N(ERROR, 10)
        << ENDPOINT << "Failed writing packet " << packet_number << " of "
        << encrypted_length << " bytes from " << send_from_address.host()
        << " to " << send_to_address << ", with error code "
        << result.error_code << ". long_term_mtu_:" << long_term_mtu_
        << ", previous_validated_mtu_:" << previous_validated_mtu_
        << ", max_packet_length():" << max_packet_length()
        << ", is_mtu_discovery:" << is_mtu_discovery;
    if (MaybeRevertToPreviousMtu()) {
      return true;
    }

    OnWriteError(result.error_code);
    return false;
  }

  if (result.status == WRITE_STATUS_OK) {
    // packet_send_time is the ideal send time, if allow_burst is true, writer
    // may have sent it earlier than that.
    packet_send_time = packet_send_time + result.send_time_offset;
  }

  if (IsRetransmittable(*packet) == HAS_RETRANSMITTABLE_DATA &&
      !is_termination_packet) {
    // Start blackhole/path degrading detections if the sent packet is not
    // termination packet and contains retransmittable data.
    // Do not restart detection if detection is in progress indicating no
    // forward progress has been made since last event (i.e., packet was sent
    // or new packets were acknowledged).
    if (!blackhole_detector_.IsDetectionInProgress()) {
      // Try to start detections if no detection in progress. This could
      // because either both detections are inactive when sending last packet
      // or this connection just gets out of quiescence.
      blackhole_detector_.RestartDetection(GetPathDegradingDeadline(),
                                           GetNetworkBlackholeDeadline(),
                                           GetPathMtuReductionDeadline());
    }
    idle_network_detector_.OnPacketSent(packet_send_time,
                                        sent_packet_manager_.GetPtoDelay());
  }

  MaybeSetMtuAlarm(packet_number);
  QUIC_DVLOG(1) << ENDPOINT << "time we began writing last sent packet: "
                << packet_send_time.ToDebuggingValue();

  if (IsDefaultPath(default_path_.self_address, send_to_address)) {
    if (EnforceAntiAmplificationLimit()) {
      // Include bytes sent even if they are not in flight.
      default_path_.bytes_sent_before_address_validation += encrypted_length;
    }
  } else {
    MaybeUpdateBytesSentToAlternativeAddress(send_to_address, encrypted_length);
  }

  // Do not measure rtt of this packet if it's not sent on current path.
  QUIC_DLOG_IF(INFO, !send_on_current_path)
      << ENDPOINT << " Sent packet " << packet->packet_number
      << " on a different path with remote address " << send_to_address
      << " while current path has peer address " << peer_address();
  const bool in_flight = sent_packet_manager_.OnPacketSent(
      packet, packet_send_time, packet->transmission_type,
      IsRetransmittable(*packet), /*measure_rtt=*/send_on_current_path,
      last_ecn_codepoint_sent_);
  QUIC_BUG_IF(quic_bug_12714_25,
              perspective_ == Perspective::IS_SERVER &&
                  default_enable_5rto_blackhole_detection_ &&
                  blackhole_detector_.IsDetectionInProgress() &&
                  !sent_packet_manager_.HasInFlightPackets())
      << ENDPOINT
      << "Trying to start blackhole detection without no bytes in flight";

  if (debug_visitor_ != nullptr) {
    if (sent_packet_manager_.unacked_packets().empty()) {
      QUIC_BUG(quic_bug_10511_25)
          << "Unacked map is empty right after packet is sent";
    } else {
      debug_visitor_->OnPacketSent(
          packet->packet_number, packet->encrypted_length,
          packet->has_crypto_handshake, packet->transmission_type,
          packet->encryption_level,
          sent_packet_manager_.unacked_packets()
              .rbegin()
              ->retransmittable_frames,
          packet->nonretransmittable_frames, packet_send_time, result.batch_id);
    }
  }
  if (packet->encryption_level == ENCRYPTION_HANDSHAKE) {
    handshake_packet_sent_ = true;
  }

  if (packet->encryption_level == ENCRYPTION_FORWARD_SECURE) {
    if (!lowest_packet_sent_in_current_key_phase_.IsInitialized()) {
      QUIC_DLOG(INFO) << ENDPOINT
                      << "lowest_packet_sent_in_current_key_phase_ = "
                      << packet_number;
      lowest_packet_sent_in_current_key_phase_ = packet_number;
    }
    if (!is_termination_packet &&
        MaybeHandleAeadConfidentialityLimits(*packet)) {
      return true;
    }
  }
  if (in_flight || !retransmission_alarm_->IsSet()) {
    SetRetransmissionAlarm();
  }
  SetPingAlarm();
  RetirePeerIssuedConnectionIdsNoLongerOnPath();

  // The packet number length must be updated after OnPacketSent, because it
  // may change the packet number length in packet.
  packet_creator_.UpdatePacketNumberLength(
      sent_packet_manager_.GetLeastPacketAwaitedByPeer(encryption_level_),
      sent_packet_manager_.EstimateMaxPacketsInFlight(max_packet_length()));

  stats_.bytes_sent += encrypted_length;
  ++stats_.packets_sent;
  if (packet->has_ack_ecn) {
    stats_.num_ack_frames_sent_with_ecn++;
  }

  QuicByteCount bytes_not_retransmitted =
      packet->bytes_not_retransmitted.value_or(0);
  if (packet->transmission_type != NOT_RETRANSMISSION) {
    if (static_cast<uint64_t>(encrypted_length) < bytes_not_retransmitted) {
      QUIC_BUG(quic_packet_bytes_written_lt_bytes_not_retransmitted)
          << "Total bytes written to the packet should be larger than the "
             "bytes in not-retransmitted frames. Bytes written: "
          << encrypted_length
          << ", bytes not retransmitted: " << bytes_not_retransmitted;
    } else {
      // bytes_retransmitted includes packet's headers and encryption
      // overhead.
      stats_.bytes_retransmitted +=
          (encrypted_length - bytes_not_retransmitted);
    }
    ++stats_.packets_retransmitted;
  }

  return true;
}

bool QuicConnection::MaybeHandleAeadConfidentialityLimits(
    const SerializedPacket& packet) {
  if (!version().UsesTls()) {
    return false;
  }

  if (packet.encryption_level != ENCRYPTION_FORWARD_SECURE) {
    QUIC_BUG(quic_bug_12714_26)
        << "MaybeHandleAeadConfidentialityLimits called on non 1-RTT packet";
    return false;
  }
  if (!lowest_packet_sent_in_current_key_phase_.IsInitialized()) {
    QUIC_BUG(quic_bug_10511_26)
        << "lowest_packet_sent_in_current_key_phase_ must be initialized "
           "before calling MaybeHandleAeadConfidentialityLimits";
    return false;
  }

  // Calculate the number of packets encrypted from the packet number, which is
  // simpler than keeping another counter. The packet number space may be
  // sparse, so this might overcount, but doing a key update earlier than
  // necessary would only improve security and has negligible cost.
  if (packet.packet_number < lowest_packet_sent_in_current_key_phase_) {
    const std::string error_details =
        absl::StrCat("packet_number(", packet.packet_number.ToString(),
                     ") < lowest_packet_sent_in_current_key_phase_ (",
                     lowest_packet_sent_in_current_key_phase_.ToString(), ")");
    QUIC_BUG(quic_bug_10511_27) << error_details;
    CloseConnection(QUIC_INTERNAL_ERROR, error_details,
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return true;
  }
  const QuicPacketCount num_packets_encrypted_in_current_key_phase =
      packet.packet_number - lowest_packet_sent_in_current_key_phase_ + 1;

  const QuicPacketCount confidentiality_limit =
      framer_.GetOneRttEncrypterConfidentialityLimit();

  // Attempt to initiate a key update before reaching the AEAD
  // confidentiality limit when the number of packets sent in the current
  // key phase gets within |kKeyUpdateConfidentialityLimitOffset| packets of
  // the limit, unless overridden by
  // FLAGS_quic_key_update_confidentiality_limit.
  constexpr QuicPacketCount kKeyUpdateConfidentialityLimitOffset = 1000;
  QuicPacketCount key_update_limit = 0;
  if (confidentiality_limit > kKeyUpdateConfidentialityLimitOffset) {
    key_update_limit =
        confidentiality_limit - kKeyUpdateConfidentialityLimitOffset;
  }
  const QuicPacketCount key_update_limit_override =
      GetQuicFlag(quic_key_update_confidentiality_limit);
  if (key_update_limit_override) {
    key_update_limit = key_update_limit_override;
  }

  QUIC_DVLOG(2) << ENDPOINT << "Checking AEAD confidentiality limits: "
                << "num_packets_encrypted_in_current_key_phase="
                << num_packets_encrypted_in_current_key_phase
                << " key_update_limit=" << key_update_limit
                << " confidentiality_limit=" << confidentiality_limit
                << " IsKeyUpdateAllowed()=" << IsKeyUpdateAllowed();

  if (num_packets_encrypted_in_current_key_phase >= confidentiality_limit) {
    // Reached the confidentiality limit without initiating a key update,
    // must close the connection.
    const std::string error_details = absl::StrCat(
        "encrypter confidentiality limit reached: "
        "num_packets_encrypted_in_current_key_phase=",
        num_packets_encrypted_in_current_key_phase,
        " key_update_limit=", key_update_limit,
        " confidentiality_limit=", confidentiality_limit,
        " IsKeyUpdateAllowed()=", IsKeyUpdateAllowed());
    CloseConnection(QUIC_AEAD_LIMIT_REACHED, error_details,
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return true;
  }

  if (IsKeyUpdateAllowed() &&
      num_packets_encrypted_in_current_key_phase >= key_update_limit) {
    // Approaching the confidentiality limit, initiate key update so that
    // the next set of keys will be ready for the next packet before the
    // limit is reached.
    KeyUpdateReason reason = KeyUpdateReason::kLocalAeadConfidentialityLimit;
    if (key_update_limit_override) {
      QUIC_DLOG(INFO) << ENDPOINT
                      << "reached FLAGS_quic_key_update_confidentiality_limit, "
                         "initiating key update: "
                      << "num_packets_encrypted_in_current_key_phase="
                      << num_packets_encrypted_in_current_key_phase
                      << " key_update_limit=" << key_update_limit
                      << " confidentiality_limit=" << confidentiality_limit;
      reason = KeyUpdateReason::kLocalKeyUpdateLimitOverride;
    } else {
      QUIC_DLOG(INFO) << ENDPOINT
                      << "approaching AEAD confidentiality limit, "
                         "initiating key update: "
                      << "num_packets_encrypted_in_current_key_phase="
                      << num_packets_encrypted_in_current_key_phase
                      << " key_update_limit=" << key_update_limit
                      << " confidentiality_limit=" << confidentiality_limit;
    }
    InitiateKeyUpdate(reason);
  }

  return false;
}

void QuicConnection::FlushPackets() {
  if (!connected_) {
    return;
  }

  if (!writer_->IsBatchMode()) {
    return;
  }

  if (HandleWriteBlocked()) {
    QUIC_DLOG(INFO) << ENDPOINT << "FlushPackets called while blocked.";
    return;
  }

  WriteResult result = writer_->Flush();

  QUIC_HISTOGRAM_ENUM("QuicConnection.FlushPacketStatus", result.status,
                      WRITE_STATUS_NUM_VALUES,
                      "Status code returned by writer_->Flush() in "
                      "QuicConnection::FlushPackets.");

  if (HandleWriteBlocked()) {
    QUICHE_DCHECK_EQ(WRITE_STATUS_BLOCKED, result.status)
        << "Unexpected flush result:" << result;
    QUIC_DLOG(INFO) << ENDPOINT << "Write blocked in FlushPackets.";
    return;
  }

  if (IsWriteError(result.status) && !MaybeRevertToPreviousMtu()) {
    OnWriteError(result.error_code);
  }
}

bool QuicConnection::IsMsgTooBig(const QuicPacketWriter* writer,
                                 const WriteResult& result) {
  absl::optional<int> writer_error_code = writer->MessageTooBigErrorCode();
  return (result.status == WRITE_STATUS_MSG_TOO_BIG) ||
         (writer_error_code.has_value() && IsWriteError(result.status) &&
          result.error_code == *writer_error_code);
}

bool QuicConnection::ShouldDiscardPacket(EncryptionLevel encryption_level) {
  if (!connected_) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Not sending packet as connection is disconnected.";
    return true;
  }

  if (encryption_level_ == ENCRYPTION_FORWARD_SECURE &&
      encryption_level == ENCRYPTION_INITIAL) {
    // Drop packets that are NULL encrypted since the peer won't accept them
    // anymore.
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Dropping NULL encrypted packet since the connection is "
                       "forward secure.";
    return true;
  }

  return false;
}

QuicTime QuicConnection::GetPathMtuReductionDeadline() const {
  if (previous_validated_mtu_ == 0) {
    return QuicTime::Zero();
  }
  QuicTime::Delta delay = sent_packet_manager_.GetMtuReductionDelay(
      num_rtos_for_blackhole_detection_);
  if (delay.IsZero()) {
    return QuicTime::Zero();
  }
  return clock_->ApproximateNow() + delay;
}

bool QuicConnection::MaybeRevertToPreviousMtu() {
  if (previous_validated_mtu_ == 0) {
    return false;
  }

  SetMaxPacketLength(previous_validated_mtu_);
  mtu_discoverer_.Disable();
  mtu_discovery_alarm_->Cancel();
  previous_validated_mtu_ = 0;
  return true;
}

void QuicConnection::OnWriteError(int error_code) {
  if (write_error_occurred_) {
    // A write error already occurred. The connection is being closed.
    return;
  }
  write_error_occurred_ = true;

  const std::string error_details = absl::StrCat(
      "Write failed with error: ", error_code, " (", strerror(error_code), ")");
  QUIC_LOG_FIRST_N(ERROR, 2) << ENDPOINT << error_details;
  absl::optional<int> writer_error_code = writer_->MessageTooBigErrorCode();
  if (writer_error_code.has_value() && error_code == *writer_error_code) {
    CloseConnection(QUIC_PACKET_WRITE_ERROR, error_details,
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  // We can't send an error as the socket is presumably borked.
  QUIC_CODE_COUNT(quic_tear_down_local_connection_on_write_error_ietf);
  CloseConnection(QUIC_PACKET_WRITE_ERROR, error_details,
                  ConnectionCloseBehavior::SILENT_CLOSE);
}

QuicPacketBuffer QuicConnection::GetPacketBuffer() {
  if (version().CanSendCoalescedPackets() && !coalescing_done_) {
    // Do not use writer's packet buffer for coalesced packets which may
    // contain multiple QUIC packets.
    return {nullptr, nullptr};
  }
  return writer_->GetNextWriteLocation(self_address().host(), peer_address());
}

void QuicConnection::OnSerializedPacket(SerializedPacket serialized_packet) {
  if (serialized_packet.encrypted_buffer == nullptr) {
    // We failed to serialize the packet, so close the connection.
    // Specify that the close is silent, that no packet be sent, so no infinite
    // loop here.
    // TODO(ianswett): This is actually an internal error, not an
    // encryption failure.
    QUIC_CODE_COUNT(quic_tear_down_local_connection_on_serialized_packet_ietf);
    CloseConnection(QUIC_ENCRYPTION_FAILURE,
                    "Serialized packet does not have an encrypted buffer.",
                    ConnectionCloseBehavior::SILENT_CLOSE);
    return;
  }

  if (serialized_packet.retransmittable_frames.empty()) {
    // Increment consecutive_num_packets_with_no_retransmittable_frames_ if
    // this packet is a new transmission with no retransmittable frames.
    ++consecutive_num_packets_with_no_retransmittable_frames_;
  } else {
    consecutive_num_packets_with_no_retransmittable_frames_ = 0;
  }
  if (retransmittable_on_wire_behavior_ == SEND_FIRST_FORWARD_SECURE_PACKET &&
      first_serialized_one_rtt_packet_ == nullptr &&
      serialized_packet.encryption_level == ENCRYPTION_FORWARD_SECURE) {
    first_serialized_one_rtt_packet_ = std::make_unique<BufferedPacket>(
        serialized_packet, self_address(), peer_address(),
        GetEcnCodepointToSend(peer_address()));
  }
  SendOrQueuePacket(std::move(serialized_packet));
}

void QuicConnection::OnUnrecoverableError(QuicErrorCode error,
                                          const std::string& error_details) {
  // The packet creator or generator encountered an unrecoverable error: tear
  // down local connection state immediately.
  QUIC_CODE_COUNT(quic_tear_down_local_connection_on_unrecoverable_error_ietf);
  CloseConnection(error, error_details, ConnectionCloseBehavior::SILENT_CLOSE);
}

void QuicConnection::OnCongestionChange() {
  visitor_->OnCongestionWindowChange(clock_->ApproximateNow());

  // Uses the connection's smoothed RTT. If zero, uses initial_rtt.
  QuicTime::Delta rtt = sent_packet_manager_.GetRttStats()->smoothed_rtt();
  if (rtt.IsZero()) {
    rtt = sent_packet_manager_.GetRttStats()->initial_rtt();
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnRttChanged(rtt);
  }
}

void QuicConnection::OnPathMtuIncreased(QuicPacketLength packet_size) {
  if (packet_size > max_packet_length()) {
    previous_validated_mtu_ = max_packet_length();
    SetMaxPacketLength(packet_size);
    mtu_discoverer_.OnMaxPacketLengthUpdated(previous_validated_mtu_,
                                             max_packet_length());
  }
}

void QuicConnection::OnInFlightEcnPacketAcked() {
  QUIC_BUG_IF(quic_bug_518619343_01, !GetQuicReloadableFlag(quic_send_ect1))
      << "Unexpected call to OnInFlightEcnPacketAcked()";
  // Only packets on the default path are in-flight.
  if (!default_path_.ecn_marked_packet_acked) {
    QUIC_DVLOG(1) << ENDPOINT << "First ECT packet acked on active path.";
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_send_ect1, 2, 8);
    default_path_.ecn_marked_packet_acked = true;
  }
}

void QuicConnection::OnInvalidEcnFeedback() {
  QUIC_BUG_IF(quic_bug_518619343_02, !GetQuicReloadableFlag(quic_send_ect1))
      << "Unexpected call to OnInvalidEcnFeedback().";
  if (disable_ecn_codepoint_validation_) {
    // In some tests, senders may send ECN marks in patterns that are not
    // in accordance with the spec, and should not fail validation as a result.
    return;
  }
  QUIC_DVLOG(1) << ENDPOINT << "ECN feedback is invalid, stop marking.";
  packet_writer_params_.ecn_codepoint = ECN_NOT_ECT;
}

std::unique_ptr<QuicSelfIssuedConnectionIdManager>
QuicConnection::MakeSelfIssuedConnectionIdManager() {
  QUICHE_DCHECK((perspective_ == Perspective::IS_CLIENT &&
                 !default_path_.client_connection_id.IsEmpty()) ||
                (perspective_ == Perspective::IS_SERVER &&
                 !default_path_.server_connection_id.IsEmpty()));
  return std::make_unique<QuicSelfIssuedConnectionIdManager>(
      kMinNumOfActiveConnectionIds,
      perspective_ == Perspective::IS_CLIENT
          ? default_path_.client_connection_id
          : default_path_.server_connection_id,
      clock_, alarm_factory_, this, context(), connection_id_generator_);
}

void QuicConnection::MaybeSendConnectionIdToClient() {
  if (perspective_ == Perspective::IS_CLIENT) {
    return;
  }
  QUICHE_DCHECK(self_issued_cid_manager_ != nullptr);
  self_issued_cid_manager_->MaybeSendNewConnectionIds();
}

void QuicConnection::OnHandshakeComplete() {
  sent_packet_manager_.SetHandshakeConfirmed();
  if (version().HasIetfQuicFrames() && perspective_ == Perspective::IS_SERVER &&
      self_issued_cid_manager_ != nullptr) {
    self_issued_cid_manager_->MaybeSendNewConnectionIds();
  }
  if (send_ack_frequency_on_handshake_completion_ &&
      sent_packet_manager_.CanSendAckFrequency()) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_can_send_ack_frequency, 2, 3);
    auto ack_frequency_frame =
        sent_packet_manager_.GetUpdatedAckFrequencyFrame();
    // This AckFrequencyFrame is meant to only update the max_ack_delay. Set
    // packet tolerance to the default value for now.
    ack_frequency_frame.packet_tolerance =
        kDefaultRetransmittablePacketsBeforeAck;
    visitor_->SendAckFrequency(ack_frequency_frame);
    if (!connected_) {
      return;
    }
  }
  // This may have changed the retransmission timer, so re-arm it.
  SetRetransmissionAlarm();
  if (default_enable_5rto_blackhole_detection_) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_default_enable_5rto_blackhole_detection2,
                                 2, 3);
    OnForwardProgressMade();
  }
  if (!SupportsMultiplePacketNumberSpaces()) {
    // The client should immediately ack the SHLO to confirm the handshake is
    // complete with the server.
    if (perspective_ == Perspective::IS_CLIENT && ack_frame_updated()) {
      ack_alarm_->Update(clock_->ApproximateNow(), QuicTime::Delta::Zero());
    }
    return;
  }
  // Stop sending ack of handshake packet number space.
  uber_received_packet_manager_.ResetAckStates(ENCRYPTION_HANDSHAKE);
  // Re-arm ack alarm.
  ack_alarm_->Update(uber_received_packet_manager_.GetEarliestAckTimeout(),
                     kAlarmGranularity);
  if (!accelerated_server_preferred_address_ &&
      received_server_preferred_address_.IsInitialized()) {
    QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, perspective_);
    visitor_->OnServerPreferredAddressAvailable(
        received_server_preferred_address_);
  }
}

void QuicConnection::MaybeCreateMultiPortPath() {
  QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, perspective_);
  QUIC_CLIENT_HISTOGRAM_BOOL(
      "QuicConnection.ServerAllowsActiveMigrationForMultiPort",
      !active_migration_disabled_,
      "Whether the server allows active migration that's required for "
      "multi-port");
  if (active_migration_disabled_) {
    return;
  }
  if (path_validator_.HasPendingPathValidation()) {
    QUIC_CLIENT_HISTOGRAM_ENUM("QuicConnection.MultiPortPathCreationCancelled",
                               path_validator_.GetPathValidationReason(),
                               PathValidationReason::kMaxValue,
                               "Reason for cancelled multi port path creation");
    return;
  }
  if (multi_port_stats_->num_multi_port_paths_created >=
      kMaxNumMultiPortPaths) {
    return;
  }

  auto context_observer = std::make_unique<ContextObserver>(this);
  visitor_->CreateContextForMultiPortPath(std::move(context_observer));
}

void QuicConnection::SendOrQueuePacket(SerializedPacket packet) {
  // The caller of this function is responsible for checking CanWrite().
  WritePacket(&packet);
}

void QuicConnection::SendAck() {
  QUICHE_DCHECK(!SupportsMultiplePacketNumberSpaces());
  QUIC_DVLOG(1) << ENDPOINT << "Sending an ACK proactively";
  QuicFrames frames;
  frames.push_back(GetUpdatedAckFrame());
  if (!packet_creator_.FlushAckFrame(frames)) {
    return;
  }
  ResetAckStates();
  if (!ShouldBundleRetransmittableFrameWithAck()) {
    return;
  }
  consecutive_num_packets_with_no_retransmittable_frames_ = 0;
  if (packet_creator_.HasPendingRetransmittableFrames() ||
      visitor_->WillingAndAbleToWrite()) {
    // There are pending retransmittable frames.
    return;
  }

  visitor_->OnAckNeedsRetransmittableFrame();
}

EncryptionLevel QuicConnection::GetEncryptionLevelToSendPingForSpace(
    PacketNumberSpace space) const {
  switch (space) {
    case INITIAL_DATA:
      return ENCRYPTION_INITIAL;
    case HANDSHAKE_DATA:
      return ENCRYPTION_HANDSHAKE;
    case APPLICATION_DATA:
      return framer_.GetEncryptionLevelToSendApplicationData();
    default:
      QUICHE_DCHECK(false);
      return NUM_ENCRYPTION_LEVELS;
  }
}

bool QuicConnection::IsKnownServerAddress(
    const QuicSocketAddress& address) const {
  QUICHE_DCHECK(address.IsInitialized());
  return std::find(known_server_addresses_.cbegin(),
                   known_server_addresses_.cend(),
                   address) != known_server_addresses_.cend();
}

QuicEcnCodepoint QuicConnection::GetEcnCodepointToSend(
    const QuicSocketAddress& destination_address) const {
  // Don't send ECN marks on alternate paths. Sending ECN marks might
  // cause the connectivity check to fail on some networks.
  if (destination_address != peer_address()) {
    return ECN_NOT_ECT;
  }
  // If the path might drop ECN marked packets, send retransmission without
  // them.
  if (in_probe_time_out_ && !default_path_.ecn_marked_packet_acked) {
    return ECN_NOT_ECT;
  }
  return packet_writer_params_.ecn_codepoint;
}

WriteResult QuicConnection::SendPacketToWriter(
    const char* buffer, size_t buf_len, const QuicIpAddress& self_address,
    const QuicSocketAddress& destination_address, QuicPacketWriter* writer,
    const QuicEcnCodepoint ecn_codepoint) {
  QuicPacketWriterParams params = packet_writer_params_;
  params.ecn_codepoint = ecn_codepoint;
  last_ecn_codepoint_sent_ = ecn_codepoint;
  WriteResult result =
      writer->WritePacket(buffer, buf_len, self_address, destination_address,
                          per_packet_options_, params);
  return result;
}

void QuicConnection::OnRetransmissionTimeout() {
  ScopedRetransmissionTimeoutIndicator indicator(this);
#ifndef NDEBUG
  if (sent_packet_manager_.unacked_packets().empty()) {
    QUICHE_DCHECK(sent_packet_manager_.handshake_mode_disabled());
    QUICHE_DCHECK(!IsHandshakeConfirmed());
  }
#endif
  if (!connected_) {
    return;
  }

  QuicPacketNumber previous_created_packet_number =
      packet_creator_.packet_number();
  const auto retransmission_mode =
      sent_packet_manager_.OnRetransmissionTimeout();
  if (retransmission_mode == QuicSentPacketManager::PTO_MODE) {
    // Skip a packet number when PTO fires to elicit an immediate ACK.
    const QuicPacketCount num_packet_numbers_to_skip = 1;
    packet_creator_.SkipNPacketNumbers(
        num_packet_numbers_to_skip,
        sent_packet_manager_.GetLeastPacketAwaitedByPeer(encryption_level_),
        sent_packet_manager_.EstimateMaxPacketsInFlight(max_packet_length()));
    previous_created_packet_number += num_packet_numbers_to_skip;
    if (debug_visitor_ != nullptr) {
      debug_visitor_->OnNPacketNumbersSkipped(num_packet_numbers_to_skip,
                                              clock_->Now());
    }
  }
  if (default_enable_5rto_blackhole_detection_ &&
      !sent_packet_manager_.HasInFlightPackets() &&
      blackhole_detector_.IsDetectionInProgress()) {
    // Stop detection in quiescence.
    QUICHE_DCHECK_EQ(QuicSentPacketManager::LOSS_MODE, retransmission_mode);
    blackhole_detector_.StopDetection(/*permanent=*/false);
  }
  WriteIfNotBlocked();

  // A write failure can result in the connection being closed, don't attempt to
  // write further packets, or to set alarms.
  if (!connected_) {
    return;
  }
  // When PTO fires, the SentPacketManager gives the connection the opportunity
  // to send new data before retransmitting.
  sent_packet_manager_.MaybeSendProbePacket();

  if (packet_creator_.packet_number() == previous_created_packet_number &&
      retransmission_mode == QuicSentPacketManager::PTO_MODE &&
      !visitor_->WillingAndAbleToWrite()) {
    // Send PING if timer fires in PTO mode but there is no data to send.
    QUIC_DLOG(INFO) << ENDPOINT
                    << "No packet gets sent when timer fires in mode "
                    << retransmission_mode << ", send PING";
    QUICHE_DCHECK_LT(0u,
                     sent_packet_manager_.pending_timer_transmission_count());
    if (SupportsMultiplePacketNumberSpaces()) {
      // Based on https://datatracker.ietf.org/doc/html/rfc9002#appendix-A.9
      PacketNumberSpace packet_number_space;
      if (sent_packet_manager_
              .GetEarliestPacketSentTimeForPto(&packet_number_space)
              .IsInitialized()) {
        SendPingAtLevel(
            GetEncryptionLevelToSendPingForSpace(packet_number_space));
      } else {
        // The client must PTO when there is nothing in flight if the server
        // could be blocked from sending by the amplification limit
        QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, perspective_);
        if (framer_.HasEncrypterOfEncryptionLevel(ENCRYPTION_HANDSHAKE)) {
          SendPingAtLevel(ENCRYPTION_HANDSHAKE);
        } else if (framer_.HasEncrypterOfEncryptionLevel(ENCRYPTION_INITIAL)) {
          SendPingAtLevel(ENCRYPTION_INITIAL);
        } else {
          QUIC_BUG(quic_bug_no_pto) << "PTO fired but nothing was sent.";
        }
      }
    } else {
      SendPingAtLevel(encryption_level_);
    }
  }
  if (retransmission_mode == QuicSentPacketManager::PTO_MODE) {
    // When timer fires in PTO mode, ensure 1) at least one packet is created,
    // or there is data to send and available credit (such that packets will be
    // sent eventually).
    QUIC_BUG_IF(
        quic_bug_12714_27,
        packet_creator_.packet_number() == previous_created_packet_number &&
            (!visitor_->WillingAndAbleToWrite() ||
             sent_packet_manager_.pending_timer_transmission_count() == 0u))
        << "retransmission_mode: " << retransmission_mode
        << ", packet_number: " << packet_creator_.packet_number()
        << ", session has data to write: " << visitor_->WillingAndAbleToWrite()
        << ", writer is blocked: " << writer_->IsWriteBlocked()
        << ", pending_timer_transmission_count: "
        << sent_packet_manager_.pending_timer_transmission_count();
  }

  // Ensure the retransmission alarm is always set if there are unacked packets
  // and nothing waiting to be sent.
  // This happens if the loss algorithm invokes a timer based loss, but the
  // packet doesn't need to be retransmitted.
  if (!HasQueuedData() && !retransmission_alarm_->IsSet()) {
    SetRetransmissionAlarm();
  }
  if (packet_writer_params_.ecn_codepoint == ECN_NOT_ECT ||
      default_path_.ecn_marked_packet_acked) {
    return;
  }
  ++default_path_.ecn_pto_count;
  if (default_path_.ecn_pto_count == kEcnPtoLimit) {
    // Give up on ECN. There are two scenarios:
    // 1. All packets are suffering PTO. In this case, the connection
    // abandons ECN after 1 failed ECT(1) flight and one failed Not-ECT
    // flight.
    // 2. Only ECN packets are suffering PTO. In that case, alternating
    // flights will have ECT(1). On the second ECT(1) failure, the
    // connection will abandon.
    // This behavior is in the range of acceptable choices in S13.4.2 of RFC
    // 9000.
    QUIC_DVLOG(1) << ENDPOINT << "ECN packets PTO 3 times.";
    OnInvalidEcnFeedback();
  }
}

void QuicConnection::SetEncrypter(EncryptionLevel level,
                                  std::unique_ptr<QuicEncrypter> encrypter) {
  packet_creator_.SetEncrypter(level, std::move(encrypter));
}

void QuicConnection::RemoveEncrypter(EncryptionLevel level) {
  framer_.RemoveEncrypter(level);
}

void QuicConnection::SetDiversificationNonce(
    const DiversificationNonce& nonce) {
  QUICHE_DCHECK_EQ(Perspective::IS_SERVER, perspective_);
  packet_creator_.SetDiversificationNonce(nonce);
}

void QuicConnection::SetDefaultEncryptionLevel(EncryptionLevel level) {
  QUIC_DVLOG(1) << ENDPOINT << "Setting default encryption level from "
                << encryption_level_ << " to " << level;
  const bool changing_level = level != encryption_level_;
  if (changing_level && packet_creator_.HasPendingFrames()) {
    // Flush all queued frames when encryption level changes.
    ScopedPacketFlusher flusher(this);
    packet_creator_.FlushCurrentPacket();
  }
  encryption_level_ = level;
  packet_creator_.set_encryption_level(level);
  QUIC_BUG_IF(quic_bug_12714_28, !framer_.HasEncrypterOfEncryptionLevel(level))
      << ENDPOINT << "Trying to set encryption level to "
      << EncryptionLevelToString(level) << " while the key is missing";

  if (!changing_level) {
    return;
  }
  // The least packet awaited by the peer depends on the encryption level so
  // we recalculate it here.
  packet_creator_.UpdatePacketNumberLength(
      sent_packet_manager_.GetLeastPacketAwaitedByPeer(encryption_level_),
      sent_packet_manager_.EstimateMaxPacketsInFlight(max_packet_length()));
}

void QuicConnection::SetDecrypter(EncryptionLevel level,
                                  std::unique_ptr<QuicDecrypter> decrypter) {
  framer_.SetDecrypter(level, std::move(decrypter));

  if (!undecryptable_packets_.empty() &&
      !process_undecryptable_packets_alarm_->IsSet()) {
    process_undecryptable_packets_alarm_->Set(clock_->ApproximateNow());
  }
}

void QuicConnection::SetAlternativeDecrypter(
    EncryptionLevel level, std::unique_ptr<QuicDecrypter> decrypter,
    bool latch_once_used) {
  framer_.SetAlternativeDecrypter(level, std::move(decrypter), latch_once_used);

  if (!undecryptable_packets_.empty() &&
      !process_undecryptable_packets_alarm_->IsSet()) {
    process_undecryptable_packets_alarm_->Set(clock_->ApproximateNow());
  }
}

void QuicConnection::InstallDecrypter(
    EncryptionLevel level, std::unique_ptr<QuicDecrypter> decrypter) {
  if (level == ENCRYPTION_ZERO_RTT) {
    had_zero_rtt_decrypter_ = true;
  }
  framer_.InstallDecrypter(level, std::move(decrypter));
  if (!undecryptable_packets_.empty() &&
      !process_undecryptable_packets_alarm_->IsSet()) {
    process_undecryptable_packets_alarm_->Set(clock_->ApproximateNow());
  }
}

void QuicConnection::RemoveDecrypter(EncryptionLevel level) {
  framer_.RemoveDecrypter(level);
}

void QuicConnection::DiscardPreviousOneRttKeys() {
  framer_.DiscardPreviousOneRttKeys();
}

bool QuicConnection::IsKeyUpdateAllowed() const {
  return support_key_update_for_connection_ &&
         GetLargestAckedPacket().IsInitialized() &&
         lowest_packet_sent_in_current_key_phase_.IsInitialized() &&
         GetLargestAckedPacket() >= lowest_packet_sent_in_current_key_phase_;
}

bool QuicConnection::HaveSentPacketsInCurrentKeyPhaseButNoneAcked() const {
  return lowest_packet_sent_in_current_key_phase_.IsInitialized() &&
         (!GetLargestAckedPacket().IsInitialized() ||
          GetLargestAckedPacket() < lowest_packet_sent_in_current_key_phase_);
}

QuicPacketCount QuicConnection::PotentialPeerKeyUpdateAttemptCount() const {
  return framer_.PotentialPeerKeyUpdateAttemptCount();
}

bool QuicConnection::InitiateKeyUpdate(KeyUpdateReason reason) {
  QUIC_DLOG(INFO) << ENDPOINT << "InitiateKeyUpdate";
  if (!IsKeyUpdateAllowed()) {
    QUIC_BUG(quic_bug_10511_28) << "key update not allowed";
    return false;
  }
  return framer_.DoKeyUpdate(reason);
}

const QuicDecrypter* QuicConnection::decrypter() const {
  return framer_.decrypter();
}

const QuicDecrypter* QuicConnection::alternative_decrypter() const {
  return framer_.alternative_decrypter();
}

void QuicConnection::QueueUndecryptablePacket(
    const QuicEncryptedPacket& packet, EncryptionLevel decryption_level) {
  for (const auto& saved_packet : undecryptable_packets_) {
    if (packet.data() == saved_packet.packet->data() &&
        packet.length() == saved_packet.packet->length()) {
      QUIC_DVLOG(1) << ENDPOINT << "Not queueing known undecryptable packet";
      return;
    }
  }
  QUIC_DVLOG(1) << ENDPOINT << "Queueing undecryptable packet.";
  undecryptable_packets_.emplace_back(packet, decryption_level,
                                      last_received_packet_info_);
  if (perspective_ == Perspective::IS_CLIENT) {
    SetRetransmissionAlarm();
  }
}

void QuicConnection::MaybeProcessUndecryptablePackets() {
  process_undecryptable_packets_alarm_->Cancel();

  if (undecryptable_packets_.empty() ||
      encryption_level_ == ENCRYPTION_INITIAL) {
    return;
  }

  auto iter = undecryptable_packets_.begin();
  while (connected_ && iter != undecryptable_packets_.end()) {
    // Making sure there is no pending frames when processing next undecrypted
    // packet because the queued ack frame may change.
    packet_creator_.FlushCurrentPacket();
    if (!connected_) {
      return;
    }
    UndecryptablePacket* undecryptable_packet = &*iter;
    QUIC_DVLOG(1) << ENDPOINT << "Attempting to process undecryptable packet";
    if (debug_visitor_ != nullptr) {
      debug_visitor_->OnAttemptingToProcessUndecryptablePacket(
          undecryptable_packet->encryption_level);
    }
    last_received_packet_info_ = undecryptable_packet->packet_info;
    current_packet_data_ = undecryptable_packet->packet->data();
    const bool processed = framer_.ProcessPacket(*undecryptable_packet->packet);
    current_packet_data_ = nullptr;

    if (processed) {
      QUIC_DVLOG(1) << ENDPOINT << "Processed undecryptable packet!";
      iter = undecryptable_packets_.erase(iter);
      ++stats_.packets_processed;
      continue;
    }
    const bool has_decryption_key = version().KnowsWhichDecrypterToUse() &&
                                    framer_.HasDecrypterOfEncryptionLevel(
                                        undecryptable_packet->encryption_level);
    if (framer_.error() == QUIC_DECRYPTION_FAILURE &&
        ShouldEnqueueUnDecryptablePacket(undecryptable_packet->encryption_level,
                                         has_decryption_key)) {
      QUIC_DVLOG(1)
          << ENDPOINT
          << "Need to attempt to process this undecryptable packet later";
      ++iter;
      continue;
    }
    iter = undecryptable_packets_.erase(iter);
  }

  // Once handshake is complete, there will be no new keys installed and hence
  // any undecryptable packets will never be able to be decrypted.
  if (IsHandshakeComplete()) {
    if (debug_visitor_ != nullptr) {
      for (const auto& undecryptable_packet : undecryptable_packets_) {
        debug_visitor_->OnUndecryptablePacket(
            undecryptable_packet.encryption_level, /*dropped=*/true);
      }
    }
    undecryptable_packets_.clear();
  }
  if (perspective_ == Perspective::IS_CLIENT) {
    SetRetransmissionAlarm();
  }
}

void QuicConnection::QueueCoalescedPacket(const QuicEncryptedPacket& packet) {
  QUIC_DVLOG(1) << ENDPOINT << "Queueing coalesced packet.";
  received_coalesced_packets_.push_back(packet.Clone());
  ++stats_.num_coalesced_packets_received;
}

bool QuicConnection::MaybeProcessCoalescedPackets() {
  bool processed = false;
  while (connected_ && !received_coalesced_packets_.empty()) {
    // Making sure there are no pending frames when processing the next
    // coalesced packet because the queued ack frame may change.
    packet_creator_.FlushCurrentPacket();
    if (!connected_) {
      return processed;
    }

    std::unique_ptr<QuicEncryptedPacket> packet =
        std::move(received_coalesced_packets_.front());
    received_coalesced_packets_.pop_front();

    QUIC_DVLOG(1) << ENDPOINT << "Processing coalesced packet";
    if (framer_.ProcessPacket(*packet)) {
      processed = true;
      ++stats_.num_coalesced_packets_processed;
    } else {
      // If we are unable to decrypt this packet, it might be
      // because the CHLO or SHLO packet was lost.
    }
  }
  if (processed) {
    MaybeProcessUndecryptablePackets();
    MaybeSendInResponseToPacket();
  }
  return processed;
}

void QuicConnection::CloseConnection(
    QuicErrorCode error, const std::string& details,
    ConnectionCloseBehavior connection_close_behavior) {
  CloseConnection(error, NO_IETF_QUIC_ERROR, details,
                  connection_close_behavior);
}

void QuicConnection::CloseConnection(
    QuicErrorCode error, QuicIetfTransportErrorCodes ietf_error,
    const std::string& error_details,
    ConnectionCloseBehavior connection_close_behavior) {
  QUICHE_DCHECK(!error_details.empty());
  if (!connected_) {
    QUIC_DLOG(INFO) << "Connection is already closed.";
    return;
  }

  if (ietf_error != NO_IETF_QUIC_ERROR) {
    QUIC_DLOG(INFO) << ENDPOINT << "Closing connection: " << connection_id()
                    << ", with wire error: " << ietf_error
                    << ", error: " << QuicErrorCodeToString(error)
                    << ", and details:  " << error_details;
  } else {
    QUIC_DLOG(INFO) << ENDPOINT << "Closing connection: " << connection_id()
                    << ", with error: " << QuicErrorCodeToString(error) << " ("
                    << error << "), and details:  " << error_details;
  }

  if (connection_close_behavior != ConnectionCloseBehavior::SILENT_CLOSE) {
    SendConnectionClosePacket(error, ietf_error, error_details);
  }

  TearDownLocalConnectionState(error, ietf_error, error_details,
                               ConnectionCloseSource::FROM_SELF);
}

void QuicConnection::SendConnectionClosePacket(
    QuicErrorCode error, QuicIetfTransportErrorCodes ietf_error,
    const std::string& details) {
  // Always use the current path to send CONNECTION_CLOSE.
  QuicPacketCreator::ScopedPeerAddressContext context(
      &packet_creator_, peer_address(), default_path_.client_connection_id,
      default_path_.server_connection_id);
  if (!SupportsMultiplePacketNumberSpaces()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Sending connection close packet.";
    ScopedEncryptionLevelContext context(this,
                                         GetConnectionCloseEncryptionLevel());
    if (version().CanSendCoalescedPackets()) {
      coalesced_packet_.Clear();
    }
    ClearQueuedPackets();
    // If there was a packet write error, write the smallest close possible.
    ScopedPacketFlusher flusher(this);
    // Always bundle an ACK with connection close for debugging purpose.
    if (error != QUIC_PACKET_WRITE_ERROR &&
        !uber_received_packet_manager_.IsAckFrameEmpty(
            QuicUtils::GetPacketNumberSpace(encryption_level_)) &&
        !packet_creator_.has_ack()) {
      SendAck();
    }
    QuicConnectionCloseFrame* frame;

    frame = new QuicConnectionCloseFrame(transport_version(), error, ietf_error,
                                         details,
                                         framer_.current_received_frame_type());
    packet_creator_.ConsumeRetransmittableControlFrame(QuicFrame(frame));
    packet_creator_.FlushCurrentPacket();
    if (version().CanSendCoalescedPackets()) {
      FlushCoalescedPacket();
    }
    ClearQueuedPackets();
    return;
  }
  ScopedPacketFlusher flusher(this);

  // Now that the connection is being closed, discard any unsent packets
  // so the only packets to be sent will be connection close packets.
  if (version().CanSendCoalescedPackets()) {
    coalesced_packet_.Clear();
  }
  ClearQueuedPackets();

  for (EncryptionLevel level :
       {ENCRYPTION_INITIAL, ENCRYPTION_HANDSHAKE, ENCRYPTION_ZERO_RTT,
        ENCRYPTION_FORWARD_SECURE}) {
    if (!framer_.HasEncrypterOfEncryptionLevel(level)) {
      continue;
    }
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Sending connection close packet at level: " << level;
    ScopedEncryptionLevelContext context(this, level);
    // Bundle an ACK of the corresponding packet number space for debugging
    // purpose.
    if (error != QUIC_PACKET_WRITE_ERROR &&
        !uber_received_packet_manager_.IsAckFrameEmpty(
            QuicUtils::GetPacketNumberSpace(encryption_level_)) &&
        !packet_creator_.has_ack()) {
      QuicFrames frames;
      frames.push_back(GetUpdatedAckFrame());
      packet_creator_.FlushAckFrame(frames);
    }

    if (level == ENCRYPTION_FORWARD_SECURE &&
        perspective_ == Perspective::IS_SERVER) {
      visitor_->BeforeConnectionCloseSent();
    }

    auto* frame = new QuicConnectionCloseFrame(
        transport_version(), error, ietf_error, details,
        framer_.current_received_frame_type());
    packet_creator_.ConsumeRetransmittableControlFrame(QuicFrame(frame));
    packet_creator_.FlushCurrentPacket();
  }
  if (version().CanSendCoalescedPackets()) {
    FlushCoalescedPacket();
  }
  // Since the connection is closing, if the connection close packets were not
  // sent, then they should be discarded.
  ClearQueuedPackets();
}

void QuicConnection::TearDownLocalConnectionState(
    QuicErrorCode error, QuicIetfTransportErrorCodes ietf_error,
    const std::string& error_details, ConnectionCloseSource source) {
  QuicConnectionCloseFrame frame(transport_version(), error, ietf_error,
                                 error_details,
                                 framer_.current_received_frame_type());
  return TearDownLocalConnectionState(frame, source);
}

void QuicConnection::TearDownLocalConnectionState(
    const QuicConnectionCloseFrame& frame, ConnectionCloseSource source) {
  if (!connected_) {
    QUIC_DLOG(INFO) << "Connection is already closed.";
    return;
  }

  // If we are using a batch writer, flush packets queued in it, if any.
  FlushPackets();
  connected_ = false;
  QUICHE_DCHECK(visitor_ != nullptr);
  visitor_->OnConnectionClosed(frame, source);
  // LossDetectionTunerInterface::Finish() may be called from
  // sent_packet_manager_.OnConnectionClosed. Which may require the session to
  // finish its business first.
  sent_packet_manager_.OnConnectionClosed();
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnConnectionClosed(frame, source);
  }
  // Cancel the alarms so they don't trigger any action now that the
  // connection is closed.
  CancelAllAlarms();
  CancelPathValidation();

  peer_issued_cid_manager_.reset();
  self_issued_cid_manager_.reset();
}

void QuicConnection::CancelAllAlarms() {
  QUIC_DVLOG(1) << "Cancelling all QuicConnection alarms.";

  ack_alarm_->PermanentCancel();
  ping_manager_.Stop();
  retransmission_alarm_->PermanentCancel();
  send_alarm_->PermanentCancel();
  mtu_discovery_alarm_->PermanentCancel();
  process_undecryptable_packets_alarm_->PermanentCancel();
  discard_previous_one_rtt_keys_alarm_->PermanentCancel();
  discard_zero_rtt_decryption_keys_alarm_->PermanentCancel();
  multi_port_probing_alarm_->PermanentCancel();
  blackhole_detector_.StopDetection(/*permanent=*/true);
  idle_network_detector_.StopDetection();
}

QuicByteCount QuicConnection::max_packet_length() const {
  return packet_creator_.max_packet_length();
}

void QuicConnection::SetMaxPacketLength(QuicByteCount length) {
  long_term_mtu_ = length;
  stats_.max_egress_mtu = std::max(stats_.max_egress_mtu, long_term_mtu_);
  packet_creator_.SetMaxPacketLength(GetLimitedMaxPacketSize(length));
}

bool QuicConnection::HasQueuedData() const {
  return packet_creator_.HasPendingFrames() || !buffered_packets_.empty();
}

void QuicConnection::SetNetworkTimeouts(QuicTime::Delta handshake_timeout,
                                        QuicTime::Delta idle_timeout) {
  QUIC_BUG_IF(quic_bug_12714_29, idle_timeout > handshake_timeout)
      << "idle_timeout:" << idle_timeout.ToMilliseconds()
      << " handshake_timeout:" << handshake_timeout.ToMilliseconds();
  // Adjust the idle timeout on client and server to prevent clients from
  // sending requests to servers which have already closed the connection.
  if (perspective_ == Perspective::IS_SERVER) {
    idle_timeout = idle_timeout + QuicTime::Delta::FromSeconds(3);
  } else if (idle_timeout > QuicTime::Delta::FromSeconds(1)) {
    idle_timeout = idle_timeout - QuicTime::Delta::FromSeconds(1);
  }
  idle_network_detector_.SetTimeouts(handshake_timeout, idle_timeout);
}

void QuicConnection::SetPingAlarm() {
  if (!connected_) {
    return;
  }
  ping_manager_.SetAlarm(clock_->ApproximateNow(),
                         visitor_->ShouldKeepConnectionAlive(),
                         sent_packet_manager_.HasInFlightPackets());
}

void QuicConnection::SetRetransmissionAlarm() {
  if (!connected_) {
    if (retransmission_alarm_->IsSet()) {
      QUIC_BUG(quic_bug_10511_29)
          << ENDPOINT << "Retransmission alarm is set while disconnected";
      retransmission_alarm_->Cancel();
    }
    return;
  }
  if (packet_creator_.PacketFlusherAttached()) {
    pending_retransmission_alarm_ = true;
    return;
  }
  if (LimitedByAmplificationFactor(packet_creator_.max_packet_length())) {
    // Do not set retransmission timer if connection is anti-amplification limit
    // throttled. Otherwise, nothing can be sent when timer fires.
    retransmission_alarm_->Cancel();
    return;
  }
  PacketNumberSpace packet_number_space;
  if (SupportsMultiplePacketNumberSpaces() && !IsHandshakeConfirmed() &&
      !sent_packet_manager_
           .GetEarliestPacketSentTimeForPto(&packet_number_space)
           .IsInitialized()) {
    // Before handshake gets confirmed, GetEarliestPacketSentTimeForPto
    // returning 0 indicates no packets are in flight or only application data
    // is in flight.
    if (perspective_ == Perspective::IS_SERVER) {
      // No need to arm PTO on server side.
      retransmission_alarm_->Cancel();
      return;
    }
    if (retransmission_alarm_->IsSet() &&
        GetRetransmissionDeadline() > retransmission_alarm_->deadline()) {
      // Do not postpone armed PTO on the client side.
      return;
    }
  }

  retransmission_alarm_->Update(GetRetransmissionDeadline(), kAlarmGranularity);
}

void QuicConnection::MaybeSetMtuAlarm(QuicPacketNumber sent_packet_number) {
  if (mtu_discovery_alarm_->IsSet() ||
      !mtu_discoverer_.ShouldProbeMtu(sent_packet_number)) {
    return;
  }
  mtu_discovery_alarm_->Set(clock_->ApproximateNow());
}

QuicConnection::ScopedPacketFlusher::ScopedPacketFlusher(
    QuicConnection* connection)
    : connection_(connection),
      flush_and_set_pending_retransmission_alarm_on_delete_(false),
      handshake_packet_sent_(connection != nullptr &&
                             connection->handshake_packet_sent_) {
  if (connection_ == nullptr) {
    return;
  }

  if (!connection_->packet_creator_.PacketFlusherAttached()) {
    flush_and_set_pending_retransmission_alarm_on_delete_ = true;
    connection->packet_creator_.AttachPacketFlusher();
  }
}

QuicConnection::ScopedPacketFlusher::~ScopedPacketFlusher() {
  if (connection_ == nullptr || !connection_->connected()) {
    return;
  }

  if (flush_and_set_pending_retransmission_alarm_on_delete_) {
    const QuicTime ack_timeout =
        connection_->uber_received_packet_manager_.GetEarliestAckTimeout();
    if (ack_timeout.IsInitialized()) {
      if (ack_timeout <= connection_->clock_->ApproximateNow() &&
          !connection_->CanWrite(NO_RETRANSMITTABLE_DATA)) {
        // Cancel ACK alarm if connection is write blocked, and ACK will be
        // sent when connection gets unblocked.
        connection_->ack_alarm_->Cancel();
      } else if (!connection_->ack_alarm_->IsSet() ||
                 connection_->ack_alarm_->deadline() > ack_timeout) {
        connection_->ack_alarm_->Update(ack_timeout, QuicTime::Delta::Zero());
      }
    }
    if (connection_->ack_alarm_->IsSet() &&
        connection_->ack_alarm_->deadline() <=
            connection_->clock_->ApproximateNow()) {
      // An ACK needs to be sent right now. This ACK did not get bundled
      // because either there was no data to write or packets were marked as
      // received after frames were queued in the generator.
      if (connection_->send_alarm_->IsSet() &&
          connection_->send_alarm_->deadline() <=
              connection_->clock_->ApproximateNow()) {
        // If send alarm will go off soon, let send alarm send the ACK.
        connection_->ack_alarm_->Cancel();
      } else if (connection_->SupportsMultiplePacketNumberSpaces()) {
        connection_->SendAllPendingAcks();
      } else {
        connection_->SendAck();
      }
    }

    // INITIAL or HANDSHAKE retransmission could cause peer to derive new
    // keys, such that the buffered undecryptable packets may be processed.
    // This endpoint would derive an inflated RTT sample when receiving ACKs
    // of those undecryptable packets. To mitigate this, tries to coalesce as
    // many higher space packets as possible (via for loop inside
    // MaybeCoalescePacketOfHigherSpace) to fill the remaining space in the
    // coalescer.
    if (connection_->version().CanSendCoalescedPackets()) {
      connection_->MaybeCoalescePacketOfHigherSpace();
    }
    connection_->packet_creator_.Flush();
    if (connection_->version().CanSendCoalescedPackets()) {
      connection_->FlushCoalescedPacket();
    }
    connection_->FlushPackets();

    if (!connection_->connected()) {
      return;
    }

    if (!handshake_packet_sent_ && connection_->handshake_packet_sent_) {
      // This would cause INITIAL key to be dropped. Drop keys here to avoid
      // missing the write keys in the middle of writing.
      connection_->visitor_->OnHandshakePacketSent();
    }
    // Reset transmission type.
    connection_->SetTransmissionType(NOT_RETRANSMISSION);

    // Once all transmissions are done, check if there is any outstanding data
    // to send and notify the congestion controller if not.
    //
    // Note that this means that the application limited check will happen as
    // soon as the last flusher gets destroyed, which is typically after a
    // single stream write is finished.  This means that if all the data from a
    // single write goes through the connection, the application-limited signal
    // will fire even if the caller does a write operation immediately after.
    // There are two important approaches to remedy this situation:
    // (1) Instantiate ScopedPacketFlusher before performing multiple subsequent
    //     writes, thus deferring this check until all writes are done.
    // (2) Write data in chunks sufficiently large so that they cause the
    //     connection to be limited by the congestion control.  Typically, this
    //     would mean writing chunks larger than the product of the current
    //     pacing rate and the pacer granularity.  So, for instance, if the
    //     pacing rate of the connection is 1 Gbps, and the pacer granularity is
    //     1 ms, the caller should send at least 125k bytes in order to not
    //     be marked as application-limited.
    connection_->CheckIfApplicationLimited();

    if (connection_->pending_retransmission_alarm_) {
      connection_->SetRetransmissionAlarm();
      connection_->pending_retransmission_alarm_ = false;
    }
  }
  QUICHE_DCHECK_EQ(flush_and_set_pending_retransmission_alarm_on_delete_,
                   !connection_->packet_creator_.PacketFlusherAttached());
}

QuicConnection::ScopedEncryptionLevelContext::ScopedEncryptionLevelContext(
    QuicConnection* connection, EncryptionLevel encryption_level)
    : connection_(connection), latched_encryption_level_(ENCRYPTION_INITIAL) {
  if (connection_ == nullptr) {
    return;
  }
  latched_encryption_level_ = connection_->encryption_level_;
  connection_->SetDefaultEncryptionLevel(encryption_level);
}

QuicConnection::ScopedEncryptionLevelContext::~ScopedEncryptionLevelContext() {
  if (connection_ == nullptr || !connection_->connected_) {
    return;
  }
  connection_->SetDefaultEncryptionLevel(latched_encryption_level_);
}

QuicConnection::BufferedPacket::BufferedPacket(
    const SerializedPacket& packet, const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address, const QuicEcnCodepoint ecn_codepoint)
    : BufferedPacket(packet.encrypted_buffer, packet.encrypted_length,
                     self_address, peer_address, ecn_codepoint) {}

QuicConnection::BufferedPacket::BufferedPacket(
    const char* encrypted_buffer, QuicPacketLength encrypted_length,
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address, const QuicEcnCodepoint ecn_codepoint)
    : length(encrypted_length),
      self_address(self_address),
      peer_address(peer_address),
      ecn_codepoint(ecn_codepoint) {
  data = std::make_unique<char[]>(encrypted_length);
  memcpy(data.get(), encrypted_buffer, encrypted_length);
}

QuicConnection::BufferedPacket::BufferedPacket(
    QuicRandom& random, QuicPacketLength encrypted_length,
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address)
    : length(encrypted_length),
      self_address(self_address),
      peer_address(peer_address) {
  data = std::make_unique<char[]>(encrypted_length);
  random.RandBytes(data.get(), encrypted_length);
}

QuicConnection::ReceivedPacketInfo::ReceivedPacketInfo(QuicTime receipt_time)
    : receipt_time(receipt_time) {}
QuicConnection::ReceivedPacketInfo::ReceivedPacketInfo(
    const QuicSocketAddress& destination_address,
    const QuicSocketAddress& source_address, QuicTime receipt_time,
    QuicByteCount length, QuicEcnCodepoint ecn_codepoint)
    : destination_address(destination_address),
      source_address(source_address),
      receipt_time(receipt_time),
      length(length),
      ecn_codepoint(ecn_codepoint) {}

std::ostream& operator<<(std::ostream& os,
                         const QuicConnection::ReceivedPacketInfo& info) {
  os << " { destination_address: " << info.destination_address.ToString()
     << ", source_address: " << info.source_address.ToString()
     << ", received_bytes_counted: " << info.received_bytes_counted
     << ", length: " << info.length
     << ", destination_connection_id: " << info.destination_connection_id;
  if (!info.decrypted) {
    os << " }\n";
    return os;
  }
  os << ", decrypted: " << info.decrypted
     << ", decrypted_level: " << EncryptionLevelToString(info.decrypted_level)
     << ", header: " << info.header << ", frames: ";
  for (const auto frame : info.frames) {
    os << frame;
  }
  os << " }\n";
  return os;
}

HasRetransmittableData QuicConnection::IsRetransmittable(
    const SerializedPacket& packet) {
  // Retransmitted packets retransmittable frames are owned by the unacked
  // packet map, but are not present in the serialized packet.
  if (packet.transmission_type != NOT_RETRANSMISSION ||
      !packet.retransmittable_frames.empty()) {
    return HAS_RETRANSMITTABLE_DATA;
  } else {
    return NO_RETRANSMITTABLE_DATA;
  }
}

bool QuicConnection::IsTerminationPacket(const SerializedPacket& packet,
                                         QuicErrorCode* error_code) {
  if (packet.retransmittable_frames.empty()) {
    return false;
  }
  for (const QuicFrame& frame : packet.retransmittable_frames) {
    if (frame.type == CONNECTION_CLOSE_FRAME) {
      *error_code = frame.connection_close_frame->quic_error_code;
      return true;
    }
  }
  return false;
}

void QuicConnection::SetMtuDiscoveryTarget(QuicByteCount target) {
  QUIC_DVLOG(2) << ENDPOINT << "SetMtuDiscoveryTarget: " << target;
  mtu_discoverer_.Disable();
  mtu_discoverer_.Enable(max_packet_length(), GetLimitedMaxPacketSize(target));
}

QuicByteCount QuicConnection::GetLimitedMaxPacketSize(
    QuicByteCount suggested_max_packet_size) {
  if (!peer_address().IsInitialized()) {
    QUIC_BUG(quic_bug_10511_30)
        << "Attempted to use a connection without a valid peer address";
    return suggested_max_packet_size;
  }

  const QuicByteCount writer_limit = writer_->GetMaxPacketSize(peer_address());

  QuicByteCount max_packet_size = suggested_max_packet_size;
  if (max_packet_size > writer_limit) {
    max_packet_size = writer_limit;
  }
  if (max_packet_size > peer_max_packet_size_) {
    max_packet_size = peer_max_packet_size_;
  }
  if (max_packet_size > kMaxOutgoingPacketSize) {
    max_packet_size = kMaxOutgoingPacketSize;
  }
  return max_packet_size;
}

void QuicConnection::SendMtuDiscoveryPacket(QuicByteCount target_mtu) {
  // Currently, this limit is ensured by the caller.
  QUICHE_DCHECK_EQ(target_mtu, GetLimitedMaxPacketSize(target_mtu));

  // Send the probe.
  packet_creator_.GenerateMtuDiscoveryPacket(target_mtu);
}

// TODO(zhongyi): change this method to generate a connectivity probing packet
// and let the caller to call writer to write the packet and handle write
// status.
bool QuicConnection::SendConnectivityProbingPacket(
    QuicPacketWriter* probing_writer, const QuicSocketAddress& peer_address) {
  QUICHE_DCHECK(peer_address.IsInitialized());
  if (!connected_) {
    QUIC_BUG(quic_bug_10511_31)
        << "Not sending connectivity probing packet as connection is "
        << "disconnected.";
    return false;
  }
  if (perspective_ == Perspective::IS_SERVER && probing_writer == nullptr) {
    // Server can use default packet writer to write packet.
    probing_writer = writer_;
  }
  QUICHE_DCHECK(probing_writer);

  if (probing_writer->IsWriteBlocked()) {
    QUIC_DLOG(INFO)
        << ENDPOINT
        << "Writer blocked when sending connectivity probing packet.";
    if (probing_writer == writer_) {
      // Visitor should not be write blocked if the probing writer is not the
      // default packet writer.
      visitor_->OnWriteBlocked();
    }
    return true;
  }

  QUIC_DLOG(INFO) << ENDPOINT
                  << "Sending path probe packet for connection_id = "
                  << default_path_.server_connection_id;

  std::unique_ptr<SerializedPacket> probing_packet;
  if (!version().HasIetfQuicFrames()) {
    // Non-IETF QUIC, generate a padded ping regardless of whether this is a
    // request or a response.
    probing_packet = packet_creator_.SerializeConnectivityProbingPacket();
  } else {
    // IETF QUIC path challenge.
    // Send a path probe request using IETF QUIC PATH_CHALLENGE frame.
    QuicPathFrameBuffer transmitted_connectivity_probe_payload;
    random_generator_->RandBytes(&transmitted_connectivity_probe_payload,
                                 sizeof(QuicPathFrameBuffer));
    probing_packet =
        packet_creator_.SerializePathChallengeConnectivityProbingPacket(
            transmitted_connectivity_probe_payload);
  }
  QUICHE_DCHECK_EQ(IsRetransmittable(*probing_packet), NO_RETRANSMITTABLE_DATA);
  return WritePacketUsingWriter(std::move(probing_packet), probing_writer,
                                self_address(), peer_address,
                                /*measure_rtt=*/true);
}

bool QuicConnection::WritePacketUsingWriter(
    std::unique_ptr<SerializedPacket> packet, QuicPacketWriter* writer,
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address, bool measure_rtt) {
  const QuicTime packet_send_time = clock_->Now();
  QUIC_BUG_IF(write using blocked writer, writer->IsWriteBlocked());
  QUIC_DVLOG(2) << ENDPOINT
                << "Sending path probe packet for server connection ID "
                << default_path_.server_connection_id << std::endl
                << quiche::QuicheTextUtils::HexDump(absl::string_view(
                       packet->encrypted_buffer, packet->encrypted_length));
  WriteResult result = SendPacketToWriter(
      packet->encrypted_buffer, packet->encrypted_length, self_address.host(),
      peer_address, writer, GetEcnCodepointToSend(peer_address));

  const uint32_t writer_batch_id = result.batch_id;

  // If using a batch writer and the probing packet is buffered, flush it.
  if (writer->IsBatchMode() && result.status == WRITE_STATUS_OK &&
      result.bytes_written == 0) {
    result = writer->Flush();
  }

  if (IsWriteError(result.status)) {
    // Write error for any connectivity probe should not affect the connection
    // as it is sent on a different path.
    QUIC_DLOG(INFO) << ENDPOINT << "Write probing packet failed with error = "
                    << result.error_code;
    return false;
  }

  // Send in currrent path. Call OnPacketSent regardless of the write result.
  sent_packet_manager_.OnPacketSent(
      packet.get(), packet_send_time, packet->transmission_type,
      NO_RETRANSMITTABLE_DATA, measure_rtt, last_ecn_codepoint_sent_);

  if (debug_visitor_ != nullptr) {
    if (sent_packet_manager_.unacked_packets().empty()) {
      QUIC_BUG(quic_bug_10511_32)
          << "Unacked map is empty right after packet is sent";
    } else {
      debug_visitor_->OnPacketSent(
          packet->packet_number, packet->encrypted_length,
          packet->has_crypto_handshake, packet->transmission_type,
          packet->encryption_level,
          sent_packet_manager_.unacked_packets()
              .rbegin()
              ->retransmittable_frames,
          packet->nonretransmittable_frames, packet_send_time, writer_batch_id);
    }
  }

  if (IsWriteBlockedStatus(result.status)) {
    if (writer == writer_) {
      // Visitor should not be write blocked if the probing writer is not the
      // default packet writer.
      visitor_->OnWriteBlocked();
    }
    if (result.status == WRITE_STATUS_BLOCKED_DATA_BUFFERED) {
      QUIC_DLOG(INFO) << ENDPOINT << "Write probing packet blocked";
    }
  }

  return true;
}

void QuicConnection::DisableMtuDiscovery() {
  mtu_discoverer_.Disable();
  mtu_discovery_alarm_->Cancel();
}

void QuicConnection::DiscoverMtu() {
  QUICHE_DCHECK(!mtu_discovery_alarm_->IsSet());

  const QuicPacketNumber largest_sent_packet =
      sent_packet_manager_.GetLargestSentPacket();
  if (mtu_discoverer_.ShouldProbeMtu(largest_sent_packet)) {
    ++mtu_probe_count_;
    SendMtuDiscoveryPacket(
        mtu_discoverer_.GetUpdatedMtuProbeSize(largest_sent_packet));
  }
  QUICHE_DCHECK(!mtu_discovery_alarm_->IsSet());
}

void QuicConnection::OnEffectivePeerMigrationValidated(
    bool /*is_migration_linkable*/) {
  if (active_effective_peer_migration_type_ == NO_CHANGE) {
    QUIC_BUG(quic_bug_10511_33) << "No migration underway.";
    return;
  }
  highest_packet_sent_before_effective_peer_migration_.Clear();
  const bool send_address_token =
      active_effective_peer_migration_type_ != PORT_CHANGE;
  active_effective_peer_migration_type_ = NO_CHANGE;
  ++stats_.num_validated_peer_migration;
  if (!framer_.version().HasIetfQuicFrames()) {
    return;
  }
  if (debug_visitor_ != nullptr) {
    const QuicTime now = clock_->ApproximateNow();
    if (now >= stats_.handshake_completion_time) {
      debug_visitor_->OnPeerMigrationValidated(
          now - stats_.handshake_completion_time);
    } else {
      QUIC_BUG(quic_bug_10511_34)
          << "Handshake completion time is larger than current time.";
    }
  }

  // Lift anti-amplification limit.
  default_path_.validated = true;
  alternative_path_.Clear();
  if (send_address_token) {
    visitor_->MaybeSendAddressToken();
  }
}

void QuicConnection::StartEffectivePeerMigration(AddressChangeType type) {
  // TODO(fayang): Currently, all peer address change type are allowed. Need to
  // add a method ShouldAllowPeerAddressChange(PeerAddressChangeType type) to
  // determine whether |type| is allowed.
  if (!framer_.version().HasIetfQuicFrames()) {
    if (type == NO_CHANGE) {
      QUIC_BUG(quic_bug_10511_35)
          << "EffectivePeerMigration started without address change.";
      return;
    }
    QUIC_DLOG(INFO)
        << ENDPOINT << "Effective peer's ip:port changed from "
        << default_path_.peer_address.ToString() << " to "
        << GetEffectivePeerAddressFromCurrentPacket().ToString()
        << ", address change type is " << type
        << ", migrating connection without validating new client address.";

    highest_packet_sent_before_effective_peer_migration_ =
        sent_packet_manager_.GetLargestSentPacket();
    default_path_.peer_address = GetEffectivePeerAddressFromCurrentPacket();
    active_effective_peer_migration_type_ = type;

    OnConnectionMigration();
    return;
  }

  if (type == NO_CHANGE) {
    UpdatePeerAddress(last_received_packet_info_.source_address);
    QUIC_BUG(quic_bug_10511_36)
        << "EffectivePeerMigration started without address change.";
    return;
  }
  // There could be pending NEW_TOKEN_FRAME triggered by non-probing
  // PATH_RESPONSE_FRAME in the same packet or pending padding bytes in the
  // packet creator.
  packet_creator_.FlushCurrentPacket();
  packet_creator_.SendRemainingPendingPadding();
  if (!connected_) {
    return;
  }

  // Action items:
  //   1. Switch congestion controller;
  //   2. Update default_path_ (addresses, validation and bytes accounting);
  //   3. Save previous default path if needed;
  //   4. Kick off reverse path validation if needed.
  // Items 1 and 2 are must-to-do. Items 3 and 4 depends on if the new address
  // is validated or not and which path the incoming packet is on.

  const QuicSocketAddress current_effective_peer_address =
      GetEffectivePeerAddressFromCurrentPacket();
  QUIC_DLOG(INFO) << ENDPOINT << "Effective peer's ip:port changed from "
                  << default_path_.peer_address.ToString() << " to "
                  << current_effective_peer_address.ToString()
                  << ", address change type is " << type
                  << ", migrating connection.";

  const QuicSocketAddress previous_direct_peer_address = direct_peer_address_;
  PathState previous_default_path = std::move(default_path_);
  active_effective_peer_migration_type_ = type;
  MaybeClearQueuedPacketsOnPathChange();
  OnConnectionMigration();

  // Update congestion controller if the address change type is not PORT_CHANGE.
  if (type == PORT_CHANGE) {
    QUICHE_DCHECK(previous_default_path.validated ||
                  (alternative_path_.validated &&
                   alternative_path_.send_algorithm != nullptr));
    // No need to store previous congestion controller because either the new
    // default path is validated or the alternative path is validated and
    // already has associated congestion controller.
  } else {
    previous_default_path.rtt_stats.emplace();
    previous_default_path.rtt_stats->CloneFrom(
        *sent_packet_manager_.GetRttStats());
    // If the new peer address share the same IP with the alternative path, the
    // connection should switch to the congestion controller of the alternative
    // path. Otherwise, the connection should use a brand new one.
    // In order to re-use existing code in sent_packet_manager_, reset
    // congestion controller to initial state first and then change to the one
    // on alternative path.
    // TODO(danzh) combine these two steps into one after deprecating gQUIC.
    previous_default_path.send_algorithm = OnPeerIpAddressChanged();

    if (alternative_path_.peer_address.host() ==
            current_effective_peer_address.host() &&
        alternative_path_.send_algorithm != nullptr) {
      // Update the default path with the congestion controller of the
      // alternative path.
      sent_packet_manager_.SetSendAlgorithm(
          alternative_path_.send_algorithm.release());
      sent_packet_manager_.SetRttStats(
          std::move(alternative_path_.rtt_stats).value());
    }
  }
  // Update to the new peer address.
  UpdatePeerAddress(last_received_packet_info_.source_address);
  // Update the default path.
  if (IsAlternativePath(last_received_packet_info_.destination_address,
                        current_effective_peer_address)) {
    SetDefaultPathState(std::move(alternative_path_));
  } else {
    QuicConnectionId client_connection_id;
    absl::optional<StatelessResetToken> stateless_reset_token;
    FindMatchingOrNewClientConnectionIdOrToken(
        previous_default_path, alternative_path_,
        last_received_packet_info_.destination_connection_id,
        &client_connection_id, &stateless_reset_token);
    SetDefaultPathState(
        PathState(last_received_packet_info_.destination_address,
                  current_effective_peer_address, client_connection_id,
                  last_received_packet_info_.destination_connection_id,
                  stateless_reset_token));
    // The path is considered validated if its peer IP address matches any
    // validated path's peer IP address.
    default_path_.validated =
        (alternative_path_.peer_address.host() ==
             current_effective_peer_address.host() &&
         alternative_path_.validated) ||
        (previous_default_path.validated && type == PORT_CHANGE);
  }
  if (!last_received_packet_info_.received_bytes_counted) {
    // Increment bytes counting on the new default path.
    default_path_.bytes_received_before_address_validation +=
        last_received_packet_info_.length;
    last_received_packet_info_.received_bytes_counted = true;
  }

  if (!previous_default_path.validated) {
    // If the old address is under validation, cancel and fail it. Failing to
    // validate the old path shouldn't take any effect.
    QUIC_DVLOG(1) << "Cancel validation of previous peer address change to "
                  << previous_default_path.peer_address
                  << " upon peer migration to " << default_path_.peer_address;
    path_validator_.CancelPathValidation();
    ++stats_.num_peer_migration_while_validating_default_path;
  }

  // Clear alternative path if the new default path shares the same IP as the
  // alternative path.
  if (alternative_path_.peer_address.host() ==
      default_path_.peer_address.host()) {
    alternative_path_.Clear();
  }

  if (default_path_.validated) {
    QUIC_DVLOG(1) << "Peer migrated to a validated address.";
    // No need to save previous default path, validate new peer address or
    // update bytes sent/received.
    if (!(previous_default_path.validated && type == PORT_CHANGE)) {
      // The alternative path was validated because of proactive reverse path
      // validation.
      ++stats_.num_peer_migration_to_proactively_validated_address;
    }
    OnEffectivePeerMigrationValidated(
        default_path_.server_connection_id ==
        previous_default_path.server_connection_id);
    return;
  }

  // The new default address is not validated yet. Anti-amplification limit is
  // enforced.
  QUICHE_DCHECK(EnforceAntiAmplificationLimit());
  QUIC_DVLOG(1) << "Apply anti-amplification limit to effective peer address "
                << default_path_.peer_address << " with "
                << default_path_.bytes_sent_before_address_validation
                << " bytes sent and "
                << default_path_.bytes_received_before_address_validation
                << " bytes received.";

  QUICHE_DCHECK(!alternative_path_.peer_address.IsInitialized() ||
                alternative_path_.peer_address.host() !=
                    default_path_.peer_address.host());

  // Save previous default path to the altenative path.
  if (previous_default_path.validated) {
    // The old path is a validated path which the connection might revert back
    // to later. Store it as the alternative path.
    alternative_path_ = std::move(previous_default_path);
    QUICHE_DCHECK(alternative_path_.send_algorithm != nullptr);
  }

  // If the new address is not validated and the connection is not already
  // validating that address, a new reverse path validation is needed.
  if (!path_validator_.IsValidatingPeerAddress(
          current_effective_peer_address)) {
    ++stats_.num_reverse_path_validtion_upon_migration;
    ValidatePath(std::make_unique<ReversePathValidationContext>(
                     default_path_.self_address, peer_address(),
                     default_path_.peer_address, this),
                 std::make_unique<ReversePathValidationResultDelegate>(
                     this, previous_direct_peer_address),
                 PathValidationReason::kReversePathValidation);
  } else {
    QUIC_DVLOG(1) << "Peer address " << default_path_.peer_address
                  << " is already under validation, wait for result.";
    ++stats_.num_peer_migration_to_proactively_validated_address;
  }
}

void QuicConnection::OnConnectionMigration() {
  if (debug_visitor_ != nullptr) {
    const QuicTime now = clock_->ApproximateNow();
    if (now >= stats_.handshake_completion_time) {
      debug_visitor_->OnPeerAddressChange(
          active_effective_peer_migration_type_,
          now - stats_.handshake_completion_time);
    }
  }
  visitor_->OnConnectionMigration(active_effective_peer_migration_type_);
  if (active_effective_peer_migration_type_ != PORT_CHANGE &&
      active_effective_peer_migration_type_ != IPV4_SUBNET_CHANGE &&
      !framer_.version().HasIetfQuicFrames()) {
    sent_packet_manager_.OnConnectionMigration(/*reset_send_algorithm=*/false);
  }
}

bool QuicConnection::IsCurrentPacketConnectivityProbing() const {
  return is_current_packet_connectivity_probing_;
}

bool QuicConnection::ack_frame_updated() const {
  return uber_received_packet_manager_.IsAckFrameUpdated();
}

absl::string_view QuicConnection::GetCurrentPacket() {
  if (current_packet_data_ == nullptr) {
    return absl::string_view();
  }
  return absl::string_view(current_packet_data_,
                           last_received_packet_info_.length);
}

bool QuicConnection::MaybeConsiderAsMemoryCorruption(
    const QuicStreamFrame& frame) {
  if (QuicUtils::IsCryptoStreamId(transport_version(), frame.stream_id) ||
      last_received_packet_info_.decrypted_level != ENCRYPTION_INITIAL) {
    return false;
  }

  if (perspective_ == Perspective::IS_SERVER &&
      frame.data_length >= sizeof(kCHLO) &&
      strncmp(frame.data_buffer, reinterpret_cast<const char*>(&kCHLO),
              sizeof(kCHLO)) == 0) {
    return true;
  }

  if (perspective_ == Perspective::IS_CLIENT &&
      frame.data_length >= sizeof(kREJ) &&
      strncmp(frame.data_buffer, reinterpret_cast<const char*>(&kREJ),
              sizeof(kREJ)) == 0) {
    return true;
  }

  return false;
}

void QuicConnection::CheckIfApplicationLimited() {
  if (!connected_) {
    return;
  }

  bool application_limited =
      buffered_packets_.empty() && !visitor_->WillingAndAbleToWrite();

  if (!application_limited) {
    return;
  }

  sent_packet_manager_.OnApplicationLimited();
}

bool QuicConnection::UpdatePacketContent(QuicFrameType type) {
  last_received_packet_info_.frames.push_back(type);
  if (version().HasIetfQuicFrames()) {
    if (perspective_ == Perspective::IS_CLIENT) {
      return connected_;
    }
    if (!QuicUtils::IsProbingFrame(type)) {
      MaybeStartIetfPeerMigration();
      return connected_;
    }
    QuicSocketAddress current_effective_peer_address =
        GetEffectivePeerAddressFromCurrentPacket();
    if (IsDefaultPath(last_received_packet_info_.destination_address,
                      last_received_packet_info_.source_address)) {
      return connected_;
    }
    if (type == PATH_CHALLENGE_FRAME &&
        !IsAlternativePath(last_received_packet_info_.destination_address,
                           current_effective_peer_address)) {
      QUIC_DVLOG(1)
          << "The peer is probing a new path with effective peer address "
          << current_effective_peer_address << ",  self address "
          << last_received_packet_info_.destination_address;
      if (!default_path_.validated) {
        // Skip reverse path validation because either handshake hasn't
        // completed or the connection is validating the default path. Using
        // PATH_CHALLENGE to validate alternative client address before
        // handshake gets comfirmed is meaningless because anyone can respond to
        // it. If the connection is validating the default path, this
        // alternative path is currently the only validated path which shouldn't
        // be overridden.
        QUIC_DVLOG(1) << "The connection hasn't finished handshake or is "
                         "validating a recent peer address change.";
        QUIC_BUG_IF(quic_bug_12714_30,
                    IsHandshakeConfirmed() && !alternative_path_.validated)
            << "No validated peer address to send after handshake comfirmed.";
      } else if (!IsReceivedPeerAddressValidated()) {
        QuicConnectionId client_connection_id;
        absl::optional<StatelessResetToken> stateless_reset_token;
        FindMatchingOrNewClientConnectionIdOrToken(
            default_path_, alternative_path_,
            last_received_packet_info_.destination_connection_id,
            &client_connection_id, &stateless_reset_token);
        // Only override alternative path state upon receiving a PATH_CHALLENGE
        // from an unvalidated peer address, and the connection isn't validating
        // a recent peer migration.
        alternative_path_ =
            PathState(last_received_packet_info_.destination_address,
                      current_effective_peer_address, client_connection_id,
                      last_received_packet_info_.destination_connection_id,
                      stateless_reset_token);
        should_proactively_validate_peer_address_on_path_challenge_ = true;
      }
    }
    MaybeUpdateBytesReceivedFromAlternativeAddress(
        last_received_packet_info_.length);
    return connected_;
  }

  if (!ignore_gquic_probing_) {
    // Packet content is tracked to identify connectivity probe in non-IETF
    // version, where a connectivity probe is defined as
    // - a padded PING packet with peer address change received by server,
    // - a padded PING packet on new path received by client.

    if (current_packet_content_ == NOT_PADDED_PING) {
      // We have already learned the current packet is not a connectivity
      // probing packet. Peer migration should have already been started earlier
      // if needed.
      return connected_;
    }

    if (type == PING_FRAME) {
      if (current_packet_content_ == NO_FRAMES_RECEIVED) {
        current_packet_content_ = FIRST_FRAME_IS_PING;
        return connected_;
      }
    }

    // In Google QUIC, we look for a packet with just a PING and PADDING.
    // If the condition is met, mark things as connectivity-probing, causing
    // later processing to generate the correct response.
    if (type == PADDING_FRAME &&
        current_packet_content_ == FIRST_FRAME_IS_PING) {
      current_packet_content_ = SECOND_FRAME_IS_PADDING;
      QUIC_CODE_COUNT_N(gquic_padded_ping_received, 1, 2);
      if (perspective_ == Perspective::IS_SERVER) {
        is_current_packet_connectivity_probing_ =
            current_effective_peer_migration_type_ != NO_CHANGE;
        if (is_current_packet_connectivity_probing_) {
          QUIC_CODE_COUNT_N(gquic_padded_ping_received, 2, 2);
        }
        QUIC_DLOG_IF(INFO, is_current_packet_connectivity_probing_)
            << ENDPOINT
            << "Detected connectivity probing packet. "
               "current_effective_peer_migration_type_:"
            << current_effective_peer_migration_type_;
      } else {
        is_current_packet_connectivity_probing_ =
            (last_received_packet_info_.source_address != peer_address()) ||
            (last_received_packet_info_.destination_address !=
             default_path_.self_address);
        QUIC_DLOG_IF(INFO, is_current_packet_connectivity_probing_)
            << ENDPOINT
            << "Detected connectivity probing packet. "
               "last_packet_source_address:"
            << last_received_packet_info_.source_address
            << ", peer_address_:" << peer_address()
            << ", last_packet_destination_address:"
            << last_received_packet_info_.destination_address
            << ", default path self_address :" << default_path_.self_address;
      }
      return connected_;
    }

    current_packet_content_ = NOT_PADDED_PING;
  } else {
    QUIC_RELOADABLE_FLAG_COUNT(quic_ignore_gquic_probing);
    QUICHE_DCHECK_EQ(current_packet_content_, NO_FRAMES_RECEIVED);
  }

  if (GetLargestReceivedPacket().IsInitialized() &&
      last_received_packet_info_.header.packet_number ==
          GetLargestReceivedPacket()) {
    UpdatePeerAddress(last_received_packet_info_.source_address);
    if (current_effective_peer_migration_type_ != NO_CHANGE) {
      // Start effective peer migration immediately when the current packet is
      // confirmed not a connectivity probing packet.
      StartEffectivePeerMigration(current_effective_peer_migration_type_);
    }
  }
  current_effective_peer_migration_type_ = NO_CHANGE;
  return connected_;
}

void QuicConnection::MaybeStartIetfPeerMigration() {
  QUICHE_DCHECK(version().HasIetfQuicFrames());
  if (current_effective_peer_migration_type_ != NO_CHANGE &&
      !IsHandshakeConfirmed()) {
    QUIC_LOG_EVERY_N_SEC(INFO, 60)
        << ENDPOINT << "Effective peer's ip:port changed from "
        << default_path_.peer_address.ToString() << " to "
        << GetEffectivePeerAddressFromCurrentPacket().ToString()
        << " before handshake confirmed, "
           "current_effective_peer_migration_type_: "
        << current_effective_peer_migration_type_;
    // Peer migrated before handshake gets confirmed.
    CloseConnection((current_effective_peer_migration_type_ == PORT_CHANGE
                         ? QUIC_PEER_PORT_CHANGE_HANDSHAKE_UNCONFIRMED
                         : QUIC_CONNECTION_MIGRATION_HANDSHAKE_UNCONFIRMED),
                    "Peer address changed before handshake is confirmed.",
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  if (GetLargestReceivedPacket().IsInitialized() &&
      last_received_packet_info_.header.packet_number ==
          GetLargestReceivedPacket()) {
    if (current_effective_peer_migration_type_ != NO_CHANGE) {
      // Start effective peer migration when the current packet contains a
      // non-probing frame.
      // TODO(fayang): When multiple packet number spaces is supported, only
      // start peer migration for the application data.
      StartEffectivePeerMigration(current_effective_peer_migration_type_);
    } else {
      UpdatePeerAddress(last_received_packet_info_.source_address);
    }
  }
  current_effective_peer_migration_type_ = NO_CHANGE;
}

void QuicConnection::PostProcessAfterAckFrame(bool acked_new_packet) {
  if (!packet_creator_.has_ack()) {
    uber_received_packet_manager_.DontWaitForPacketsBefore(
        last_received_packet_info_.decrypted_level,
        SupportsMultiplePacketNumberSpaces()
            ? sent_packet_manager_.GetLargestPacketPeerKnowsIsAcked(
                  last_received_packet_info_.decrypted_level)
            : sent_packet_manager_.largest_packet_peer_knows_is_acked());
  }
  // Always reset the retransmission alarm when an ack comes in, since we now
  // have a better estimate of the current rtt than when it was set.
  SetRetransmissionAlarm();
  if (acked_new_packet) {
    OnForwardProgressMade();
  } else if (default_enable_5rto_blackhole_detection_ &&
             !sent_packet_manager_.HasInFlightPackets() &&
             blackhole_detector_.IsDetectionInProgress()) {
    // In case no new packets get acknowledged, it is possible packets are
    // detected lost because of time based loss detection. Cancel blackhole
    // detection if there is no packets in flight.
    blackhole_detector_.StopDetection(/*permanent=*/false);
  }
}

void QuicConnection::SetSessionNotifier(
    SessionNotifierInterface* session_notifier) {
  sent_packet_manager_.SetSessionNotifier(session_notifier);
}

void QuicConnection::SetDataProducer(
    QuicStreamFrameDataProducer* data_producer) {
  framer_.set_data_producer(data_producer);
}

void QuicConnection::SetTransmissionType(TransmissionType type) {
  packet_creator_.SetTransmissionType(type);
}

void QuicConnection::UpdateReleaseTimeIntoFuture() {
  QUICHE_DCHECK(supports_release_time_);

  const QuicTime::Delta prior_max_release_time = release_time_into_future_;
  release_time_into_future_ = std::max(
      QuicTime::Delta::FromMilliseconds(kMinReleaseTimeIntoFutureMs),
      std::min(QuicTime::Delta::FromMilliseconds(
                   GetQuicFlag(quic_max_pace_time_into_future_ms)),
               sent_packet_manager_.GetRttStats()->SmoothedOrInitialRtt() *
                   GetQuicFlag(quic_pace_time_into_future_srtt_fraction)));
  QUIC_DVLOG(3) << "Updated max release time delay from "
                << prior_max_release_time << " to "
                << release_time_into_future_;
}

void QuicConnection::ResetAckStates() {
  ack_alarm_->Cancel();
  uber_received_packet_manager_.ResetAckStates(encryption_level_);
}

MessageStatus QuicConnection::SendMessage(
    QuicMessageId message_id, absl::Span<quiche::QuicheMemSlice> message,
    bool flush) {
  if (MemSliceSpanTotalSize(message) > GetCurrentLargestMessagePayload()) {
    return MESSAGE_STATUS_TOO_LARGE;
  }
  if (!connected_ || (!flush && !CanWrite(HAS_RETRANSMITTABLE_DATA))) {
    return MESSAGE_STATUS_BLOCKED;
  }
  ScopedPacketFlusher flusher(this);
  return packet_creator_.AddMessageFrame(message_id, message);
}

QuicPacketLength QuicConnection::GetCurrentLargestMessagePayload() const {
  return packet_creator_.GetCurrentLargestMessagePayload();
}

QuicPacketLength QuicConnection::GetGuaranteedLargestMessagePayload() const {
  return packet_creator_.GetGuaranteedLargestMessagePayload();
}

uint32_t QuicConnection::cipher_id() const {
  if (version().KnowsWhichDecrypterToUse()) {
    return framer_.GetDecrypter(last_received_packet_info_.decrypted_level)
        ->cipher_id();
  }
  return framer_.decrypter()->cipher_id();
}

EncryptionLevel QuicConnection::GetConnectionCloseEncryptionLevel() const {
  if (perspective_ == Perspective::IS_CLIENT) {
    return encryption_level_;
  }
  if (IsHandshakeComplete()) {
    // A forward secure packet has been received.
    QUIC_BUG_IF(quic_bug_12714_31,
                encryption_level_ != ENCRYPTION_FORWARD_SECURE)
        << ENDPOINT << "Unexpected connection close encryption level "
        << encryption_level_;
    return ENCRYPTION_FORWARD_SECURE;
  }
  if (framer_.HasEncrypterOfEncryptionLevel(ENCRYPTION_ZERO_RTT)) {
    if (encryption_level_ != ENCRYPTION_ZERO_RTT) {
      QUIC_CODE_COUNT(quic_wrong_encryption_level_connection_close_ietf);
    }
    return ENCRYPTION_ZERO_RTT;
  }
  return ENCRYPTION_INITIAL;
}

void QuicConnection::MaybeBundleCryptoDataWithAcks() {
  QUICHE_DCHECK(SupportsMultiplePacketNumberSpaces());
  if (IsHandshakeConfirmed()) {
    return;
  }
  PacketNumberSpace space = HANDSHAKE_DATA;
  if (perspective() == Perspective::IS_SERVER &&
      framer_.HasEncrypterOfEncryptionLevel(ENCRYPTION_INITIAL)) {
    // On the server side, sends INITIAL data with INITIAL ACK if initial key is
    // available.
    space = INITIAL_DATA;
  }
  const QuicTime ack_timeout =
      uber_received_packet_manager_.GetAckTimeout(space);
  if (!ack_timeout.IsInitialized() ||
      (ack_timeout > clock_->ApproximateNow() &&
       ack_timeout > uber_received_packet_manager_.GetEarliestAckTimeout())) {
    // No pending ACK of space.
    return;
  }
  if (coalesced_packet_.length() > 0) {
    // Do not bundle CRYPTO data if the ACK could be coalesced with other
    // packets.
    return;
  }

  if (!framer_.HasAnEncrypterForSpace(space)) {
    QUIC_BUG(quic_bug_10511_39)
        << ENDPOINT
        << "Try to bundle crypto with ACK with missing key of space "
        << PacketNumberSpaceToString(space);
    return;
  }

  sent_packet_manager_.RetransmitDataOfSpaceIfAny(space);
}

void QuicConnection::SendAllPendingAcks() {
  QUICHE_DCHECK(SupportsMultiplePacketNumberSpaces());
  QUIC_DVLOG(1) << ENDPOINT << "Trying to send all pending ACKs";
  ack_alarm_->Cancel();
  QuicTime earliest_ack_timeout =
      uber_received_packet_manager_.GetEarliestAckTimeout();
  QUIC_BUG_IF(quic_bug_12714_32, !earliest_ack_timeout.IsInitialized());
  MaybeBundleCryptoDataWithAcks();
  if (GetQuicRestartFlag(quic_opport_bundle_qpack_decoder_data)) {
    QUIC_RESTART_FLAG_COUNT_N(quic_opport_bundle_qpack_decoder_data, 2, 3);
    visitor_->MaybeBundleOpportunistically();
  }
  earliest_ack_timeout = uber_received_packet_manager_.GetEarliestAckTimeout();
  if (!earliest_ack_timeout.IsInitialized()) {
    return;
  }
  for (int8_t i = INITIAL_DATA; i <= APPLICATION_DATA; ++i) {
    const QuicTime ack_timeout = uber_received_packet_manager_.GetAckTimeout(
        static_cast<PacketNumberSpace>(i));
    if (!ack_timeout.IsInitialized()) {
      continue;
    }
    if (!framer_.HasAnEncrypterForSpace(static_cast<PacketNumberSpace>(i))) {
      // The key has been dropped.
      continue;
    }
    if (ack_timeout > clock_->ApproximateNow() &&
        ack_timeout > earliest_ack_timeout) {
      // Always send the earliest ACK to make forward progress in case alarm
      // fires early.
      continue;
    }
    QUIC_DVLOG(1) << ENDPOINT << "Sending ACK of packet number space "
                  << PacketNumberSpaceToString(
                         static_cast<PacketNumberSpace>(i));
    ScopedEncryptionLevelContext context(
        this, QuicUtils::GetEncryptionLevelToSendAckofSpace(
                  static_cast<PacketNumberSpace>(i)));
    QuicFrames frames;
    frames.push_back(uber_received_packet_manager_.GetUpdatedAckFrame(
        static_cast<PacketNumberSpace>(i), clock_->ApproximateNow()));
    const bool flushed = packet_creator_.FlushAckFrame(frames);
    // Consider reset ack states even when flush is not successful.
    if (!flushed) {
      // Connection is write blocked.
      QUIC_BUG_IF(quic_bug_12714_33,
                  !writer_->IsWriteBlocked() &&
                      !LimitedByAmplificationFactor(
                          packet_creator_.max_packet_length()) &&
                      !IsMissingDestinationConnectionID())
          << "Writer not blocked and not throttled by amplification factor, "
             "but ACK not flushed for packet space:"
          << PacketNumberSpaceToString(static_cast<PacketNumberSpace>(i))
          << ", connected: " << connected_
          << ", fill_coalesced_packet: " << fill_coalesced_packet_
          << ", blocked_by_no_connection_id: "
          << (peer_issued_cid_manager_ != nullptr &&
              packet_creator_.GetDestinationConnectionId().IsEmpty())
          << ", has_soft_max_packet_length: "
          << packet_creator_.HasSoftMaxPacketLength()
          << ", max_packet_length: " << packet_creator_.max_packet_length()
          << ", pending frames: " << packet_creator_.GetPendingFramesInfo();
      break;
    }
    ResetAckStates();
  }

  const QuicTime timeout =
      uber_received_packet_manager_.GetEarliestAckTimeout();
  if (timeout.IsInitialized()) {
    // If there are ACKs pending, re-arm ack alarm.
    ack_alarm_->Update(timeout, kAlarmGranularity);
  }
  // Only try to bundle retransmittable data with ACK frame if default
  // encryption level is forward secure.
  if (encryption_level_ != ENCRYPTION_FORWARD_SECURE ||
      !ShouldBundleRetransmittableFrameWithAck()) {
    return;
  }
  consecutive_num_packets_with_no_retransmittable_frames_ = 0;
  if (packet_creator_.HasPendingRetransmittableFrames() ||
      visitor_->WillingAndAbleToWrite()) {
    // There are pending retransmittable frames.
    return;
  }

  visitor_->OnAckNeedsRetransmittableFrame();
}

bool QuicConnection::ShouldBundleRetransmittableFrameWithAck() const {
  if (consecutive_num_packets_with_no_retransmittable_frames_ >=
      max_consecutive_num_packets_with_no_retransmittable_frames_) {
    return true;
  }
  if (bundle_retransmittable_with_pto_ack_ &&
      sent_packet_manager_.GetConsecutivePtoCount() > 0) {
    // Bundle a retransmittable frame with an ACK if PTO has fired in order to
    // recover more quickly in cases of temporary network outage.
    return true;
  }
  return false;
}

void QuicConnection::MaybeCoalescePacketOfHigherSpace() {
  if (!connected() || !packet_creator_.HasSoftMaxPacketLength()) {
    return;
  }
  if (fill_coalesced_packet_) {
    // Make sure MaybeCoalescePacketOfHigherSpace is not re-entrant.
    QUIC_BUG(quic_coalesce_packet_reentrant);
    return;
  }
  for (EncryptionLevel retransmission_level :
       {ENCRYPTION_INITIAL, ENCRYPTION_HANDSHAKE}) {
    // Coalesce HANDSHAKE with INITIAL retransmission, and coalesce 1-RTT with
    // HANDSHAKE retransmission.
    const EncryptionLevel coalesced_level =
        retransmission_level == ENCRYPTION_INITIAL ? ENCRYPTION_HANDSHAKE
                                                   : ENCRYPTION_FORWARD_SECURE;
    if (coalesced_packet_.ContainsPacketOfEncryptionLevel(
            retransmission_level) &&
        coalesced_packet_.TransmissionTypeOfPacket(retransmission_level) !=
            NOT_RETRANSMISSION &&
        framer_.HasEncrypterOfEncryptionLevel(coalesced_level) &&
        !coalesced_packet_.ContainsPacketOfEncryptionLevel(coalesced_level)) {
      QUIC_DVLOG(1) << ENDPOINT
                    << "Trying to coalesce packet of encryption level: "
                    << EncryptionLevelToString(coalesced_level);
      fill_coalesced_packet_ = true;
      sent_packet_manager_.RetransmitDataOfSpaceIfAny(
          QuicUtils::GetPacketNumberSpace(coalesced_level));
      fill_coalesced_packet_ = false;
    }
  }
}

bool QuicConnection::FlushCoalescedPacket() {
  ScopedCoalescedPacketClearer clearer(&coalesced_packet_);
  if (!connected_) {
    return false;
  }
  if (!version().CanSendCoalescedPackets()) {
    QUIC_BUG_IF(quic_bug_12714_34, coalesced_packet_.length() > 0);
    return true;
  }
  if (coalesced_packet_.ContainsPacketOfEncryptionLevel(ENCRYPTION_INITIAL) &&
      !framer_.HasEncrypterOfEncryptionLevel(ENCRYPTION_INITIAL)) {
    // Initial packet will be re-serialized. Neuter it in case initial key has
    // been dropped.
    QUIC_BUG(quic_bug_10511_40)
        << ENDPOINT
        << "Coalescer contains initial packet while initial key has "
           "been dropped.";
    coalesced_packet_.NeuterInitialPacket();
  }
  if (coalesced_packet_.length() == 0) {
    return true;
  }

  char buffer[kMaxOutgoingPacketSize];
  const size_t length = packet_creator_.SerializeCoalescedPacket(
      coalesced_packet_, buffer, coalesced_packet_.max_packet_length());
  if (length == 0) {
    if (connected_) {
      CloseConnection(QUIC_FAILED_TO_SERIALIZE_PACKET,
                      "Failed to serialize coalesced packet.",
                      ConnectionCloseBehavior::SILENT_CLOSE);
    }
    return false;
  }
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnCoalescedPacketSent(coalesced_packet_, length);
  }
  QUIC_DVLOG(1) << ENDPOINT << "Sending coalesced packet "
                << coalesced_packet_.ToString(length);
  const size_t padding_size =
      length - std::min<size_t>(length, coalesced_packet_.length());
  // Buffer coalesced packet if padding + bytes_sent exceeds amplifcation limit.
  if (!buffered_packets_.empty() || HandleWriteBlocked() ||
      (enforce_strict_amplification_factor_ &&
       LimitedByAmplificationFactor(padding_size))) {
    QUIC_DVLOG(1) << ENDPOINT
                  << "Buffering coalesced packet of len: " << length;
    buffered_packets_.emplace_back(
        buffer, static_cast<QuicPacketLength>(length),
        coalesced_packet_.self_address(), coalesced_packet_.peer_address(),
        coalesced_packet_.ecn_codepoint());
  } else {
    WriteResult result = SendPacketToWriter(
        buffer, length, coalesced_packet_.self_address().host(),
        coalesced_packet_.peer_address(), writer_,
        coalesced_packet_.ecn_codepoint());
    if (IsWriteError(result.status)) {
      OnWriteError(result.error_code);
      return false;
    }
    if (IsWriteBlockedStatus(result.status)) {
      visitor_->OnWriteBlocked();
      if (result.status != WRITE_STATUS_BLOCKED_DATA_BUFFERED) {
        QUIC_DVLOG(1) << ENDPOINT
                      << "Buffering coalesced packet of len: " << length;
        buffered_packets_.emplace_back(
            buffer, static_cast<QuicPacketLength>(length),
            coalesced_packet_.self_address(), coalesced_packet_.peer_address(),
            coalesced_packet_.ecn_codepoint());
      }
    }
  }
  if (accelerated_server_preferred_address_ &&
      stats_.num_duplicated_packets_sent_to_server_preferred_address <
          kMaxDuplicatedPacketsSentToServerPreferredAddress) {
    // Send coalesced packets to both addresses while the server preferred
    // address validation is pending.
    QUICHE_DCHECK(received_server_preferred_address_.IsInitialized());
    path_validator_.MaybeWritePacketToAddress(
        buffer, length, received_server_preferred_address_);
    ++stats_.num_duplicated_packets_sent_to_server_preferred_address;
  }
  // Account for added padding.
  if (length > coalesced_packet_.length()) {
    if (IsDefaultPath(coalesced_packet_.self_address(),
                      coalesced_packet_.peer_address())) {
      if (EnforceAntiAmplificationLimit()) {
        // Include bytes sent even if they are not in flight.
        default_path_.bytes_sent_before_address_validation += padding_size;
      }
    } else {
      MaybeUpdateBytesSentToAlternativeAddress(coalesced_packet_.peer_address(),
                                               padding_size);
    }
    stats_.bytes_sent += padding_size;
    if (coalesced_packet_.initial_packet() != nullptr &&
        coalesced_packet_.initial_packet()->transmission_type !=
            NOT_RETRANSMISSION) {
      stats_.bytes_retransmitted += padding_size;
    }
  }
  return true;
}

void QuicConnection::MaybeEnableMultiplePacketNumberSpacesSupport() {
  if (version().handshake_protocol != PROTOCOL_TLS1_3) {
    return;
  }
  QUIC_DVLOG(1) << ENDPOINT << "connection " << connection_id()
                << " supports multiple packet number spaces";
  framer_.EnableMultiplePacketNumberSpacesSupport();
  sent_packet_manager_.EnableMultiplePacketNumberSpacesSupport();
  uber_received_packet_manager_.EnableMultiplePacketNumberSpacesSupport(
      perspective_);
}

bool QuicConnection::SupportsMultiplePacketNumberSpaces() const {
  return sent_packet_manager_.supports_multiple_packet_number_spaces();
}

void QuicConnection::SetLargestReceivedPacketWithAck(
    QuicPacketNumber new_value) {
  if (SupportsMultiplePacketNumberSpaces()) {
    largest_seen_packets_with_ack_[QuicUtils::GetPacketNumberSpace(
        last_received_packet_info_.decrypted_level)] = new_value;
  } else {
    largest_seen_packet_with_ack_ = new_value;
  }
}

void QuicConnection::OnForwardProgressMade() {
  if (!connected_) {
    return;
  }
  if (is_path_degrading_) {
    visitor_->OnForwardProgressMadeAfterPathDegrading();
    stats_.num_forward_progress_after_path_degrading++;
    is_path_degrading_ = false;
  }
  if (sent_packet_manager_.HasInFlightPackets()) {
    // Restart detections if forward progress has been made.
    blackhole_detector_.RestartDetection(GetPathDegradingDeadline(),
                                         GetNetworkBlackholeDeadline(),
                                         GetPathMtuReductionDeadline());
  } else {
    // Stop detections in quiecense.
    blackhole_detector_.StopDetection(/*permanent=*/false);
  }
  QUIC_BUG_IF(quic_bug_12714_35,
              perspective_ == Perspective::IS_SERVER &&
                  default_enable_5rto_blackhole_detection_ &&
                  blackhole_detector_.IsDetectionInProgress() &&
                  !sent_packet_manager_.HasInFlightPackets())
      << ENDPOINT
      << "Trying to start blackhole detection without no bytes in flight";
}

QuicPacketNumber QuicConnection::GetLargestReceivedPacketWithAck() const {
  if (SupportsMultiplePacketNumberSpaces()) {
    return largest_seen_packets_with_ack_[QuicUtils::GetPacketNumberSpace(
        last_received_packet_info_.decrypted_level)];
  }
  return largest_seen_packet_with_ack_;
}

QuicPacketNumber QuicConnection::GetLargestAckedPacket() const {
  if (SupportsMultiplePacketNumberSpaces()) {
    return sent_packet_manager_.GetLargestAckedPacket(
        last_received_packet_info_.decrypted_level);
  }
  return sent_packet_manager_.GetLargestObserved();
}

QuicPacketNumber QuicConnection::GetLargestReceivedPacket() const {
  return uber_received_packet_manager_.GetLargestObserved(
      last_received_packet_info_.decrypted_level);
}

bool QuicConnection::EnforceAntiAmplificationLimit() const {
  return version().SupportsAntiAmplificationLimit() &&
         perspective_ == Perspective::IS_SERVER && !default_path_.validated;
}

// TODO(danzh) Pass in path object or its reference of some sort to use this
// method to check anti-amplification limit on non-default path.
bool QuicConnection::LimitedByAmplificationFactor(QuicByteCount bytes) const {
  return EnforceAntiAmplificationLimit() &&
         (default_path_.bytes_sent_before_address_validation +
          (enforce_strict_amplification_factor_ ? bytes : 0)) >=
             anti_amplification_factor_ *
                 default_path_.bytes_received_before_address_validation;
}

SerializedPacketFate QuicConnection::GetSerializedPacketFate(
    bool is_mtu_discovery, EncryptionLevel encryption_level) {
  if (ShouldDiscardPacket(encryption_level)) {
    return DISCARD;
  }
  if (version().CanSendCoalescedPackets() && !coalescing_done_ &&
      !is_mtu_discovery) {
    if (!IsHandshakeConfirmed()) {
      // Before receiving ACK for any 1-RTT packets, always try to coalesce
      // packet (except MTU discovery packet).
      return COALESCE;
    }
    if (coalesced_packet_.length() > 0) {
      // If the coalescer is not empty, let this packet go through coalescer
      // to avoid potential out of order sending.
      return COALESCE;
    }
  }
  if (!buffered_packets_.empty() || HandleWriteBlocked()) {
    return BUFFER;
  }
  return SEND_TO_WRITER;
}

bool QuicConnection::IsHandshakeComplete() const {
  return visitor_->GetHandshakeState() >= HANDSHAKE_COMPLETE;
}

bool QuicConnection::IsHandshakeConfirmed() const {
  QUICHE_DCHECK_EQ(PROTOCOL_TLS1_3, version().handshake_protocol);
  return visitor_->GetHandshakeState() == HANDSHAKE_CONFIRMED;
}

size_t QuicConnection::min_received_before_ack_decimation() const {
  return uber_received_packet_manager_.min_received_before_ack_decimation();
}

void QuicConnection::set_min_received_before_ack_decimation(size_t new_value) {
  uber_received_packet_manager_.set_min_received_before_ack_decimation(
      new_value);
}

const QuicAckFrame& QuicConnection::ack_frame() const {
  if (SupportsMultiplePacketNumberSpaces()) {
    return uber_received_packet_manager_.GetAckFrame(
        QuicUtils::GetPacketNumberSpace(
            last_received_packet_info_.decrypted_level));
  }
  return uber_received_packet_manager_.ack_frame();
}

void QuicConnection::set_client_connection_id(
    QuicConnectionId client_connection_id) {
  if (!version().SupportsClientConnectionIds()) {
    QUIC_BUG_IF(quic_bug_12714_36, !client_connection_id.IsEmpty())
        << ENDPOINT << "Attempted to use client connection ID "
        << client_connection_id << " with unsupported version " << version();
    return;
  }
  default_path_.client_connection_id = client_connection_id;

  client_connection_id_is_set_ = true;
  if (version().HasIetfQuicFrames() && !client_connection_id.IsEmpty()) {
    if (perspective_ == Perspective::IS_SERVER) {
      QUICHE_DCHECK(peer_issued_cid_manager_ == nullptr);
      peer_issued_cid_manager_ =
          std::make_unique<QuicPeerIssuedConnectionIdManager>(
              kMinNumOfActiveConnectionIds, client_connection_id, clock_,
              alarm_factory_, this, context());
    } else {
      bool create_client_self_issued_cid_manager = true;
      quiche::AdjustTestValue(
          "quic::QuicConnection::create_cid_manager_when_set_client_cid",
          &create_client_self_issued_cid_manager);
      // Note in Chromium client, set_client_connection_id is not called and
      // thus self_issued_cid_manager_ should be null.
      if (create_client_self_issued_cid_manager) {
        self_issued_cid_manager_ = MakeSelfIssuedConnectionIdManager();
      }
    }
  }
  QUIC_DLOG(INFO) << ENDPOINT << "setting client connection ID to "
                  << default_path_.client_connection_id
                  << " for connection with server connection ID "
                  << default_path_.server_connection_id;
  packet_creator_.SetClientConnectionId(default_path_.client_connection_id);
  framer_.SetExpectedClientConnectionIdLength(
      default_path_.client_connection_id.length());
}

void QuicConnection::OnPathDegradingDetected() {
  is_path_degrading_ = true;
  visitor_->OnPathDegrading();
  stats_.num_path_degrading++;
  if (multi_port_stats_ && multi_port_migration_enabled_) {
    MaybeMigrateToMultiPortPath();
  }
}

void QuicConnection::MaybeMigrateToMultiPortPath() {
  if (!alternative_path_.validated) {
    QUIC_CLIENT_HISTOGRAM_ENUM(
        "QuicConnection.MultiPortPathStatusWhenMigrating",
        MultiPortStatusOnMigration::kNotValidated,
        MultiPortStatusOnMigration::kMaxValue,
        "Status of the multi port path upon migration");
    return;
  }
  std::unique_ptr<QuicPathValidationContext> context;
  const bool has_pending_validation =
      path_validator_.HasPendingPathValidation();
  if (!has_pending_validation) {
    // The multi-port path should have just finished the recent probe and
    // waiting for the next one.
    context = std::move(multi_port_path_context_);
    multi_port_probing_alarm_->Cancel();
    QUIC_CLIENT_HISTOGRAM_ENUM(
        "QuicConnection.MultiPortPathStatusWhenMigrating",
        MultiPortStatusOnMigration::kWaitingForRefreshValidation,
        MultiPortStatusOnMigration::kMaxValue,
        "Status of the multi port path upon migration");
  } else {
    // The multi-port path is currently under probing.
    context = path_validator_.ReleaseContext();
    QUIC_CLIENT_HISTOGRAM_ENUM(
        "QuicConnection.MultiPortPathStatusWhenMigrating",
        MultiPortStatusOnMigration::kPendingRefreshValidation,
        MultiPortStatusOnMigration::kMaxValue,
        "Status of the multi port path upon migration");
  }
  if (context == nullptr) {
    QUICHE_BUG(quic_bug_12714_90) << "No multi-port context to migrate to";
    return;
  }
  visitor_->MigrateToMultiPortPath(std::move(context));
}

void QuicConnection::OnBlackholeDetected() {
  if (default_enable_5rto_blackhole_detection_ &&
      !sent_packet_manager_.HasInFlightPackets()) {
    QUIC_BUG(quic_bug_10511_41)
        << ENDPOINT
        << "Blackhole detected, but there is no bytes in flight, version: "
        << version();
    // Do not close connection if there is no bytes in flight.
    return;
  }
  CloseConnection(QUIC_TOO_MANY_RTOS, "Network blackhole detected",
                  ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicConnection::OnPathMtuReductionDetected() {
  MaybeRevertToPreviousMtu();
}

void QuicConnection::OnHandshakeTimeout() {
  const QuicTime::Delta duration =
      clock_->ApproximateNow() - stats_.connection_creation_time;
  std::string error_details = absl::StrCat(
      "Handshake timeout expired after ", duration.ToDebuggingValue(),
      ". Timeout:",
      idle_network_detector_.handshake_timeout().ToDebuggingValue());
  if (perspective() == Perspective::IS_CLIENT && version().UsesTls()) {
    absl::StrAppend(&error_details, UndecryptablePacketsInfo());
  }
  QUIC_DVLOG(1) << ENDPOINT << error_details;
  CloseConnection(QUIC_HANDSHAKE_TIMEOUT, error_details,
                  ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicConnection::OnIdleNetworkDetected() {
  const QuicTime::Delta duration =
      clock_->ApproximateNow() -
      idle_network_detector_.last_network_activity_time();
  std::string error_details = absl::StrCat(
      "No recent network activity after ", duration.ToDebuggingValue(),
      ". Timeout:",
      idle_network_detector_.idle_network_timeout().ToDebuggingValue());
  if (perspective() == Perspective::IS_CLIENT && version().UsesTls() &&
      !IsHandshakeComplete()) {
    absl::StrAppend(&error_details, UndecryptablePacketsInfo());
  }
  QUIC_DVLOG(1) << ENDPOINT << error_details;
  const bool has_consecutive_pto =
      sent_packet_manager_.GetConsecutivePtoCount() > 0;
  if (has_consecutive_pto || visitor_->ShouldKeepConnectionAlive()) {
    if (GetQuicReloadableFlag(quic_add_stream_info_to_idle_close_detail) &&
        !has_consecutive_pto) {
      // Include stream information in error detail if there are open streams.
      QUIC_RELOADABLE_FLAG_COUNT(quic_add_stream_info_to_idle_close_detail);
      absl::StrAppend(&error_details, ", ",
                      visitor_->GetStreamsInfoForLogging());
    }
    CloseConnection(QUIC_NETWORK_IDLE_TIMEOUT, error_details,
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  QuicErrorCode error_code = QUIC_NETWORK_IDLE_TIMEOUT;
  if (idle_timeout_connection_close_behavior_ ==
      ConnectionCloseBehavior::
          SILENT_CLOSE_WITH_CONNECTION_CLOSE_PACKET_SERIALIZED) {
    error_code = QUIC_SILENT_IDLE_TIMEOUT;
  }
  CloseConnection(error_code, error_details,
                  idle_timeout_connection_close_behavior_);
}

void QuicConnection::OnKeepAliveTimeout() {
  if (retransmission_alarm_->IsSet() ||
      !visitor_->ShouldKeepConnectionAlive()) {
    return;
  }
  SendPingAtLevel(framer().GetEncryptionLevelToSendApplicationData());
}

void QuicConnection::OnRetransmittableOnWireTimeout() {
  if (retransmission_alarm_->IsSet() ||
      !visitor_->ShouldKeepConnectionAlive()) {
    return;
  }
  bool packet_buffered = false;
  switch (retransmittable_on_wire_behavior_) {
    case DEFAULT:
      break;
    case SEND_FIRST_FORWARD_SECURE_PACKET:
      if (first_serialized_one_rtt_packet_ != nullptr) {
        buffered_packets_.emplace_back(
            first_serialized_one_rtt_packet_->data.get(),
            first_serialized_one_rtt_packet_->length, self_address(),
            peer_address(), first_serialized_one_rtt_packet_->ecn_codepoint);
        packet_buffered = true;
      }
      break;
    case SEND_RANDOM_BYTES:
      const QuicPacketLength random_bytes_length = std::max<QuicPacketLength>(
          QuicFramer::GetMinStatelessResetPacketLength() + 1,
          random_generator_->RandUint64() %
              packet_creator_.max_packet_length());
      buffered_packets_.emplace_back(*random_generator_, random_bytes_length,
                                     self_address(), peer_address());
      packet_buffered = true;
      break;
  }
  if (packet_buffered) {
    if (!writer_->IsWriteBlocked()) {
      WriteQueuedPackets();
    }
    if (connected_) {
      // Always reset PING alarm with has_in_flight_packets=true. This is used
      // to avoid re-arming the alarm in retransmittable-on-wire mode.
      ping_manager_.SetAlarm(clock_->ApproximateNow(),
                             visitor_->ShouldKeepConnectionAlive(),
                             /*has_in_flight_packets=*/true);
    }
    return;
  }
  SendPingAtLevel(framer().GetEncryptionLevelToSendApplicationData());
}

void QuicConnection::OnPeerIssuedConnectionIdRetired() {
  QUICHE_DCHECK(peer_issued_cid_manager_ != nullptr);
  QuicConnectionId* default_path_cid =
      perspective_ == Perspective::IS_CLIENT
          ? &default_path_.server_connection_id
          : &default_path_.client_connection_id;
  QuicConnectionId* alternative_path_cid =
      perspective_ == Perspective::IS_CLIENT
          ? &alternative_path_.server_connection_id
          : &alternative_path_.client_connection_id;
  bool default_path_and_alternative_path_use_the_same_peer_connection_id =
      *default_path_cid == *alternative_path_cid;
  if (!default_path_cid->IsEmpty() &&
      !peer_issued_cid_manager_->IsConnectionIdActive(*default_path_cid)) {
    *default_path_cid = QuicConnectionId();
  }
  // TODO(haoyuewang) Handle the change for default_path_ & alternatvie_path_
  // via the same helper function.
  if (default_path_cid->IsEmpty()) {
    // Try setting a new connection ID now such that subsequent
    // RetireConnectionId frames can be sent on the default path.
    const QuicConnectionIdData* unused_connection_id_data =
        peer_issued_cid_manager_->ConsumeOneUnusedConnectionId();
    if (unused_connection_id_data != nullptr) {
      *default_path_cid = unused_connection_id_data->connection_id;
      default_path_.stateless_reset_token =
          unused_connection_id_data->stateless_reset_token;
      if (perspective_ == Perspective::IS_CLIENT) {
        packet_creator_.SetServerConnectionId(
            unused_connection_id_data->connection_id);
      } else {
        packet_creator_.SetClientConnectionId(
            unused_connection_id_data->connection_id);
      }
    }
  }
  if (default_path_and_alternative_path_use_the_same_peer_connection_id) {
    *alternative_path_cid = *default_path_cid;
    alternative_path_.stateless_reset_token =
        default_path_.stateless_reset_token;
  } else if (!alternative_path_cid->IsEmpty() &&
             !peer_issued_cid_manager_->IsConnectionIdActive(
                 *alternative_path_cid)) {
    *alternative_path_cid = EmptyQuicConnectionId();
    const QuicConnectionIdData* unused_connection_id_data =
        peer_issued_cid_manager_->ConsumeOneUnusedConnectionId();
    if (unused_connection_id_data != nullptr) {
      *alternative_path_cid = unused_connection_id_data->connection_id;
      alternative_path_.stateless_reset_token =
          unused_connection_id_data->stateless_reset_token;
    }
  }

  std::vector<uint64_t> retired_cid_sequence_numbers =
      peer_issued_cid_manager_->ConsumeToBeRetiredConnectionIdSequenceNumbers();
  QUICHE_DCHECK(!retired_cid_sequence_numbers.empty());
  for (const auto& sequence_number : retired_cid_sequence_numbers) {
    ++stats_.num_retire_connection_id_sent;
    visitor_->SendRetireConnectionId(sequence_number);
  }
}

bool QuicConnection::SendNewConnectionId(
    const QuicNewConnectionIdFrame& frame) {
  visitor_->SendNewConnectionId(frame);
  ++stats_.num_new_connection_id_sent;
  return connected_;
}

bool QuicConnection::MaybeReserveConnectionId(
    const QuicConnectionId& connection_id) {
  if (perspective_ == Perspective::IS_SERVER) {
    return visitor_->MaybeReserveConnectionId(connection_id);
  }
  return true;
}

void QuicConnection::OnSelfIssuedConnectionIdRetired(
    const QuicConnectionId& connection_id) {
  if (perspective_ == Perspective::IS_SERVER) {
    visitor_->OnServerConnectionIdRetired(connection_id);
  }
}

void QuicConnection::MaybeUpdateAckTimeout() {
  if (should_last_packet_instigate_acks_) {
    return;
  }
  should_last_packet_instigate_acks_ = true;
  uber_received_packet_manager_.MaybeUpdateAckTimeout(
      /*should_last_packet_instigate_acks=*/true,
      last_received_packet_info_.decrypted_level,
      last_received_packet_info_.header.packet_number,
      last_received_packet_info_.receipt_time, clock_->ApproximateNow(),
      sent_packet_manager_.GetRttStats());
}

QuicTime QuicConnection::GetPathDegradingDeadline() const {
  if (!ShouldDetectPathDegrading()) {
    return QuicTime::Zero();
  }
  return clock_->ApproximateNow() +
         sent_packet_manager_.GetPathDegradingDelay();
}

bool QuicConnection::ShouldDetectPathDegrading() const {
  if (!connected_) {
    return false;
  }
  if (GetQuicReloadableFlag(
          quic_no_path_degrading_before_handshake_confirmed) &&
      SupportsMultiplePacketNumberSpaces()) {
    QUIC_RELOADABLE_FLAG_COUNT_N(
        quic_no_path_degrading_before_handshake_confirmed, 1, 2);
    // No path degrading detection before handshake confirmed.
    return perspective_ == Perspective::IS_CLIENT && IsHandshakeConfirmed() &&
           !is_path_degrading_;
  }
  // No path degrading detection before handshake completes.
  if (!idle_network_detector_.handshake_timeout().IsInfinite()) {
    return false;
  }
  return perspective_ == Perspective::IS_CLIENT && !is_path_degrading_;
}

QuicTime QuicConnection::GetNetworkBlackholeDeadline() const {
  if (!ShouldDetectBlackhole()) {
    return QuicTime::Zero();
  }
  QUICHE_DCHECK_LT(0u, num_rtos_for_blackhole_detection_);

  const QuicTime::Delta blackhole_delay =
      sent_packet_manager_.GetNetworkBlackholeDelay(
          num_rtos_for_blackhole_detection_);
  if (!ShouldDetectPathDegrading()) {
    return clock_->ApproximateNow() + blackhole_delay;
  }
  return clock_->ApproximateNow() +
         CalculateNetworkBlackholeDelay(
             blackhole_delay, sent_packet_manager_.GetPathDegradingDelay(),
             sent_packet_manager_.GetPtoDelay());
}

// static
QuicTime::Delta QuicConnection::CalculateNetworkBlackholeDelay(
    QuicTime::Delta blackhole_delay, QuicTime::Delta path_degrading_delay,
    QuicTime::Delta pto_delay) {
  const QuicTime::Delta min_delay = path_degrading_delay + pto_delay * 2;
  if (blackhole_delay < min_delay) {
    QUIC_CODE_COUNT(quic_extending_short_blackhole_delay);
  }
  return std::max(min_delay, blackhole_delay);
}

void QuicConnection::AddKnownServerAddress(const QuicSocketAddress& address) {
  QUICHE_DCHECK(perspective_ == Perspective::IS_CLIENT);
  if (!address.IsInitialized() || IsKnownServerAddress(address)) {
    return;
  }
  known_server_addresses_.push_back(address);
}

absl::optional<QuicNewConnectionIdFrame>
QuicConnection::MaybeIssueNewConnectionIdForPreferredAddress() {
  if (self_issued_cid_manager_ == nullptr) {
    return absl::nullopt;
  }
  return self_issued_cid_manager_
      ->MaybeIssueNewConnectionIdForPreferredAddress();
}

bool QuicConnection::ShouldDetectBlackhole() const {
  if (!connected_ || blackhole_detection_disabled_) {
    return false;
  }
  if (GetQuicReloadableFlag(
          quic_no_path_degrading_before_handshake_confirmed) &&
      SupportsMultiplePacketNumberSpaces() && !IsHandshakeConfirmed()) {
    QUIC_RELOADABLE_FLAG_COUNT_N(
        quic_no_path_degrading_before_handshake_confirmed, 2, 2);
    return false;
  }
  // No blackhole detection before handshake completes.
  if (default_enable_5rto_blackhole_detection_) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_default_enable_5rto_blackhole_detection2,
                                 3, 3);
    return IsHandshakeComplete();
  }

  if (!idle_network_detector_.handshake_timeout().IsInfinite()) {
    return false;
  }
  return num_rtos_for_blackhole_detection_ > 0;
}

QuicTime QuicConnection::GetRetransmissionDeadline() const {
  if (perspective_ == Perspective::IS_CLIENT &&
      SupportsMultiplePacketNumberSpaces() && !IsHandshakeConfirmed() &&
      stats_.pto_count == 0 &&
      !framer_.HasDecrypterOfEncryptionLevel(ENCRYPTION_HANDSHAKE) &&
      !undecryptable_packets_.empty()) {
    // Retransmits ClientHello quickly when a Handshake or 1-RTT packet is
    // received prior to having Handshake keys. Adding kAlarmGranulary will
    // avoid spurious retransmissions in the case of small-scale reordering.
    return clock_->ApproximateNow() + kAlarmGranularity;
  }
  return sent_packet_manager_.GetRetransmissionTime();
}

bool QuicConnection::SendPathChallenge(
    const QuicPathFrameBuffer& data_buffer,
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address,
    const QuicSocketAddress& effective_peer_address, QuicPacketWriter* writer) {
  if (!framer_.HasEncrypterOfEncryptionLevel(ENCRYPTION_FORWARD_SECURE)) {
    return connected_;
  }

  QuicConnectionId client_cid, server_cid;
  FindOnPathConnectionIds(self_address, effective_peer_address, &client_cid,
                          &server_cid);
  if (writer == writer_) {
    ScopedPacketFlusher flusher(this);
    {
      QuicPacketCreator::ScopedPeerAddressContext context(
          &packet_creator_, peer_address, client_cid, server_cid);
      // It's using the default writer, add the PATH_CHALLENGE the same way as
      // other frames. This may cause connection to be closed.
      packet_creator_.AddPathChallengeFrame(data_buffer);
    }
  } else if (!writer->IsWriteBlocked()) {
    // Switch to the right CID and source/peer addresses.
    QuicPacketCreator::ScopedPeerAddressContext context(
        &packet_creator_, peer_address, client_cid, server_cid);
    std::unique_ptr<SerializedPacket> probing_packet =
        packet_creator_.SerializePathChallengeConnectivityProbingPacket(
            data_buffer);
    QUICHE_DCHECK_EQ(IsRetransmittable(*probing_packet),
                     NO_RETRANSMITTABLE_DATA)
        << ENDPOINT << "Probing Packet contains retransmittable frames";
    QUICHE_DCHECK_EQ(self_address, alternative_path_.self_address)
        << ENDPOINT
        << "Send PATH_CHALLENGE from self_address: " << self_address.ToString()
        << " which is different from alt_path self address: "
        << alternative_path_.self_address.ToString();
    WritePacketUsingWriter(std::move(probing_packet), writer, self_address,
                           peer_address, /*measure_rtt=*/false);
  } else {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Writer blocked when sending PATH_CHALLENGE.";
  }
  return connected_;
}

QuicTime QuicConnection::GetRetryTimeout(
    const QuicSocketAddress& peer_address_to_use,
    QuicPacketWriter* writer_to_use) const {
  if (writer_to_use == writer_ && peer_address_to_use == peer_address()) {
    return clock_->ApproximateNow() + sent_packet_manager_.GetPtoDelay();
  }
  return clock_->ApproximateNow() +
         QuicTime::Delta::FromMilliseconds(3 * kInitialRttMs);
}

void QuicConnection::ValidatePath(
    std::unique_ptr<QuicPathValidationContext> context,
    std::unique_ptr<QuicPathValidator::ResultDelegate> result_delegate,
    PathValidationReason reason) {
  QUICHE_DCHECK(version().HasIetfQuicFrames());
  if (path_validator_.HasPendingPathValidation()) {
    if (perspective_ == Perspective::IS_CLIENT &&
        IsValidatingServerPreferredAddress()) {
      QUIC_CLIENT_HISTOGRAM_BOOL(
          "QuicSession.ServerPreferredAddressValidationCancelled", true,
          "How often the caller kicked off another validation while there is "
          "an on-going server preferred address validation.");
    }
    // Cancel and fail any earlier validation.
    path_validator_.CancelPathValidation();
  }
  if (perspective_ == Perspective::IS_CLIENT &&
      !IsDefaultPath(context->self_address(), context->peer_address())) {
    if (self_issued_cid_manager_ != nullptr) {
      self_issued_cid_manager_->MaybeSendNewConnectionIds();
      if (!connected_) {
        return;
      }
    }
    if ((self_issued_cid_manager_ != nullptr &&
         !self_issued_cid_manager_->HasConnectionIdToConsume()) ||
        (peer_issued_cid_manager_ != nullptr &&
         !peer_issued_cid_manager_->HasUnusedConnectionId())) {
      QUIC_DVLOG(1) << "Client cannot start new path validation as there is no "
                       "requried connection ID is available.";
      result_delegate->OnPathValidationFailure(std::move(context));
      return;
    }
    QuicConnectionId client_connection_id, server_connection_id;
    absl::optional<StatelessResetToken> stateless_reset_token;
    if (self_issued_cid_manager_ != nullptr) {
      client_connection_id =
          *self_issued_cid_manager_->ConsumeOneConnectionId();
    }
    if (peer_issued_cid_manager_ != nullptr) {
      const auto* connection_id_data =
          peer_issued_cid_manager_->ConsumeOneUnusedConnectionId();
      server_connection_id = connection_id_data->connection_id;
      stateless_reset_token = connection_id_data->stateless_reset_token;
    }
    alternative_path_ = PathState(context->self_address(),
                                  context->peer_address(), client_connection_id,
                                  server_connection_id, stateless_reset_token);
  }
  path_validator_.StartPathValidation(std::move(context),
                                      std::move(result_delegate), reason);
  if (perspective_ == Perspective::IS_CLIENT &&
      IsValidatingServerPreferredAddress()) {
    AddKnownServerAddress(received_server_preferred_address_);
  }
}

bool QuicConnection::SendPathResponse(
    const QuicPathFrameBuffer& data_buffer,
    const QuicSocketAddress& peer_address_to_send,
    const QuicSocketAddress& effective_peer_address) {
  if (!framer_.HasEncrypterOfEncryptionLevel(ENCRYPTION_FORWARD_SECURE)) {
    return false;
  }
  QuicConnectionId client_cid, server_cid;
  FindOnPathConnectionIds(last_received_packet_info_.destination_address,
                          effective_peer_address, &client_cid, &server_cid);
  // Send PATH_RESPONSE using the provided peer address. If the creator has been
  // using a different peer address, it will flush before and after serializing
  // the current PATH_RESPONSE.
  QuicPacketCreator::ScopedPeerAddressContext context(
      &packet_creator_, peer_address_to_send, client_cid, server_cid);
  QUIC_DVLOG(1) << ENDPOINT << "Send PATH_RESPONSE to " << peer_address_to_send;
  if (default_path_.self_address ==
      last_received_packet_info_.destination_address) {
    // The PATH_CHALLENGE is received on the default socket. Respond on the same
    // socket.
    return packet_creator_.AddPathResponseFrame(data_buffer);
  }

  QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, perspective_);
  // This PATH_CHALLENGE is received on an alternative socket which should be
  // used to send PATH_RESPONSE.
  if (!path_validator_.HasPendingPathValidation() ||
      path_validator_.GetContext()->self_address() !=
          last_received_packet_info_.destination_address) {
    // Ignore this PATH_CHALLENGE if it's received from an uninteresting
    // socket.
    return true;
  }
  QuicPacketWriter* writer = path_validator_.GetContext()->WriterToUse();
  if (writer->IsWriteBlocked()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Writer blocked when sending PATH_RESPONSE.";
    return true;
  }

  std::unique_ptr<SerializedPacket> probing_packet =
      packet_creator_.SerializePathResponseConnectivityProbingPacket(
          {data_buffer}, /*is_padded=*/true);
  QUICHE_DCHECK_EQ(IsRetransmittable(*probing_packet), NO_RETRANSMITTABLE_DATA);
  QUIC_DVLOG(1) << ENDPOINT
                << "Send PATH_RESPONSE from alternative socket with address "
                << last_received_packet_info_.destination_address;
  // Ignore the return value to treat write error on the alternative writer as
  // part of network error. If the writer becomes blocked, wait for the peer to
  // send another PATH_CHALLENGE.
  WritePacketUsingWriter(std::move(probing_packet), writer,
                         last_received_packet_info_.destination_address,
                         peer_address_to_send,
                         /*measure_rtt=*/false);
  return true;
}

void QuicConnection::UpdatePeerAddress(QuicSocketAddress peer_address) {
  direct_peer_address_ = peer_address;
  packet_creator_.SetDefaultPeerAddress(peer_address);
}

void QuicConnection::SendPingAtLevel(EncryptionLevel level) {
  ScopedEncryptionLevelContext context(this, level);
  SendControlFrame(QuicFrame(QuicPingFrame()));
}

bool QuicConnection::HasPendingPathValidation() const {
  return path_validator_.HasPendingPathValidation();
}

QuicPathValidationContext* QuicConnection::GetPathValidationContext() const {
  return path_validator_.GetContext();
}

void QuicConnection::CancelPathValidation() {
  path_validator_.CancelPathValidation();
}

bool QuicConnection::UpdateConnectionIdsOnMigration(
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address) {
  QUICHE_DCHECK(perspective_ == Perspective::IS_CLIENT);
  if (IsAlternativePath(self_address, peer_address)) {
    // Client migration is after path validation.
    default_path_.client_connection_id = alternative_path_.client_connection_id;
    default_path_.server_connection_id = alternative_path_.server_connection_id;
    default_path_.stateless_reset_token =
        alternative_path_.stateless_reset_token;
    return true;
  }
  // Client migration is without path validation.
  if (self_issued_cid_manager_ != nullptr) {
    self_issued_cid_manager_->MaybeSendNewConnectionIds();
    if (!connected_) {
      return false;
    }
  }
  if ((self_issued_cid_manager_ != nullptr &&
       !self_issued_cid_manager_->HasConnectionIdToConsume()) ||
      (peer_issued_cid_manager_ != nullptr &&
       !peer_issued_cid_manager_->HasUnusedConnectionId())) {
    return false;
  }
  if (self_issued_cid_manager_ != nullptr) {
    default_path_.client_connection_id =
        *self_issued_cid_manager_->ConsumeOneConnectionId();
  }
  if (peer_issued_cid_manager_ != nullptr) {
    const auto* connection_id_data =
        peer_issued_cid_manager_->ConsumeOneUnusedConnectionId();
    default_path_.server_connection_id = connection_id_data->connection_id;
    default_path_.stateless_reset_token =
        connection_id_data->stateless_reset_token;
  }
  return true;
}

void QuicConnection::RetirePeerIssuedConnectionIdsNoLongerOnPath() {
  if (!version().HasIetfQuicFrames() || peer_issued_cid_manager_ == nullptr) {
    return;
  }
  if (perspective_ == Perspective::IS_CLIENT) {
    peer_issued_cid_manager_->MaybeRetireUnusedConnectionIds(
        {default_path_.server_connection_id,
         alternative_path_.server_connection_id});
  } else {
    peer_issued_cid_manager_->MaybeRetireUnusedConnectionIds(
        {default_path_.client_connection_id,
         alternative_path_.client_connection_id});
  }
}

bool QuicConnection::MigratePath(const QuicSocketAddress& self_address,
                                 const QuicSocketAddress& peer_address,
                                 QuicPacketWriter* writer, bool owns_writer) {
  QUICHE_DCHECK(perspective_ == Perspective::IS_CLIENT);
  if (!connected_) {
    if (owns_writer) {
      delete writer;
    }
    return false;
  }
  QUICHE_DCHECK(!version().UsesHttp3() || IsHandshakeConfirmed() ||
                accelerated_server_preferred_address_);

  if (version().UsesHttp3()) {
    if (!UpdateConnectionIdsOnMigration(self_address, peer_address)) {
      if (owns_writer) {
        delete writer;
      }
      return false;
    }
    if (packet_creator_.GetServerConnectionId().length() !=
        default_path_.server_connection_id.length()) {
      packet_creator_.FlushCurrentPacket();
    }
    packet_creator_.SetClientConnectionId(default_path_.client_connection_id);
    packet_creator_.SetServerConnectionId(default_path_.server_connection_id);
  }

  const auto self_address_change_type = QuicUtils::DetermineAddressChangeType(
      default_path_.self_address, self_address);
  const auto peer_address_change_type = QuicUtils::DetermineAddressChangeType(
      default_path_.peer_address, peer_address);
  QUICHE_DCHECK(self_address_change_type != NO_CHANGE ||
                peer_address_change_type != NO_CHANGE);
  const bool is_port_change = (self_address_change_type == PORT_CHANGE ||
                               self_address_change_type == NO_CHANGE) &&
                              (peer_address_change_type == PORT_CHANGE ||
                               peer_address_change_type == NO_CHANGE);
  SetSelfAddress(self_address);
  UpdatePeerAddress(peer_address);
  default_path_.peer_address = peer_address;
  if (writer_ != writer) {
    SetQuicPacketWriter(writer, owns_writer);
  }
  MaybeClearQueuedPacketsOnPathChange();
  OnSuccessfulMigration(is_port_change);
  return true;
}

void QuicConnection::OnPathValidationFailureAtClient(
    bool is_multi_port, const QuicPathValidationContext& context) {
  QUICHE_DCHECK(perspective_ == Perspective::IS_CLIENT &&
                version().HasIetfQuicFrames());
  alternative_path_.Clear();

  if (is_multi_port && multi_port_stats_ != nullptr) {
    if (is_path_degrading_) {
      multi_port_stats_->num_multi_port_probe_failures_when_path_degrading++;
    } else {
      multi_port_stats_
          ->num_multi_port_probe_failures_when_path_not_degrading++;
    }
  }

  if (context.peer_address() == received_server_preferred_address_ &&
      received_server_preferred_address_ != default_path_.peer_address) {
    QUIC_DLOG(INFO) << "Failed to validate server preferred address : "
                    << received_server_preferred_address_;
    mutable_stats().failed_to_validate_server_preferred_address = true;
  }

  RetirePeerIssuedConnectionIdsNoLongerOnPath();
}

QuicConnectionId QuicConnection::GetOneActiveServerConnectionId() const {
  if (perspective_ == Perspective::IS_CLIENT ||
      self_issued_cid_manager_ == nullptr) {
    return connection_id();
  }
  auto active_connection_ids = GetActiveServerConnectionIds();
  QUIC_BUG_IF(quic_bug_6944, active_connection_ids.empty());
  if (active_connection_ids.empty() ||
      std::find(active_connection_ids.begin(), active_connection_ids.end(),
                connection_id()) != active_connection_ids.end()) {
    return connection_id();
  }
  QUICHE_CODE_COUNT(connection_id_on_default_path_has_been_retired);
  auto active_connection_id =
      self_issued_cid_manager_->GetOneActiveConnectionId();
  return active_connection_id;
}

std::vector<QuicConnectionId> QuicConnection::GetActiveServerConnectionIds()
    const {
  QUICHE_DCHECK_EQ(Perspective::IS_SERVER, perspective_);
  std::vector<QuicConnectionId> result;
  if (self_issued_cid_manager_ == nullptr) {
    result.push_back(default_path_.server_connection_id);
  } else {
    QUICHE_DCHECK(version().HasIetfQuicFrames());
    result = self_issued_cid_manager_->GetUnretiredConnectionIds();
  }
  if (!original_destination_connection_id_.has_value()) {
    return result;
  }
  // Add the original connection ID
  if (std::find(result.begin(), result.end(),
                original_destination_connection_id_.value()) != result.end()) {
    QUIC_BUG(quic_unexpected_original_destination_connection_id)
        << "original_destination_connection_id: "
        << original_destination_connection_id_.value()
        << " is unexpectedly in active list";
  } else {
    result.insert(result.end(), original_destination_connection_id_.value());
  }
  return result;
}

void QuicConnection::CreateConnectionIdManager() {
  if (!version().HasIetfQuicFrames()) {
    return;
  }

  if (perspective_ == Perspective::IS_CLIENT) {
    if (!default_path_.server_connection_id.IsEmpty()) {
      peer_issued_cid_manager_ =
          std::make_unique<QuicPeerIssuedConnectionIdManager>(
              kMinNumOfActiveConnectionIds, default_path_.server_connection_id,
              clock_, alarm_factory_, this, context());
    }
  } else {
    if (!default_path_.server_connection_id.IsEmpty()) {
      self_issued_cid_manager_ = MakeSelfIssuedConnectionIdManager();
    }
  }
}

void QuicConnection::QuicBugIfHasPendingFrames(QuicStreamId id) const {
  QUIC_BUG_IF(quic_has_pending_frames_unexpectedly,
              connected_ && packet_creator_.HasPendingStreamFramesOfStream(id))
      << "Stream " << id
      << " has pending frames unexpectedly. Received packet info: "
      << last_received_packet_info_;
}

void QuicConnection::SetUnackedMapInitialCapacity() {
  sent_packet_manager_.ReserveUnackedPacketsInitialCapacity(
      GetUnackedMapInitialCapacity());
}

void QuicConnection::SetSourceAddressTokenToSend(absl::string_view token) {
  QUICHE_DCHECK_EQ(perspective_, Perspective::IS_CLIENT);
  if (!packet_creator_.HasRetryToken()) {
    // Ignore received tokens (via NEW_TOKEN frame) from previous connections
    // when a RETRY token has been received.
    packet_creator_.SetRetryToken(std::string(token.data(), token.length()));
  }
}

void QuicConnection::MaybeUpdateBytesSentToAlternativeAddress(
    const QuicSocketAddress& peer_address, QuicByteCount sent_packet_size) {
  if (!version().SupportsAntiAmplificationLimit() ||
      perspective_ != Perspective::IS_SERVER) {
    return;
  }
  QUICHE_DCHECK(!IsDefaultPath(default_path_.self_address, peer_address));
  if (!IsAlternativePath(default_path_.self_address, peer_address)) {
    QUIC_DLOG(INFO) << "Wrote to uninteresting peer address: " << peer_address
                    << " default direct_peer_address_ " << direct_peer_address_
                    << " alternative path peer address "
                    << alternative_path_.peer_address;
    return;
  }
  if (alternative_path_.validated) {
    return;
  }
  if (alternative_path_.bytes_sent_before_address_validation >=
      anti_amplification_factor_ *
          alternative_path_.bytes_received_before_address_validation) {
    QUIC_LOG_FIRST_N(WARNING, 100)
        << "Server sent more data than allowed to unverified alternative "
           "peer address "
        << peer_address << " bytes sent "
        << alternative_path_.bytes_sent_before_address_validation
        << ", bytes received "
        << alternative_path_.bytes_received_before_address_validation;
  }
  alternative_path_.bytes_sent_before_address_validation += sent_packet_size;
}

void QuicConnection::MaybeUpdateBytesReceivedFromAlternativeAddress(
    QuicByteCount received_packet_size) {
  if (!version().SupportsAntiAmplificationLimit() ||
      perspective_ != Perspective::IS_SERVER ||
      !IsAlternativePath(last_received_packet_info_.destination_address,
                         GetEffectivePeerAddressFromCurrentPacket()) ||
      last_received_packet_info_.received_bytes_counted) {
    return;
  }
  // Only update bytes received if this probing frame is received on the most
  // recent alternative path.
  QUICHE_DCHECK(!IsDefaultPath(last_received_packet_info_.destination_address,
                               GetEffectivePeerAddressFromCurrentPacket()));
  if (!alternative_path_.validated) {
    alternative_path_.bytes_received_before_address_validation +=
        received_packet_size;
  }
  last_received_packet_info_.received_bytes_counted = true;
}

bool QuicConnection::IsDefaultPath(
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address) const {
  return direct_peer_address_ == peer_address &&
         default_path_.self_address == self_address;
}

bool QuicConnection::IsAlternativePath(
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address) const {
  return alternative_path_.peer_address == peer_address &&
         alternative_path_.self_address == self_address;
}

void QuicConnection::PathState::Clear() {
  self_address = QuicSocketAddress();
  peer_address = QuicSocketAddress();
  client_connection_id = {};
  server_connection_id = {};
  validated = false;
  bytes_received_before_address_validation = 0;
  bytes_sent_before_address_validation = 0;
  send_algorithm = nullptr;
  rtt_stats = absl::nullopt;
  stateless_reset_token.reset();
  ecn_marked_packet_acked = false;
  ecn_pto_count = 0;
}

QuicConnection::PathState::PathState(PathState&& other) {
  *this = std::move(other);
}

QuicConnection::PathState& QuicConnection::PathState::operator=(
    QuicConnection::PathState&& other) {
  if (this != &other) {
    self_address = other.self_address;
    peer_address = other.peer_address;
    client_connection_id = other.client_connection_id;
    server_connection_id = other.server_connection_id;
    stateless_reset_token = other.stateless_reset_token;
    validated = other.validated;
    bytes_received_before_address_validation =
        other.bytes_received_before_address_validation;
    bytes_sent_before_address_validation =
        other.bytes_sent_before_address_validation;
    send_algorithm = std::move(other.send_algorithm);
    if (other.rtt_stats.has_value()) {
      rtt_stats.emplace();
      rtt_stats->CloneFrom(other.rtt_stats.value());
    } else {
      rtt_stats.reset();
    }
    other.Clear();
  }
  return *this;
}

bool QuicConnection::IsReceivedPeerAddressValidated() const {
  QuicSocketAddress current_effective_peer_address =
      GetEffectivePeerAddressFromCurrentPacket();
  QUICHE_DCHECK(current_effective_peer_address.IsInitialized());
  return (alternative_path_.peer_address.host() ==
              current_effective_peer_address.host() &&
          alternative_path_.validated) ||
         (default_path_.validated && default_path_.peer_address.host() ==
                                         current_effective_peer_address.host());
}

void QuicConnection::OnMultiPortPathProbingSuccess(
    std::unique_ptr<QuicPathValidationContext> context, QuicTime start_time) {
  QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, perspective());
  alternative_path_.validated = true;
  multi_port_path_context_ = std::move(context);
  multi_port_probing_alarm_->Set(clock_->ApproximateNow() +
                                 multi_port_probing_interval_);
  if (multi_port_stats_ != nullptr) {
    auto now = clock_->Now();
    auto time_delta = now - start_time;
    multi_port_stats_->rtt_stats.UpdateRtt(time_delta, QuicTime::Delta::Zero(),
                                           now);
    if (is_path_degrading_) {
      multi_port_stats_->rtt_stats_when_default_path_degrading.UpdateRtt(
          time_delta, QuicTime::Delta::Zero(), now);
    }
  }
}

void QuicConnection::MaybeProbeMultiPortPath() {
  if (!connected_ || path_validator_.HasPendingPathValidation() ||
      !multi_port_path_context_ ||
      alternative_path_.self_address !=
          multi_port_path_context_->self_address() ||
      alternative_path_.peer_address !=
          multi_port_path_context_->peer_address() ||
      !visitor_->ShouldKeepConnectionAlive() ||
      multi_port_probing_alarm_->IsSet()) {
    return;
  }
  auto multi_port_validation_result_delegate =
      std::make_unique<MultiPortPathValidationResultDelegate>(this);
  path_validator_.StartPathValidation(
      std::move(multi_port_path_context_),
      std::move(multi_port_validation_result_delegate),
      PathValidationReason::kMultiPort);
}

void QuicConnection::ContextObserver::OnMultiPortPathContextAvailable(
    std::unique_ptr<QuicPathValidationContext> path_context) {
  if (!path_context) {
    return;
  }
  auto multi_port_validation_result_delegate =
      std::make_unique<MultiPortPathValidationResultDelegate>(connection_);
  connection_->multi_port_probing_alarm_->Cancel();
  connection_->multi_port_path_context_ = nullptr;
  connection_->multi_port_stats_->num_multi_port_paths_created++;
  connection_->ValidatePath(std::move(path_context),
                            std::move(multi_port_validation_result_delegate),
                            PathValidationReason::kMultiPort);
}

QuicConnection::MultiPortPathValidationResultDelegate::
    MultiPortPathValidationResultDelegate(QuicConnection* connection)
    : connection_(connection) {
  QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, connection->perspective());
}

void QuicConnection::MultiPortPathValidationResultDelegate::
    OnPathValidationSuccess(std::unique_ptr<QuicPathValidationContext> context,
                            QuicTime start_time) {
  connection_->OnMultiPortPathProbingSuccess(std::move(context), start_time);
}

void QuicConnection::MultiPortPathValidationResultDelegate::
    OnPathValidationFailure(
        std::unique_ptr<QuicPathValidationContext> context) {
  connection_->OnPathValidationFailureAtClient(/*is_multi_port=*/true,
                                               *context);
}

QuicConnection::ReversePathValidationResultDelegate::
    ReversePathValidationResultDelegate(
        QuicConnection* connection,
        const QuicSocketAddress& direct_peer_address)
    : QuicPathValidator::ResultDelegate(),
      connection_(connection),
      original_direct_peer_address_(direct_peer_address),
      peer_address_default_path_(connection->direct_peer_address_),
      peer_address_alternative_path_(
          connection_->alternative_path_.peer_address),
      active_effective_peer_migration_type_(
          connection_->active_effective_peer_migration_type_) {}

void QuicConnection::ReversePathValidationResultDelegate::
    OnPathValidationSuccess(std::unique_ptr<QuicPathValidationContext> context,
                            QuicTime start_time) {
  QUIC_DLOG(INFO) << "Successfully validated new path " << *context
                  << ", validation started at " << start_time;
  if (connection_->IsDefaultPath(context->self_address(),
                                 context->peer_address())) {
    QUIC_CODE_COUNT_N(quic_kick_off_client_address_validation, 3, 6);
    if (connection_->active_effective_peer_migration_type_ == NO_CHANGE) {
      std::string error_detail = absl::StrCat(
          "Reverse path validation on default path from ",
          context->self_address().ToString(), " to ",
          context->peer_address().ToString(),
          " completed without active peer address change: current "
          "peer address on default path ",
          connection_->direct_peer_address_.ToString(),
          ", peer address on default path when the reverse path "
          "validation was kicked off ",
          peer_address_default_path_.ToString(),
          ", peer address on alternative path when the reverse "
          "path validation was kicked off ",
          peer_address_alternative_path_.ToString(),
          ", with active_effective_peer_migration_type_ = ",
          AddressChangeTypeToString(active_effective_peer_migration_type_),
          ". The last received packet number ",
          connection_->last_received_packet_info_.header.packet_number
              .ToString(),
          " Connection is connected: ", connection_->connected_);
      QUIC_BUG(quic_bug_10511_43) << error_detail;
    }
    connection_->OnEffectivePeerMigrationValidated(
        connection_->alternative_path_.server_connection_id ==
        connection_->default_path_.server_connection_id);
  } else {
    QUICHE_DCHECK(connection_->IsAlternativePath(
        context->self_address(), context->effective_peer_address()));
    QUIC_CODE_COUNT_N(quic_kick_off_client_address_validation, 4, 6);
    QUIC_DVLOG(1) << "Mark alternative peer address "
                  << context->effective_peer_address() << " validated.";
    connection_->alternative_path_.validated = true;
  }
}

void QuicConnection::ReversePathValidationResultDelegate::
    OnPathValidationFailure(
        std::unique_ptr<QuicPathValidationContext> context) {
  if (!connection_->connected()) {
    return;
  }
  QUIC_DLOG(INFO) << "Fail to validate new path " << *context;
  if (connection_->IsDefaultPath(context->self_address(),
                                 context->peer_address())) {
    // Only act upon validation failure on the default path.
    QUIC_CODE_COUNT_N(quic_kick_off_client_address_validation, 5, 6);
    connection_->RestoreToLastValidatedPath(original_direct_peer_address_);
  } else if (connection_->IsAlternativePath(
                 context->self_address(), context->effective_peer_address())) {
    QUIC_CODE_COUNT_N(quic_kick_off_client_address_validation, 6, 6);
    connection_->alternative_path_.Clear();
  }
  connection_->RetirePeerIssuedConnectionIdsNoLongerOnPath();
}

QuicConnection::ScopedRetransmissionTimeoutIndicator::
    ScopedRetransmissionTimeoutIndicator(QuicConnection* connection)
    : connection_(connection) {
  QUICHE_DCHECK(!connection_->in_probe_time_out_)
      << "ScopedRetransmissionTimeoutIndicator is not supposed to be nested";
  connection_->in_probe_time_out_ = true;
}

QuicConnection::ScopedRetransmissionTimeoutIndicator::
    ~ScopedRetransmissionTimeoutIndicator() {
  QUICHE_DCHECK(connection_->in_probe_time_out_);
  connection_->in_probe_time_out_ = false;
}

void QuicConnection::RestoreToLastValidatedPath(
    QuicSocketAddress original_direct_peer_address) {
  QUIC_DLOG(INFO) << "Switch back to use the old peer address "
                  << alternative_path_.peer_address;
  if (!alternative_path_.validated) {
    // If not validated by now, close connection silently so that the following
    // packets received will be rejected.
    CloseConnection(QUIC_INTERNAL_ERROR,
                    "No validated peer address to use after reverse path "
                    "validation failure.",
                    ConnectionCloseBehavior::SILENT_CLOSE);
    return;
  }
  MaybeClearQueuedPacketsOnPathChange();

  // Revert congestion control context to old state.
  OnPeerIpAddressChanged();

  if (alternative_path_.send_algorithm != nullptr) {
    sent_packet_manager_.SetSendAlgorithm(
        alternative_path_.send_algorithm.release());
    sent_packet_manager_.SetRttStats(alternative_path_.rtt_stats.value());
  } else {
    QUIC_BUG(quic_bug_10511_42)
        << "Fail to store congestion controller before migration.";
  }

  UpdatePeerAddress(original_direct_peer_address);
  SetDefaultPathState(std::move(alternative_path_));

  active_effective_peer_migration_type_ = NO_CHANGE;
  ++stats_.num_invalid_peer_migration;
  // The reverse path validation failed because of alarm firing, flush all the
  // pending writes previously throttled by anti-amplification limit.
  WriteIfNotBlocked();
}

std::unique_ptr<SendAlgorithmInterface>
QuicConnection::OnPeerIpAddressChanged() {
  QUICHE_DCHECK(framer_.version().HasIetfQuicFrames());
  std::unique_ptr<SendAlgorithmInterface> old_send_algorithm =
      sent_packet_manager_.OnConnectionMigration(
          /*reset_send_algorithm=*/true);
  // OnConnectionMigration() should have marked in-flight packets to be
  // retransmitted if there is any.
  QUICHE_DCHECK(!sent_packet_manager_.HasInFlightPackets());
  // OnConnectionMigration() may have changed the retransmission timer, so
  // re-arm it.
  SetRetransmissionAlarm();
  // Stop detections in quiecense.
  blackhole_detector_.StopDetection(/*permanent=*/false);
  return old_send_algorithm;
}

void QuicConnection::set_keep_alive_ping_timeout(
    QuicTime::Delta keep_alive_ping_timeout) {
  ping_manager_.set_keep_alive_timeout(keep_alive_ping_timeout);
}

void QuicConnection::set_initial_retransmittable_on_wire_timeout(
    QuicTime::Delta retransmittable_on_wire_timeout) {
  ping_manager_.set_initial_retransmittable_on_wire_timeout(
      retransmittable_on_wire_timeout);
}

bool QuicConnection::IsValidatingServerPreferredAddress() const {
  QUICHE_DCHECK_EQ(perspective_, Perspective::IS_CLIENT);
  return received_server_preferred_address_.IsInitialized() &&
         received_server_preferred_address_ != default_path_.peer_address &&
         path_validator_.HasPendingPathValidation() &&
         path_validator_.GetContext()->peer_address() ==
             received_server_preferred_address_;
}

void QuicConnection::OnServerPreferredAddressValidated(
    QuicPathValidationContext& context, bool owns_writer) {
  QUIC_DLOG(INFO) << "Server preferred address: " << context.peer_address()
                  << " validated. Migrating path, self_address: "
                  << context.self_address()
                  << ", peer_address: " << context.peer_address();
  mutable_stats().server_preferred_address_validated = true;
  const bool success =
      MigratePath(context.self_address(), context.peer_address(),
                  context.WriterToUse(), owns_writer);
  QUIC_BUG_IF(failed to migrate to server preferred address, !success)
      << "Failed to migrate to server preferred address: "
      << context.peer_address() << " after successful validation";
}

bool QuicConnection::set_ecn_codepoint(QuicEcnCodepoint ecn_codepoint) {
  if (!GetQuicReloadableFlag(quic_send_ect1)) {
    return false;
  }
  QUIC_RELOADABLE_FLAG_COUNT_N(quic_send_ect1, 3, 8);
  if (disable_ecn_codepoint_validation_ || ecn_codepoint == ECN_NOT_ECT) {
    packet_writer_params_.ecn_codepoint = ecn_codepoint;
    return true;
  }
  if (!writer_->SupportsEcn()) {
    return false;
  }
  switch (ecn_codepoint) {
    case ECN_NOT_ECT:
      QUICHE_DCHECK(false);
      break;
    case ECN_ECT0:
      if (!sent_packet_manager_.GetSendAlgorithm()->SupportsECT0()) {
        return false;
      }
      break;
    case ECN_ECT1:
      if (!sent_packet_manager_.GetSendAlgorithm()->SupportsECT1()) {
        return false;
      }
      break;
    case ECN_CE:
      return false;
  }
  packet_writer_params_.ecn_codepoint = ecn_codepoint;
  return true;
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic

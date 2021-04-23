// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/quic_connection.h"

#include <string.h>
#include <sys/types.h>

#include <algorithm>
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
#include "quic/core/congestion_control/rtt_stats.h"
#include "quic/core/congestion_control/send_algorithm_interface.h"
#include "quic/core/crypto/crypto_protocol.h"
#include "quic/core/crypto/crypto_utils.h"
#include "quic/core/crypto/quic_decrypter.h"
#include "quic/core/crypto/quic_encrypter.h"
#include "quic/core/proto/cached_network_parameters_proto.h"
#include "quic/core/quic_bandwidth.h"
#include "quic/core/quic_config.h"
#include "quic/core/quic_connection_id.h"
#include "quic/core/quic_constants.h"
#include "quic/core/quic_error_codes.h"
#include "quic/core/quic_legacy_version_encapsulator.h"
#include "quic/core/quic_packet_creator.h"
#include "quic/core/quic_packet_writer.h"
#include "quic/core/quic_path_validator.h"
#include "quic/core/quic_types.h"
#include "quic/core/quic_utils.h"
#include "quic/platform/api/quic_bug_tracker.h"
#include "quic/platform/api/quic_client_stats.h"
#include "quic/platform/api/quic_error_code_wrappers.h"
#include "quic/platform/api/quic_exported_stats.h"
#include "quic/platform/api/quic_flag_utils.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_hostname_utils.h"
#include "quic/platform/api/quic_logging.h"
#include "quic/platform/api/quic_map_util.h"
#include "quic/platform/api/quic_server_stats.h"
#include "quic/platform/api/quic_socket_address.h"
#include "common/platform/api/quiche_text_utils.h"

namespace quic {

class QuicDecrypter;
class QuicEncrypter;

namespace {

// Maximum number of consecutive sent nonretransmittable packets.
const QuicPacketCount kMaxConsecutiveNonRetransmittablePackets = 19;

// The minimum release time into future in ms.
const int kMinReleaseTimeIntoFutureMs = 1;

// An alarm that is scheduled to send an ack if a timeout occurs.
class AckAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit AckAlarmDelegate(QuicConnection* connection)
      : connection_(connection) {}
  AckAlarmDelegate(const AckAlarmDelegate&) = delete;
  AckAlarmDelegate& operator=(const AckAlarmDelegate&) = delete;

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

 private:
  QuicConnection* connection_;
};

// This alarm will be scheduled any time a data-bearing packet is sent out.
// When the alarm goes off, the connection checks to see if the oldest packets
// have been acked, and retransmit them if they have not.
class RetransmissionAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit RetransmissionAlarmDelegate(QuicConnection* connection)
      : connection_(connection) {}
  RetransmissionAlarmDelegate(const RetransmissionAlarmDelegate&) = delete;
  RetransmissionAlarmDelegate& operator=(const RetransmissionAlarmDelegate&) =
      delete;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    connection_->OnRetransmissionTimeout();
  }

 private:
  QuicConnection* connection_;
};

// An alarm that is scheduled when the SentPacketManager requires a delay
// before sending packets and fires when the packet may be sent.
class SendAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit SendAlarmDelegate(QuicConnection* connection)
      : connection_(connection) {}
  SendAlarmDelegate(const SendAlarmDelegate&) = delete;
  SendAlarmDelegate& operator=(const SendAlarmDelegate&) = delete;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    connection_->WriteAndBundleAcksIfNotBlocked();
  }

 private:
  QuicConnection* connection_;
};

class PingAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit PingAlarmDelegate(QuicConnection* connection)
      : connection_(connection) {}
  PingAlarmDelegate(const PingAlarmDelegate&) = delete;
  PingAlarmDelegate& operator=(const PingAlarmDelegate&) = delete;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    connection_->OnPingTimeout();
  }

 private:
  QuicConnection* connection_;
};

class MtuDiscoveryAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit MtuDiscoveryAlarmDelegate(QuicConnection* connection)
      : connection_(connection) {}
  MtuDiscoveryAlarmDelegate(const MtuDiscoveryAlarmDelegate&) = delete;
  MtuDiscoveryAlarmDelegate& operator=(const MtuDiscoveryAlarmDelegate&) =
      delete;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    connection_->DiscoverMtu();
  }

 private:
  QuicConnection* connection_;
};

class ProcessUndecryptablePacketsAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit ProcessUndecryptablePacketsAlarmDelegate(QuicConnection* connection)
      : connection_(connection) {}
  ProcessUndecryptablePacketsAlarmDelegate(
      const ProcessUndecryptablePacketsAlarmDelegate&) = delete;
  ProcessUndecryptablePacketsAlarmDelegate& operator=(
      const ProcessUndecryptablePacketsAlarmDelegate&) = delete;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    QuicConnection::ScopedPacketFlusher flusher(connection_);
    connection_->MaybeProcessUndecryptablePackets();
  }

 private:
  QuicConnection* connection_;
};

class DiscardPreviousOneRttKeysAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit DiscardPreviousOneRttKeysAlarmDelegate(QuicConnection* connection)
      : connection_(connection) {}
  DiscardPreviousOneRttKeysAlarmDelegate(
      const DiscardPreviousOneRttKeysAlarmDelegate&) = delete;
  DiscardPreviousOneRttKeysAlarmDelegate& operator=(
      const DiscardPreviousOneRttKeysAlarmDelegate&) = delete;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    connection_->DiscardPreviousOneRttKeys();
  }

 private:
  QuicConnection* connection_;
};

class DiscardZeroRttDecryptionKeysAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit DiscardZeroRttDecryptionKeysAlarmDelegate(QuicConnection* connection)
      : connection_(connection) {}
  DiscardZeroRttDecryptionKeysAlarmDelegate(
      const DiscardZeroRttDecryptionKeysAlarmDelegate&) = delete;
  DiscardZeroRttDecryptionKeysAlarmDelegate& operator=(
      const DiscardZeroRttDecryptionKeysAlarmDelegate&) = delete;

  void OnAlarm() override {
    QUICHE_DCHECK(connection_->connected());
    QUIC_DLOG(INFO) << "0-RTT discard alarm fired";
    connection_->RemoveDecrypter(ENCRYPTION_ZERO_RTT);
  }

 private:
  QuicConnection* connection_;
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
bool PacketCanReplaceConnectionId(const QuicPacketHeader& header,
                                  Perspective perspective) {
  return perspective == Perspective::IS_CLIENT &&
         header.form == IETF_QUIC_LONG_HEADER_PACKET &&
         header.version.IsKnown() &&
         header.version.AllowsVariableLengthConnectionIds() &&
         (header.long_packet_type == INITIAL ||
          header.long_packet_type == RETRY);
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

}  // namespace

#define ENDPOINT \
  (perspective_ == Perspective::IS_SERVER ? "Server: " : "Client: ")

QuicConnection::QuicConnection(
    QuicConnectionId server_connection_id,
    QuicSocketAddress initial_self_address,
    QuicSocketAddress initial_peer_address,
    QuicConnectionHelperInterface* helper,
    QuicAlarmFactory* alarm_factory,
    QuicPacketWriter* writer,
    bool owns_writer,
    Perspective perspective,
    const ParsedQuicVersionVector& supported_versions)
    : framer_(supported_versions,
              helper->GetClock()->ApproximateNow(),
              perspective,
              server_connection_id.length()),
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
      server_connection_id_(server_connection_id),
      client_connection_id_(EmptyQuicConnectionId()),
      client_connection_id_is_set_(false),
      direct_peer_address_(initial_peer_address),
      default_path_(initial_self_address, QuicSocketAddress()),
      active_effective_peer_migration_type_(NO_CHANGE),
      support_key_update_for_connection_(false),
      last_packet_decrypted_(false),
      last_size_(0),
      current_packet_data_(nullptr),
      last_decrypted_packet_level_(ENCRYPTION_INITIAL),
      should_last_packet_instigate_acks_(false),
      max_undecryptable_packets_(0),
      max_tracked_packets_(GetQuicFlag(FLAGS_quic_max_tracked_packet_count)),
      idle_timeout_connection_close_behavior_(
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET),
      num_rtos_for_blackhole_detection_(0),
      uber_received_packet_manager_(&stats_),
      stop_waiting_count_(0),
      pending_retransmission_alarm_(false),
      defer_send_in_response_to_packets_(false),
      ping_timeout_(QuicTime::Delta::FromSeconds(kPingTimeoutSecs)),
      initial_retransmittable_on_wire_timeout_(QuicTime::Delta::Infinite()),
      consecutive_retransmittable_on_wire_ping_count_(0),
      retransmittable_on_wire_ping_count_(0),
      arena_(),
      ack_alarm_(alarm_factory_->CreateAlarm(arena_.New<AckAlarmDelegate>(this),
                                             &arena_)),
      retransmission_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<RetransmissionAlarmDelegate>(this),
          &arena_)),
      send_alarm_(
          alarm_factory_->CreateAlarm(arena_.New<SendAlarmDelegate>(this),
                                      &arena_)),
      ping_alarm_(
          alarm_factory_->CreateAlarm(arena_.New<PingAlarmDelegate>(this),
                                      &arena_)),
      mtu_discovery_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<MtuDiscoveryAlarmDelegate>(this),
          &arena_)),
      process_undecryptable_packets_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<ProcessUndecryptablePacketsAlarmDelegate>(this),
          &arena_)),
      discard_previous_one_rtt_keys_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<DiscardPreviousOneRttKeysAlarmDelegate>(this),
          &arena_)),
      discard_zero_rtt_decryption_keys_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<DiscardZeroRttDecryptionKeysAlarmDelegate>(this),
          &arena_)),
      visitor_(nullptr),
      debug_visitor_(nullptr),
      packet_creator_(server_connection_id_, &framer_, random_generator_, this),
      time_of_last_received_packet_(clock_->ApproximateNow()),
      sent_packet_manager_(perspective,
                           clock_,
                           random_generator_,
                           &stats_,
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
      no_stop_waiting_frames_(version().HasIetfInvariantHeader()),
      consecutive_num_packets_with_no_retransmittable_frames_(0),
      max_consecutive_num_packets_with_no_retransmittable_frames_(
          kMaxConsecutiveNonRetransmittablePackets),
      bundle_retransmittable_with_pto_ack_(false),
      fill_up_link_during_probing_(false),
      probing_retransmission_pending_(false),
      stateless_reset_token_received_(false),
      received_stateless_reset_token_(0),
      last_control_frame_id_(kInvalidControlFrameId),
      is_path_degrading_(false),
      processing_ack_frame_(false),
      supports_release_time_(false),
      release_time_into_future_(QuicTime::Delta::Zero()),
      blackhole_detector_(this, &arena_, alarm_factory_),
      idle_network_detector_(this,
                             clock_->ApproximateNow(),
                             &arena_,
                             alarm_factory_),
      encrypted_control_frames_(
          GetQuicReloadableFlag(quic_encrypted_control_frames)),
      use_encryption_level_context_(
          encrypted_control_frames_ &&
          GetQuicReloadableFlag(quic_use_encryption_level_context)),
      path_validator_(alarm_factory_, &arena_, this, random_generator_),
      alternative_path_(QuicSocketAddress(), QuicSocketAddress()),
      most_recent_frame_type_(NUM_FRAME_TYPES),
      validate_client_addresses_(
          framer_.version().HasIetfQuicFrames() && use_path_validator_ &&
          count_bytes_on_alternative_path_separately_ &&
          update_packet_content_returns_connected_ &&
          GetQuicReloadableFlag(quic_server_reverse_validate_new_path)) {
  QUIC_BUG_IF(!start_peer_migration_earlier_ && send_path_response_);

  QUICHE_DCHECK(perspective_ == Perspective::IS_CLIENT ||
                default_path_.self_address.IsInitialized());

  if (use_encryption_level_context_) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_use_encryption_level_context);
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Created connection with server connection ID "
                  << server_connection_id
                  << " and version: " << ParsedQuicVersionToString(version());

  QUIC_BUG_IF(!QuicUtils::IsConnectionIdValidForVersion(server_connection_id,
                                                        transport_version()))
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
  InstallInitialCrypters(server_connection_id_);

  // On the server side, version negotiation has been done by the dispatcher,
  // and the server connection is created with the right version.
  if (perspective_ == Perspective::IS_SERVER) {
    SetVersionNegotiated();
  }
  if (default_enable_5rto_blackhole_detection_) {
    num_rtos_for_blackhole_detection_ = 5;
    if (GetQuicReloadableFlag(quic_disable_server_blackhole_detection) &&
        perspective_ == Perspective::IS_SERVER) {
      QUIC_RELOADABLE_FLAG_COUNT(quic_disable_server_blackhole_detection);
      blackhole_detection_disabled_ = true;
    }
  }
  packet_creator_.SetDefaultPeerAddress(initial_peer_address);
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

void QuicConnection::ClearQueuedPackets() {
  buffered_packets_.clear();
}

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
    expected_initial_source_connection_id = server_connection_id_;
  } else {
    expected_initial_source_connection_id = client_connection_id_;
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
    support_key_update_for_connection_ =
        config.KeyUpdateSupportedForConnection();
    framer_.SetKeyUpdateSupportForConnection(
        support_key_update_for_connection_);
  } else {
    SetNetworkTimeouts(config.max_time_before_crypto_handshake(),
                       config.max_idle_time_before_crypto_handshake());
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
    if (config.HasClientSentConnectionOption(k2RTO, perspective_)) {
      QUIC_CODE_COUNT(quic_2rto_blackhole_detection);
      num_rtos_for_blackhole_detection_ = 2;
    }
    if (config.HasClientSentConnectionOption(k3RTO, perspective_)) {
      QUIC_CODE_COUNT(quic_3rto_blackhole_detection);
      num_rtos_for_blackhole_detection_ = 3;
    }
    if (config.HasClientSentConnectionOption(k4RTO, perspective_)) {
      QUIC_CODE_COUNT(quic_4rto_blackhole_detection);
      num_rtos_for_blackhole_detection_ = 4;
    }
    if (config.HasClientSentConnectionOption(k6RTO, perspective_)) {
      QUIC_CODE_COUNT(quic_6rto_blackhole_detection);
      num_rtos_for_blackhole_detection_ = 6;
    }
  }

  if (config.HasClientRequestedIndependentOption(kFIDT, perspective_)) {
    idle_network_detector_.enable_shorter_idle_timeout_on_sent_packet();
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
  if (sent_packet_manager_.pto_enabled()) {
    if (config.HasClientSentConnectionOption(k6PTO, perspective_) ||
        config.HasClientSentConnectionOption(k7PTO, perspective_) ||
        config.HasClientSentConnectionOption(k8PTO, perspective_)) {
      num_rtos_for_blackhole_detection_ = 5;
    }
  }
  if (config.HasClientSentConnectionOption(kNSTP, perspective_)) {
    no_stop_waiting_frames_ = true;
  }
  if (config.HasReceivedStatelessResetToken()) {
    stateless_reset_token_received_ = true;
    received_stateless_reset_token_ = config.ReceivedStatelessResetToken();
  }
  if (config.HasReceivedAckDelayExponent()) {
    framer_.set_peer_ack_delay_exponent(config.ReceivedAckDelayExponent());
  }
  if (GetQuicReloadableFlag(quic_send_timestamps) &&
      config.HasClientSentConnectionOption(kSTMP, perspective_)) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_send_timestamps);
    framer_.set_process_timestamps(true);
    uber_received_packet_manager_.set_save_timestamps(true);
  }
  if (config.HasClientSentConnectionOption(kEACK, perspective_)) {
    bundle_retransmittable_with_pto_ack_ = true;
  }
  if (GetQuicReloadableFlag(quic_dont_defer_sending) &&
      config.HasClientSentConnectionOption(kDFER, perspective_)) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_dont_defer_sending);
    defer_send_in_response_to_packets_ = false;
  }
  if (config.HasReceivedMaxPacketSize()) {
    peer_max_packet_size_ = config.ReceivedMaxPacketSize();
    MaybeUpdatePacketCreatorMaxPacketLengthAndPadding();
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
}

void QuicConnection::EnableLegacyVersionEncapsulation(
    const std::string& server_name) {
  if (perspective_ != Perspective::IS_CLIENT) {
    QUIC_BUG << "Cannot enable Legacy Version Encapsulation on the server";
    return;
  }
  if (legacy_version_encapsulation_enabled_) {
    QUIC_BUG << "Do not call EnableLegacyVersionEncapsulation twice";
    return;
  }
  if (!QuicHostnameUtils::IsValidSNI(server_name)) {
    // Legacy Version Encapsulation is only used when SNI is transmitted.
    QUIC_DLOG(INFO)
        << "Refusing to use Legacy Version Encapsulation with invalid SNI \""
        << server_name << "\"";
    return;
  }
  QUIC_DLOG(INFO) << "Enabling Legacy Version Encapsulation with SNI \""
                  << server_name << "\"";
  legacy_version_encapsulation_enabled_ = true;
  legacy_version_encapsulation_sni_ = server_name;
}

bool QuicConnection::MaybeTestLiveness() {
  QUICHE_DCHECK_EQ(perspective_, Perspective::IS_CLIENT);
  if (encryption_level_ != ENCRYPTION_FORWARD_SECURE) {
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
    if (QuicContainsValue(available_versions, version)) {
      framer_.set_version(version);
      return true;
    }
  }

  return false;
}

void QuicConnection::OnError(QuicFramer* framer) {
  // Packets that we can not or have not decrypted are dropped.
  // TODO(rch): add stats to measure this.
  if (!connected_ || last_packet_decrypted_ == false) {
    return;
  }
  CloseConnection(framer->error(), framer->detailed_error(),
                  ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicConnection::OnPacket() {
  last_packet_decrypted_ = false;
}

void QuicConnection::OnPublicResetPacket(const QuicPublicResetPacket& packet) {
  // Check that any public reset packet with a different connection ID that was
  // routed to this QuicConnection has been redirected before control reaches
  // here.  (Check for a bug regression.)
  QUICHE_DCHECK_EQ(server_connection_id_, packet.connection_id);
  QUICHE_DCHECK_EQ(perspective_, Perspective::IS_CLIENT);
  QUICHE_DCHECK(!version().HasIetfInvariantHeader());
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPublicResetPacket(packet);
  }
  std::string error_details = "Received public reset.";
  if (perspective_ == Perspective::IS_CLIENT && !packet.endpoint_id.empty()) {
    absl::StrAppend(&error_details, " From ", packet.endpoint_id, ".");
  }
  QUIC_DLOG(INFO) << ENDPOINT << error_details;
  QUIC_CODE_COUNT(quic_tear_down_local_connection_on_public_reset);
  TearDownLocalConnectionState(QUIC_PUBLIC_RESET, NO_IETF_QUIC_ERROR,
                               error_details, ConnectionCloseSource::FROM_PEER);
}

bool QuicConnection::OnProtocolVersionMismatch(
    ParsedQuicVersion received_version) {
  QUIC_DLOG(INFO) << ENDPOINT << "Received packet with mismatched version "
                  << ParsedQuicVersionToString(received_version);
  if (perspective_ == Perspective::IS_CLIENT) {
    const std::string error_details = "Protocol version mismatch.";
    QUIC_BUG << ENDPOINT << error_details;
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
  QUICHE_DCHECK_EQ(server_connection_id_, packet.connection_id);
  if (perspective_ == Perspective::IS_SERVER) {
    const std::string error_details =
        "Server received version negotiation packet.";
    QUIC_BUG << error_details;
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

  if (QuicContainsValue(packet.versions, version())) {
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
      ConnectionCloseBehavior::SILENT_CLOSE);
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
            version(), server_connection_id_, retry_without_tag,
            retry_integrity_tag)) {
      QUIC_DLOG(ERROR) << "Ignoring RETRY with invalid integrity tag";
      return;
    }
  } else {
    if (original_connection_id != server_connection_id_) {
      QUIC_DLOG(ERROR) << "Ignoring RETRY with original connection ID "
                       << original_connection_id << " not matching expected "
                       << server_connection_id_ << " token "
                       << absl::BytesToHexString(retry_token);
      return;
    }
  }
  framer_.set_drop_incoming_retry_packets(true);
  stats_.retry_packet_processed = true;
  QUIC_DLOG(INFO) << "Received RETRY, replacing connection ID "
                  << server_connection_id_ << " with " << new_connection_id
                  << ", received token " << absl::BytesToHexString(retry_token);
  if (!original_destination_connection_id_.has_value()) {
    original_destination_connection_id_ = server_connection_id_;
  }
  QUICHE_DCHECK(!retry_source_connection_id_.has_value())
      << retry_source_connection_id_.value();
  retry_source_connection_id_ = new_connection_id;
  server_connection_id_ = new_connection_id;
  packet_creator_.SetServerConnectionId(server_connection_id_);
  packet_creator_.SetRetryToken(retry_token);

  // Reinstall initial crypters because the connection ID changed.
  InstallInitialCrypters(server_connection_id_);

  sent_packet_manager_.MarkInitialPacketsForRetransmission();
}

bool QuicConnection::HasIncomingConnectionId(QuicConnectionId connection_id) {
  for (QuicConnectionId const& incoming_connection_id :
       incoming_connection_ids_) {
    if (incoming_connection_id == connection_id) {
      return true;
    }
  }
  return false;
}

void QuicConnection::SetOriginalDestinationConnectionId(
    const QuicConnectionId& original_destination_connection_id) {
  QUIC_DLOG(INFO) << "Setting original_destination_connection_id to "
                  << original_destination_connection_id
                  << " on connection with server_connection_id "
                  << server_connection_id_;
  QUICHE_DCHECK_NE(original_destination_connection_id, server_connection_id_);
  if (!HasIncomingConnectionId(original_destination_connection_id)) {
    incoming_connection_ids_.push_back(original_destination_connection_id);
  }
  InstallInitialCrypters(original_destination_connection_id);
  QUICHE_DCHECK(!original_destination_connection_id_.has_value())
      << original_destination_connection_id_.value();
  original_destination_connection_id_ = original_destination_connection_id;
}

QuicConnectionId QuicConnection::GetOriginalDestinationConnectionId() {
  if (original_destination_connection_id_.has_value()) {
    return original_destination_connection_id_.value();
  }
  return server_connection_id_;
}

bool QuicConnection::OnUnauthenticatedPublicHeader(
    const QuicPacketHeader& header) {
  // As soon as we receive an initial we start ignoring subsequent retries.
  if (header.version_flag && header.long_packet_type == INITIAL) {
    framer_.set_drop_incoming_retry_packets(true);
  }

  QuicConnectionId server_connection_id =
      GetServerConnectionIdAsRecipient(header, perspective_);

  if (server_connection_id != server_connection_id_ &&
      !HasIncomingConnectionId(server_connection_id)) {
    if (PacketCanReplaceConnectionId(header, perspective_)) {
      QUIC_DLOG(INFO) << ENDPOINT << "Accepting packet with new connection ID "
                      << server_connection_id << " instead of "
                      << server_connection_id_;
      return true;
    }

    ++stats_.packets_dropped;
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Ignoring packet from unexpected server connection ID "
                    << server_connection_id << " instead of "
                    << server_connection_id_;
    if (debug_visitor_ != nullptr) {
      debug_visitor_->OnIncorrectConnectionId(server_connection_id);
    }
    // If this is a server, the dispatcher routes each packet to the
    // QuicConnection responsible for the packet's connection ID.  So if control
    // arrives here and this is a server, the dispatcher must be malfunctioning.
    QUICHE_DCHECK_NE(Perspective::IS_SERVER, perspective_);
    return false;
  }

  if (!version().SupportsClientConnectionIds()) {
    return true;
  }

  QuicConnectionId client_connection_id =
      GetClientConnectionIdAsRecipient(header, perspective_);

  if (client_connection_id == client_connection_id_) {
    return true;
  }

  if (!client_connection_id_is_set_ && perspective_ == Perspective::IS_SERVER) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Setting client connection ID from first packet to "
                    << client_connection_id;
    set_client_connection_id(client_connection_id);
    return true;
  }

  ++stats_.packets_dropped;
  QUIC_DLOG(INFO) << ENDPOINT
                  << "Ignoring packet from unexpected client connection ID "
                  << client_connection_id << " instead of "
                  << client_connection_id_;
  return false;
}

bool QuicConnection::OnUnauthenticatedHeader(const QuicPacketHeader& header) {
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnUnauthenticatedHeader(header);
  }

  // Check that any public reset packet with a different connection ID that was
  // routed to this QuicConnection has been redirected before control reaches
  // here.
  QUICHE_DCHECK(GetServerConnectionIdAsRecipient(header, perspective_) ==
                    server_connection_id_ ||
                HasIncomingConnectionId(
                    GetServerConnectionIdAsRecipient(header, perspective_)) ||
                PacketCanReplaceConnectionId(header, perspective_));

  if (packet_creator_.HasPendingFrames()) {
    // Incoming packets may change a queued ACK frame.
    const std::string error_details =
        "Pending frames must be serialized before incoming packets are "
        "processed.";
    QUIC_BUG << error_details << ", received header: " << header;
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
  if (IsPathDegrading()) {
    // If path was previously degrading, and migration is successful after
    // probing, restart the path degrading and blackhole detection.
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

bool QuicConnection::HasPendingAcks() const {
  return ack_alarm_->IsSet();
}

void QuicConnection::OnDecryptedPacket(size_t /*length*/,
                                       EncryptionLevel level) {
  last_decrypted_packet_level_ = level;
  last_packet_decrypted_ = true;
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
      (last_decrypted_packet_level_ == ENCRYPTION_HANDSHAKE ||
       last_decrypted_packet_level_ == ENCRYPTION_FORWARD_SECURE)) {
    // Address is validated by successfully processing a HANDSHAKE or 1-RTT
    // packet.
    default_path_.validated = true;
    stats_.address_validated_via_decrypting_packet = true;
  }
  idle_network_detector_.OnPacketReceived(time_of_last_received_packet_);

  visitor_->OnPacketDecrypted(level);
}

QuicSocketAddress QuicConnection::GetEffectivePeerAddressFromCurrentPacket()
    const {
  // By default, the connection is not proxied, and the effective peer address
  // is the packet's source address, i.e. the direct peer address.
  return last_packet_source_address_;
}

bool QuicConnection::OnPacketHeader(const QuicPacketHeader& header) {
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPacketHeader(header, clock_->ApproximateNow(),
                                   last_decrypted_packet_level_);
  }

  // Will be decremented below if we fall through to return true.
  ++stats_.packets_dropped;

  if (!ProcessValidatedPacket(header)) {
    return false;
  }

  // Initialize the current packet content state.
  most_recent_frame_type_ = NUM_FRAME_TYPES;
  current_packet_content_ = NO_FRAMES_RECEIVED;
  is_current_packet_connectivity_probing_ = false;
  has_path_challenge_in_current_packet_ = false;
  current_effective_peer_migration_type_ = NO_CHANGE;

  if (perspective_ == Perspective::IS_CLIENT) {
    if (!GetLargestReceivedPacket().IsInitialized() ||
        header.packet_number > GetLargestReceivedPacket()) {
      // Update direct_peer_address_ and default path peer_address immediately
      // for client connections.
      // TODO(fayang): only change peer addresses in application data packet
      // number space.
      UpdatePeerAddress(last_packet_source_address_);
      default_path_.peer_address = GetEffectivePeerAddressFromCurrentPacket();
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

    QUIC_DLOG_IF(INFO, current_effective_peer_migration_type_ != NO_CHANGE)
        << ENDPOINT << "Effective peer's ip:port changed from "
        << default_path_.peer_address.ToString() << " to "
        << GetEffectivePeerAddressFromCurrentPacket().ToString()
        << ", active_effective_peer_migration_type is "
        << active_effective_peer_migration_type_;
  }

  --stats_.packets_dropped;
  QUIC_DVLOG(1) << ENDPOINT << "Received packet header: " << header;
  last_header_ = header;
  if (!stats_.first_decrypted_packet.IsInitialized()) {
    stats_.first_decrypted_packet = last_header_.packet_number;
  }

  // Record packet receipt to populate ack info before processing stream
  // frames, since the processing may result in sending a bundled ack.
  uber_received_packet_manager_.RecordPacketReceived(
      last_decrypted_packet_level_, last_header_,
      idle_network_detector_.time_of_last_received_packet());
  if (GetQuicReloadableFlag(quic_enable_token_based_address_validation)) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_enable_token_based_address_validation, 2,
                                 2);
    if (EnforceAntiAmplificationLimit() && !IsHandshakeConfirmed() &&
        !header.retry_token.empty() &&
        visitor_->ValidateToken(header.retry_token)) {
      QUIC_DLOG(INFO) << ENDPOINT << "Address validated via token.";
      QUIC_CODE_COUNT(quic_address_validated_via_token);
      default_path_.validated = true;
      stats_.address_validated_via_token = true;
    }
  }
  QUICHE_DCHECK(connected_);
  return true;
}

bool QuicConnection::OnStreamFrame(const QuicStreamFrame& frame) {
  QUIC_BUG_IF(!connected_)
      << "Processing STREAM frame when connection is closed. Last frame: "
      << most_recent_frame_type_;

  // Since a stream frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  if (!UpdatePacketContent(STREAM_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnStreamFrame(frame);
  }
  if (!QuicUtils::IsCryptoStreamId(transport_version(), frame.stream_id) &&
      last_decrypted_packet_level_ == ENCRYPTION_INITIAL) {
    if (MaybeConsiderAsMemoryCorruption(frame)) {
      CloseConnection(QUIC_MAYBE_CORRUPTED_MEMORY,
                      "Received crypto frame on non crypto stream.",
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return false;
    }

    QUIC_PEER_BUG << ENDPOINT
                  << "Received an unencrypted data frame: closing connection"
                  << " packet_number:" << last_header_.packet_number
                  << " stream_id:" << frame.stream_id
                  << " received_packets:" << ack_frame();
    CloseConnection(QUIC_UNENCRYPTED_STREAM_DATA,
                    "Unencrypted stream data seen.",
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  MaybeUpdateAckTimeout();
  visitor_->OnStreamFrame(frame);
  stats_.stream_bytes_received += frame.data_length;
  consecutive_retransmittable_on_wire_ping_count_ = 0;
  return connected_;
}

bool QuicConnection::OnCryptoFrame(const QuicCryptoFrame& frame) {
  QUIC_BUG_IF(!connected_)
      << "Processing CRYPTO frame when connection is closed. Last frame: "
      << most_recent_frame_type_;

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
  QUIC_BUG_IF(!connected_)
      << "Processing ACK frame start when connection is closed. Last frame: "
      << most_recent_frame_type_;

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
      last_header_.packet_number <= GetLargestReceivedPacketWithAck()) {
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
                       << ", last_decrypted_packet_level_:"
                       << last_decrypted_packet_level_;
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
  QUIC_BUG_IF(!connected_)
      << "Processing ACK frame range when connection is closed. Last frame: "
      << most_recent_frame_type_;
  QUIC_DVLOG(1) << ENDPOINT << "OnAckRange: [" << start << ", " << end << ")";

  if (GetLargestReceivedPacketWithAck().IsInitialized() &&
      last_header_.packet_number <= GetLargestReceivedPacketWithAck()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received an old ack frame: ignoring";
    return true;
  }

  sent_packet_manager_.OnAckRange(start, end);
  return true;
}

bool QuicConnection::OnAckTimestamp(QuicPacketNumber packet_number,
                                    QuicTime timestamp) {
  QUIC_BUG_IF(!connected_) << "Processing ACK frame time stamp when connection "
                              "is closed. Last frame: "
                           << most_recent_frame_type_;
  QUIC_DVLOG(1) << ENDPOINT << "OnAckTimestamp: [" << packet_number << ", "
                << timestamp.ToDebuggingValue() << ")";

  if (GetLargestReceivedPacketWithAck().IsInitialized() &&
      last_header_.packet_number <= GetLargestReceivedPacketWithAck()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received an old ack frame: ignoring";
    return true;
  }

  sent_packet_manager_.OnAckTimestamp(packet_number, timestamp);
  return true;
}

bool QuicConnection::OnAckFrameEnd(QuicPacketNumber start) {
  QUIC_BUG_IF(!connected_)
      << "Processing ACK frame end when connection is closed. Last frame: "
      << most_recent_frame_type_;
  QUIC_DVLOG(1) << ENDPOINT << "OnAckFrameEnd, start: " << start;

  if (GetLargestReceivedPacketWithAck().IsInitialized() &&
      last_header_.packet_number <= GetLargestReceivedPacketWithAck()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received an old ack frame: ignoring";
    return true;
  }
  const bool one_rtt_packet_was_acked =
      sent_packet_manager_.one_rtt_packet_acked();
  const bool zero_rtt_packet_was_acked =
      sent_packet_manager_.zero_rtt_packet_acked();
  const AckResult ack_result = sent_packet_manager_.OnAckFrameEnd(
      idle_network_detector_.time_of_last_received_packet(),
      last_header_.packet_number, last_decrypted_packet_level_);
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
  SetLargestReceivedPacketWithAck(last_header_.packet_number);
  // If the incoming ack's packets set expresses missing packets: peer is still
  // waiting for a packet lower than a packet that we are no longer planning to
  // send.
  // If the incoming ack's packets set expresses received packets: peer is still
  // acking packets which we never care about.
  // Send an ack to raise the high water mark.
  const bool send_stop_waiting =
      no_stop_waiting_frames_ ? false : GetLeastUnacked() > start;
  PostProcessAfterAckFrame(send_stop_waiting,
                           ack_result == PACKETS_NEWLY_ACKED);
  processing_ack_frame_ = false;
  return connected_;
}

bool QuicConnection::OnStopWaitingFrame(const QuicStopWaitingFrame& frame) {
  QUIC_BUG_IF(!connected_)
      << "Processing STOP_WAITING frame when connection is closed. Last frame: "
      << most_recent_frame_type_;

  // Since a stop waiting frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  if (!UpdatePacketContent(STOP_WAITING_FRAME)) {
    return false;
  }

  if (no_stop_waiting_frames_) {
    return true;
  }
  if (largest_seen_packet_with_stop_waiting_.IsInitialized() &&
      last_header_.packet_number <= largest_seen_packet_with_stop_waiting_) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Received an old stop waiting frame: ignoring";
    return true;
  }

  const char* error = ValidateStopWaitingFrame(frame);
  if (error != nullptr) {
    CloseConnection(QUIC_INVALID_STOP_WAITING_DATA, error,
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnStopWaitingFrame(frame);
  }

  largest_seen_packet_with_stop_waiting_ = last_header_.packet_number;
  uber_received_packet_manager_.DontWaitForPacketsBefore(
      last_decrypted_packet_level_, frame.least_unacked);
  return connected_;
}

bool QuicConnection::OnPaddingFrame(const QuicPaddingFrame& frame) {
  QUIC_BUG_IF(!connected_)
      << "Processing PADDING frame when connection is closed. Last frame: "
      << most_recent_frame_type_;
  if (!UpdatePacketContent(PADDING_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPaddingFrame(frame);
  }
  return true;
}

bool QuicConnection::OnPingFrame(const QuicPingFrame& frame) {
  QUIC_BUG_IF(!connected_)
      << "Processing PING frame when connection is closed. Last frame: "
      << most_recent_frame_type_;
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

const char* QuicConnection::ValidateStopWaitingFrame(
    const QuicStopWaitingFrame& stop_waiting) {
  const QuicPacketNumber peer_least_packet_awaiting_ack =
      uber_received_packet_manager_.peer_least_packet_awaiting_ack();
  if (peer_least_packet_awaiting_ack.IsInitialized() &&
      stop_waiting.least_unacked < peer_least_packet_awaiting_ack) {
    QUIC_DLOG(ERROR) << ENDPOINT << "Peer's sent low least_unacked: "
                     << stop_waiting.least_unacked << " vs "
                     << peer_least_packet_awaiting_ack;
    // We never process old ack frames, so this number should only increase.
    return "Least unacked too small.";
  }

  if (stop_waiting.least_unacked > last_header_.packet_number) {
    QUIC_DLOG(ERROR) << ENDPOINT
                     << "Peer sent least_unacked:" << stop_waiting.least_unacked
                     << " greater than the enclosing packet number:"
                     << last_header_.packet_number;
    return "Least unacked too large.";
  }

  return nullptr;
}

bool QuicConnection::OnRstStreamFrame(const QuicRstStreamFrame& frame) {
  QUIC_BUG_IF(!connected_)
      << "Processing RST_STREAM frame when connection is closed. Last frame: "
      << most_recent_frame_type_;

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
  QUIC_BUG_IF(!connected_)
      << "Processing STOP_SENDING frame when connection is closed. Last frame: "
      << most_recent_frame_type_;

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

  visitor_->OnStopSendingFrame(frame);
  return connected_;
}

class ReversePathValidationContext : public QuicPathValidationContext {
 public:
  ReversePathValidationContext(const QuicSocketAddress& self_address,
                               const QuicSocketAddress& peer_address,
                               const QuicSocketAddress& effective_peer_address,
                               QuicConnection* connection)
      : QuicPathValidationContext(self_address,
                                  peer_address,
                                  effective_peer_address),
        connection_(connection) {}

  QuicPacketWriter* WriterToUse() override { return connection_->writer(); }

 private:
  QuicConnection* connection_;
};

bool QuicConnection::OnPathChallengeFrame(const QuicPathChallengeFrame& frame) {
  QUIC_BUG_IF(!connected_) << "Processing PATH_CHALLENGE frame when connection "
                              "is closed. Last frame: "
                           << most_recent_frame_type_;
  if (has_path_challenge_in_current_packet_) {
    QUICHE_DCHECK(send_path_response_);
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_send_path_response, 2, 5);
    // Only respond to the 1st PATH_CHALLENGE in the packet.
    return true;
  }
  if (!validate_client_addresses_) {
    return OnPathChallengeFrameInternal(frame);
  }
  {
    // UpdatePacketStateAndReplyPathChallenge() may start reverse path
    // validation, if so bundle the PATH_CHALLENGE together with the
    // PATH_RESPONSE. This context needs to be out of scope before returning.
    // TODO(danzh) inline OnPathChallengeFrameInternal() once
    // support_reverse_path_validation_ is deprecated.
    QuicPacketCreator::ScopedPeerAddressContext context(
        &packet_creator_, last_packet_source_address_);
    if (!OnPathChallengeFrameInternal(frame)) {
      return false;
    }
  }
  return connected_;
}

bool QuicConnection::OnPathChallengeFrameInternal(
    const QuicPathChallengeFrame& frame) {
  // UpdatePacketContent() may start reverse path validation.
  if (!UpdatePacketContent(PATH_CHALLENGE_FRAME)) {
    return false;
  }
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPathChallengeFrame(frame);
  }

  if (!send_path_response_) {
    // Save the path challenge's payload, for later use in generating the
    // response.
    received_path_challenge_payloads_.push_back(frame.data_buffer);

    MaybeUpdateAckTimeout();
    return true;
  }
  QUIC_RELOADABLE_FLAG_COUNT_N(quic_send_path_response, 3, 5);
  has_path_challenge_in_current_packet_ = true;
  MaybeUpdateAckTimeout();
  // Queue or send PATH_RESPONSE. Send PATH_RESPONSE to the source address of
  // the current incoming packet, even if it's not the default path or the
  // alternative path.
  if (!SendPathResponse(frame.data_buffer, last_packet_source_address_)) {
    // Queue the payloads to re-try later.
    pending_path_challenge_payloads_.push_back(
        {frame.data_buffer, last_packet_source_address_});
  }
  // TODO(b/150095588): change the stats to
  // num_valid_path_challenge_received.
  ++stats_.num_connectivity_probing_received;

  // SendPathResponse() might cause connection to be closed.
  return connected_;
}

bool QuicConnection::OnPathResponseFrame(const QuicPathResponseFrame& frame) {
  QUIC_BUG_IF(!connected_) << "Processing PATH_RESPONSE frame when connection "
                              "is closed. Last frame: "
                           << most_recent_frame_type_;
  if (!UpdatePacketContent(PATH_RESPONSE_FRAME)) {
    return false;
  }
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPathResponseFrame(frame);
  }
  MaybeUpdateAckTimeout();
  if (use_path_validator_) {
    path_validator_.OnPathResponse(frame.data_buffer,
                                   last_packet_destination_address_);
  } else {
    if (!transmitted_connectivity_probe_payload_ ||
        *transmitted_connectivity_probe_payload_ != frame.data_buffer) {
      // Is not for the probe we sent, ignore it.
      return true;
    }
    // Have received the matching PATH RESPONSE, saved payload no longer valid.
    transmitted_connectivity_probe_payload_ = nullptr;
  }
  return connected_;
}

bool QuicConnection::OnConnectionCloseFrame(
    const QuicConnectionCloseFrame& frame) {
  QUIC_BUG_IF(!connected_) << "Processing CONNECTION_CLOSE frame when "
                              "connection is closed. Last frame: "
                           << most_recent_frame_type_;

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
                      << ", transport error code: " << frame.wire_error_code
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
    QUIC_LOG_FIRST_N(ERROR, 10) << "Unexpected QUIC_BAD_MULTIPATH_FLAG error."
                                << " last_received_header: " << last_header_
                                << " encryption_level: " << encryption_level_;
  }
  TearDownLocalConnectionState(frame, ConnectionCloseSource::FROM_PEER);
  return connected_;
}

bool QuicConnection::OnMaxStreamsFrame(const QuicMaxStreamsFrame& frame) {
  QUIC_BUG_IF(!connected_)
      << "Processing MAX_STREAMS frame when connection is closed. Last frame: "
      << most_recent_frame_type_;
  if (!UpdatePacketContent(MAX_STREAMS_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnMaxStreamsFrame(frame);
  }
  return visitor_->OnMaxStreamsFrame(frame) && connected_;
}

bool QuicConnection::OnStreamsBlockedFrame(
    const QuicStreamsBlockedFrame& frame) {
  QUIC_BUG_IF(!connected_) << "Processing STREAMS_BLOCKED frame when "
                              "connection is closed. Last frame: "
                           << most_recent_frame_type_;
  if (!UpdatePacketContent(STREAMS_BLOCKED_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnStreamsBlockedFrame(frame);
  }
  return visitor_->OnStreamsBlockedFrame(frame) && connected_;
}

bool QuicConnection::OnGoAwayFrame(const QuicGoAwayFrame& frame) {
  QUIC_BUG_IF(!connected_)
      << "Processing GOAWAY frame when connection is closed. Last frame: "
      << most_recent_frame_type_;

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
  QUIC_BUG_IF(!connected_) << "Processing WINDOW_UPDATE frame when connection "
                              "is closed. Last frame: "
                           << most_recent_frame_type_;

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

bool QuicConnection::OnNewConnectionIdFrame(
    const QuicNewConnectionIdFrame& frame) {
  QUIC_BUG_IF(!connected_) << "Processing NEW_CONNECTION_ID frame when "
                              "connection is closed. Last frame: "
                           << most_recent_frame_type_;
  if (!UpdatePacketContent(NEW_CONNECTION_ID_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnNewConnectionIdFrame(frame);
  }
  return true;
}

bool QuicConnection::OnRetireConnectionIdFrame(
    const QuicRetireConnectionIdFrame& frame) {
  QUIC_BUG_IF(!connected_) << "Processing RETIRE_CONNECTION_ID frame when "
                              "connection is closed. Last frame: "
                           << most_recent_frame_type_;
  if (!UpdatePacketContent(RETIRE_CONNECTION_ID_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnRetireConnectionIdFrame(frame);
  }
  return true;
}

bool QuicConnection::OnNewTokenFrame(const QuicNewTokenFrame& frame) {
  QUIC_BUG_IF(!connected_)
      << "Processing NEW_TOKEN frame when connection is closed. Last frame: "
      << most_recent_frame_type_;
  if (!UpdatePacketContent(NEW_TOKEN_FRAME)) {
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnNewTokenFrame(frame);
  }
  if (GetQuicReloadableFlag(quic_enable_token_based_address_validation)) {
    if (perspective_ == Perspective::IS_SERVER) {
      CloseConnection(QUIC_INVALID_NEW_TOKEN,
                      "Server received new token frame.",
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return false;
    }
    // NEW_TOKEN frame should insitgate ACKs.
    MaybeUpdateAckTimeout();
    visitor_->OnNewTokenReceived(frame.token);
  }
  return true;
}

bool QuicConnection::OnMessageFrame(const QuicMessageFrame& frame) {
  QUIC_BUG_IF(!connected_)
      << "Processing MESSAGE frame when connection is closed. Last frame: "
      << most_recent_frame_type_;

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
  QUIC_BUG_IF(!connected_) << "Processing HANDSHAKE_DONE frame when connection "
                              "is closed. Last frame: "
                           << most_recent_frame_type_;
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
  QUIC_BUG_IF(!connected_) << "Processing ACK_FREQUENCY frame when connection "
                              "is closed. Last frame: "
                           << most_recent_frame_type_;
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
          QuicUtils::GetPacketNumberSpace(last_decrypted_packet_level_) ==
          APPLICATION_DATA) {
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
  QUIC_BUG_IF(!connected_)
      << "Processing BLOCKED frame when connection is closed. Last frame was "
      << most_recent_frame_type_;

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
    QUICHE_DCHECK(!version().HasIetfQuicFrames());
    ++stats_.num_connectivity_probing_received;
  }

  QUIC_DVLOG(1) << ENDPOINT << "Got"
                << (SupportsMultiplePacketNumberSpaces()
                        ? (" " + EncryptionLevelToString(
                                     last_decrypted_packet_level_))
                        : "")
                << " packet " << last_header_.packet_number << " for "
                << GetServerConnectionIdAsRecipient(last_header_, perspective_);

  QUIC_DLOG_IF(INFO, current_packet_content_ == SECOND_FRAME_IS_PADDING)
      << ENDPOINT << "Received a padded PING packet. is_probing: "
      << IsCurrentPacketConnectivityProbing();

  MaybeRespondToConnectivityProbingOrMigration();

  current_effective_peer_migration_type_ = NO_CHANGE;

  // For IETF QUIC, it is guaranteed that TLS will give connection the
  // corresponding write key before read key. In other words, connection should
  // never process a packet while an ACK for it cannot be encrypted.
  if (!should_last_packet_instigate_acks_) {
    uber_received_packet_manager_.MaybeUpdateAckTimeout(
        should_last_packet_instigate_acks_, last_decrypted_packet_level_,
        last_header_.packet_number, clock_->ApproximateNow(),
        sent_packet_manager_.GetRttStats());
  }

  ClearLastFrames();
  CloseIfTooManyOutstandingSentPackets();
}

void QuicConnection::MaybeRespondToConnectivityProbingOrMigration() {
  if (version().HasIetfQuicFrames()) {
    if (send_path_response_) {
      return;
    }
    if (perspective_ == Perspective::IS_CLIENT) {
      // This node is a client, notify that a speculative connectivity probing
      // packet has been received anyway.
      visitor_->OnPacketReceived(last_packet_destination_address_,
                                 last_packet_source_address_,
                                 /*is_connectivity_probe=*/false);
      return;
    }
    if (!received_path_challenge_payloads_.empty()) {
      if (current_effective_peer_migration_type_ != NO_CHANGE) {
        // TODO(b/150095588): change the stats to
        // num_valid_path_challenge_received.
        ++stats_.num_connectivity_probing_received;
      }
      // If the packet contains PATH CHALLENGE, send appropriate RESPONSE.
      // There was at least one PATH CHALLENGE in the received packet,
      // Generate the required PATH RESPONSE.
      SendGenericPathProbePacket(nullptr, last_packet_source_address_,
                                 /* is_response=*/true);
      return;
    }
  } else {
    if (IsCurrentPacketConnectivityProbing()) {
      visitor_->OnPacketReceived(last_packet_destination_address_,
                                 last_packet_source_address_,
                                 /*is_connectivity_probe=*/true);
      return;
    }
    if (perspective_ == Perspective::IS_CLIENT) {
      // This node is a client, notify that a speculative connectivity probing
      // packet has been received anyway.
      QUIC_DVLOG(1) << ENDPOINT
                    << "Received a speculative connectivity probing packet for "
                    << GetServerConnectionIdAsRecipient(last_header_,
                                                        perspective_)
                    << " from ip:port: "
                    << last_packet_source_address_.ToString() << " to ip:port: "
                    << last_packet_destination_address_.ToString();
      visitor_->OnPacketReceived(last_packet_destination_address_,
                                 last_packet_source_address_,
                                 /*is_connectivity_probe=*/false);
      return;
    }
  }
  // Server starts to migrate connection upon receiving of non-probing packet
  // from a new peer address.
  if (!start_peer_migration_earlier_ &&
      last_header_.packet_number == GetLargestReceivedPacket()) {
    direct_peer_address_ = last_packet_source_address_;
    if (current_effective_peer_migration_type_ != NO_CHANGE) {
      // TODO(fayang): When multiple packet number spaces is supported, only
      // start peer migration for the application data.
      StartEffectivePeerMigration(current_effective_peer_migration_type_);
    }
  }
}

bool QuicConnection::IsValidStatelessResetToken(QuicUint128 token) const {
  return stateless_reset_token_received_ &&
         token == received_stateless_reset_token_;
}

void QuicConnection::OnAuthenticatedIetfStatelessResetPacket(
    const QuicIetfStatelessResetPacket& /*packet*/) {
  // TODO(fayang): Add OnAuthenticatedIetfStatelessResetPacket to
  // debug_visitor_.
  QUICHE_DCHECK(version().HasIetfInvariantHeader());
  QUICHE_DCHECK_EQ(perspective_, Perspective::IS_CLIENT);

  if (use_path_validator_) {
    if (!IsDefaultPath(last_packet_destination_address_,
                       last_packet_source_address_)) {
      // This packet is received on a probing path. Do not close connection.
      if (IsAlternativePath(last_packet_destination_address_,
                            GetEffectivePeerAddressFromCurrentPacket())) {
        QUIC_BUG_IF(alternative_path_.validated)
            << "STATELESS_RESET received on alternate path after it's "
               "validated.";
        path_validator_.CancelPathValidation();
      } else {
        QUIC_BUG << "Received Stateless Reset on unknown socket.";
      }
      return;
    }
  } else if (!visitor_->ValidateStatelessReset(last_packet_destination_address_,
                                               last_packet_source_address_)) {
    // This packet is received on a probing path. Do not close connection.
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
  bool should_close;
  if (GetQuicReloadableFlag(
          quic_close_connection_with_too_many_outstanding_packets)) {
    QUIC_RELOADABLE_FLAG_COUNT(
        quic_close_connection_with_too_many_outstanding_packets);
    should_close =
        sent_packet_manager_.GetLargestSentPacket().IsInitialized() &&
        sent_packet_manager_.GetLargestSentPacket() >
            sent_packet_manager_.GetLeastUnacked() + max_tracked_packets_;
  } else {
    should_close =
        sent_packet_manager_.GetLargestObserved().IsInitialized() &&
        sent_packet_manager_.GetLargestObserved() >
            sent_packet_manager_.GetLeastUnacked() + max_tracked_packets_;
  }
  // This occurs if we don't discard old packets we've seen fast enough. It's
  // possible largest observed is less than leaset unacked.
  if (should_close) {
    CloseConnection(
        QUIC_TOO_MANY_OUTSTANDING_SENT_PACKETS,
        absl::StrCat("More than ", max_tracked_packets_,
                     " outstanding, least_unacked: ",
                     sent_packet_manager_.GetLeastUnacked().ToUint64(),
                     ", packets_processed: ", stats_.packets_processed,
                     ", last_decrypted_packet_level: ",
                     EncryptionLevelToString(last_decrypted_packet_level_)),
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

void QuicConnection::PopulateStopWaitingFrame(
    QuicStopWaitingFrame* stop_waiting) {
  stop_waiting->least_unacked = GetLeastUnacked();
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

  // If the writer is blocked, don't attempt to send packets now or in the send
  // alarm. When the writer unblocks, OnCanWrite() will be called for this
  // connection to send.
  if (HandleWriteBlocked()) {
    return;
  }

  // Now that we have received an ack, we might be able to send packets which
  // are queued locally, or drain streams which are blocked.
  if (defer_send_in_response_to_packets_) {
    send_alarm_->Update(clock_->ApproximateNow(), QuicTime::Delta::Zero());
  } else {
    WriteAndBundleAcksIfNotBlocked();
  }
}

void QuicConnection::MaybeActivateLegacyVersionEncapsulation() {
  if (!legacy_version_encapsulation_enabled_) {
    return;
  }
  QUICHE_DCHECK(!legacy_version_encapsulation_in_progress_);
  QUIC_BUG_IF(!packet_creator_.CanSetMaxPacketLength())
      << "Cannot activate Legacy Version Encapsulation mid-packet";
  QUIC_BUG_IF(coalesced_packet_.length() != 0u)
      << "Cannot activate Legacy Version Encapsulation mid-coalesced-packet";
  legacy_version_encapsulation_in_progress_ = true;
  MaybeUpdatePacketCreatorMaxPacketLengthAndPadding();
}
void QuicConnection::MaybeDisactivateLegacyVersionEncapsulation() {
  if (!legacy_version_encapsulation_in_progress_) {
    return;
  }
  // Flush any remaining packet before disactivating encapsulation.
  packet_creator_.FlushCurrentPacket();
  QUICHE_DCHECK(legacy_version_encapsulation_enabled_);
  legacy_version_encapsulation_in_progress_ = false;
  MaybeUpdatePacketCreatorMaxPacketLengthAndPadding();
}

size_t QuicConnection::SendCryptoData(EncryptionLevel level,
                                      size_t write_length,
                                      QuicStreamOffset offset) {
  if (write_length == 0) {
    QUIC_BUG << "Attempt to send empty crypto frame";
    return 0;
  }
  if (level == ENCRYPTION_INITIAL) {
    MaybeActivateLegacyVersionEncapsulation();
  }
  size_t consumed_length;
  {
    ScopedPacketFlusher flusher(this);
    consumed_length =
        packet_creator_.ConsumeCryptoData(level, write_length, offset);
  }  // Added scope ensures packets are flushed before continuing.
  MaybeDisactivateLegacyVersionEncapsulation();
  return consumed_length;
}

QuicConsumedData QuicConnection::SendStreamData(QuicStreamId id,
                                                size_t write_length,
                                                QuicStreamOffset offset,
                                                StreamSendingState state) {
  if (state == NO_FIN && write_length == 0) {
    QUIC_BUG << "Attempt to send empty stream frame";
    return QuicConsumedData(0, false);
  }

  if (packet_creator_.encryption_level() == ENCRYPTION_INITIAL &&
      QuicUtils::IsCryptoStreamId(transport_version(), id)) {
    MaybeActivateLegacyVersionEncapsulation();
  }
  QuicConsumedData consumed_data(0, false);
  {
    // Opportunistically bundle an ack with every outgoing packet.
    // Particularly, we want to bundle with handshake packets since we don't
    // know which decrypter will be used on an ack packet following a handshake
    // packet (a handshake packet from client to server could result in a REJ or
    // a SHLO from the server, leading to two different decrypters at the
    // server.)
    ScopedPacketFlusher flusher(this);
    consumed_data =
        packet_creator_.ConsumeData(id, write_length, offset, state);
  }  // Added scope ensures packets are flushed before continuing.
  MaybeDisactivateLegacyVersionEncapsulation();
  return consumed_data;
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
  stats_.max_packet_size = packet_creator_.max_packet_length();
  stats_.max_received_packet_size = largest_received_packet_size_;
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
    EncryptionLevel decryption_level,
    bool has_decryption_key) const {
  if (encryption_level_ == ENCRYPTION_FORWARD_SECURE) {
    // We do not expect to install any further keys.
    return false;
  }
  if (undecryptable_packets_.size() >= max_undecryptable_packets_) {
    // We do not queue more than max_undecryptable_packets_ packets.
    return false;
  }
  if (has_decryption_key) {
    // We already have the key for this decryption level, therefore no
    // future keys will allow it be decrypted.
    return false;
  }
  if (version().KnowsWhichDecrypterToUse() &&
      decryption_level <= encryption_level_) {
    // On versions that know which decrypter to use, we install keys in order
    // so we will not get newer keys for lower encryption levels.
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

void QuicConnection::MaybeUpdatePacketCreatorMaxPacketLengthAndPadding() {
  QuicByteCount max_packet_length = GetLimitedMaxPacketSize(long_term_mtu_);
  if (legacy_version_encapsulation_in_progress_) {
    QUICHE_DCHECK(legacy_version_encapsulation_enabled_);
    const QuicByteCount minimum_overhead =
        QuicLegacyVersionEncapsulator::GetMinimumOverhead(
            legacy_version_encapsulation_sni_);
    if (max_packet_length < minimum_overhead) {
      QUIC_BUG << "Cannot apply Legacy Version Encapsulation overhead because "
               << "max_packet_length " << max_packet_length
               << " < minimum_overhead " << minimum_overhead;
      legacy_version_encapsulation_in_progress_ = false;
      legacy_version_encapsulation_enabled_ = false;
      MaybeUpdatePacketCreatorMaxPacketLengthAndPadding();
      return;
    }
    max_packet_length -= minimum_overhead;
  }
  packet_creator_.SetMaxPacketLength(max_packet_length);
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
  QUIC_BUG_IF(current_packet_data_ != nullptr)
      << "ProcessUdpPacket must not be called while processing a packet.";
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPacketReceived(self_address, peer_address, packet);
  }
  current_incoming_packet_received_bytes_counted_ = false;
  last_size_ = packet.length();
  current_packet_data_ = packet.data();

  last_packet_destination_address_ = self_address;
  last_packet_source_address_ = peer_address;
  if (!default_path_.self_address.IsInitialized()) {
    default_path_.self_address = last_packet_destination_address_;
  }

  if (!direct_peer_address_.IsInitialized()) {
    UpdatePeerAddress(last_packet_source_address_);
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
  if (!count_bytes_on_alternative_path_separately_) {
    if (EnforceAntiAmplificationLimit()) {
      default_path_.bytes_received_before_address_validation += last_size_;
    }
  } else if (IsDefaultPath(last_packet_destination_address_,
                           last_packet_source_address_) &&
             EnforceAntiAmplificationLimit()) {
    QUIC_CODE_COUNT_N(quic_count_bytes_on_alternative_path_seperately, 1, 5);
    current_incoming_packet_received_bytes_counted_ = true;
    default_path_.bytes_received_before_address_validation += last_size_;
  }

  // Ensure the time coming from the packet reader is within 2 minutes of now.
  if (std::abs((packet.receipt_time() - clock_->ApproximateNow()).ToSeconds()) >
      2 * 60) {
    QUIC_BUG << "Packet receipt time:"
             << packet.receipt_time().ToDebuggingValue()
             << " too far from current time:"
             << clock_->ApproximateNow().ToDebuggingValue();
  }
  time_of_last_received_packet_ = packet.receipt_time();
  QUIC_DVLOG(1) << ENDPOINT << "time of last received packet: "
                << packet.receipt_time().ToDebuggingValue() << " from peer "
                << last_packet_source_address_;

  ScopedPacketFlusher flusher(this);
  if (!framer_.ProcessPacket(packet)) {
    // If we are unable to decrypt this packet, it might be
    // because the CHLO or SHLO packet was lost.
    QUIC_DVLOG(1) << ENDPOINT
                  << "Unable to process packet.  Last packet processed: "
                  << last_header_.packet_number;
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
  if (!validate_client_addresses_ &&
      active_effective_peer_migration_type_ != NO_CHANGE &&
      sent_packet_manager_.GetLargestObserved().IsInitialized() &&
      (!highest_packet_sent_before_effective_peer_migration_.IsInitialized() ||
       sent_packet_manager_.GetLargestObserved() >
           highest_packet_sent_before_effective_peer_migration_)) {
    if (perspective_ == Perspective::IS_SERVER) {
      OnEffectivePeerMigrationValidated();
    }
  }

  MaybeProcessCoalescedPackets();
  MaybeProcessUndecryptablePackets();
  MaybeSendInResponseToPacket();
  SetPingAlarm();
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
    QUIC_BUG << ENDPOINT << error_details;
    CloseConnection(QUIC_INTERNAL_ERROR, error_details,
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  // Add a flusher to ensure the connection is marked app-limited.
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

  // TODO(danzh) PATH_RESPONSE is of more interest to the peer than ACK,
  // evaluate if it's worth to send them before sending ACKs.
  while (!pending_path_challenge_payloads_.empty()) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_send_path_response, 4, 5);
    const PendingPathChallenge& pending_path_challenge =
        pending_path_challenge_payloads_.front();
    if (!SendPathResponse(pending_path_challenge.received_path_challenge,
                          pending_path_challenge.peer_address)) {
      break;
    }
    pending_path_challenge_payloads_.pop_front();
  }
  WriteNewData();
}

void QuicConnection::WriteNewData() {
  // Sending queued packets may have caused the socket to become write blocked,
  // or the congestion manager to prohibit sending.  If we've sent everything
  // we had queued and we're still not blocked, let the visitor know it can
  // write more.
  if (!CanWrite(HAS_RETRANSMITTABLE_DATA)) {
    return;
  }

  {
    ScopedPacketFlusher flusher(this);
    visitor_->OnCanWrite();
  }

  // After the visitor writes, it may have caused the socket to become write
  // blocked or the congestion manager to prohibit sending, so check again.
  if (visitor_->WillingAndAbleToWrite() && !send_alarm_->IsSet() &&
      CanWrite(HAS_RETRANSMITTABLE_DATA)) {
    // We're not write blocked, but some stream didn't write out all of its
    // bytes. Register for 'immediate' resumption so we'll keep writing after
    // other connections and events have had a chance to use the thread.
    send_alarm_->Set(clock_->ApproximateNow());
  }
}

void QuicConnection::WriteIfNotBlocked() {
  if (!HandleWriteBlocked()) {
    OnCanWrite();
  }
}

void QuicConnection::WriteAndBundleAcksIfNotBlocked() {
  if (!HandleWriteBlocked()) {
    ScopedPacketFlusher flusher(this);
    WriteIfNotBlocked();
  }
}

bool QuicConnection::ProcessValidatedPacket(const QuicPacketHeader& header) {
  if (perspective_ == Perspective::IS_SERVER &&
      default_path_.self_address.IsInitialized() &&
      last_packet_destination_address_.IsInitialized() &&
      default_path_.self_address != last_packet_destination_address_) {
    // Allow change between pure IPv4 and equivalent mapped IPv4 address.
    if (default_path_.self_address.port() !=
            last_packet_destination_address_.port() ||
        default_path_.self_address.host().Normalized() !=
            last_packet_destination_address_.host().Normalized()) {
      if (!visitor_->AllowSelfAddressChange()) {
        CloseConnection(
            QUIC_ERROR_MIGRATING_ADDRESS,
            "Self address migration is not supported at the server.",
            ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
        return false;
      }
    }
    default_path_.self_address = last_packet_destination_address_;
  }

  if (PacketCanReplaceConnectionId(header, perspective_) &&
      server_connection_id_ != header.source_connection_id) {
    QUIC_DLOG(INFO) << ENDPOINT << "Replacing connection ID "
                    << server_connection_id_ << " with "
                    << header.source_connection_id;
    if (!original_destination_connection_id_.has_value()) {
      original_destination_connection_id_ = server_connection_id_;
    }
    server_connection_id_ = header.source_connection_id;
    packet_creator_.SetServerConnectionId(server_connection_id_);
  }

  if (!ValidateReceivedPacketNumber(header.packet_number)) {
    return false;
  }

  if (!version_negotiated_) {
    if (perspective_ == Perspective::IS_CLIENT) {
      QUICHE_DCHECK(!header.version_flag || header.form != GOOGLE_QUIC_PACKET);
      if (!version().HasIetfInvariantHeader()) {
        // If the client gets a packet without the version flag from the server
        // it should stop sending version since the version negotiation is done.
        // IETF QUIC stops sending version once encryption level switches to
        // forward secure.
        packet_creator_.StopSendingVersion();
      }
      version_negotiated_ = true;
      OnSuccessfulVersionNegotiation();
    }
  }

  if (last_size_ > largest_received_packet_size_) {
    largest_received_packet_size_ = last_size_;
  }

  if (perspective_ == Perspective::IS_SERVER &&
      encryption_level_ == ENCRYPTION_INITIAL &&
      last_size_ > packet_creator_.max_packet_length()) {
    SetMaxPacketLength(last_size_);
  }
  return true;
}

bool QuicConnection::ValidateReceivedPacketNumber(
    QuicPacketNumber packet_number) {
  // If this packet has already been seen, or the sender has told us that it
  // will not be retransmitted, then stop processing the packet.
  if (!uber_received_packet_manager_.IsAwaitingPacket(
          last_decrypted_packet_level_, packet_number)) {
    QUIC_DLOG(INFO) << ENDPOINT << "Packet " << packet_number
                    << " no longer being waited for at level "
                    << static_cast<int>(last_decrypted_packet_level_)
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
    WriteResult result = writer_->WritePacket(
        packet.encrypted_buffer.data(), packet.encrypted_buffer.length(),
        packet.self_address.host(), packet.peer_address, per_packet_options_);
    QUIC_DVLOG(1) << ENDPOINT << "Sending buffered packet, result: " << result;
    if (IsMsgTooBig(result) &&
        packet.encrypted_buffer.length() > long_term_mtu_) {
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

void QuicConnection::SendProbingRetransmissions() {
  while (sent_packet_manager_.GetSendAlgorithm()->ShouldSendProbingPacket() &&
         CanWrite(HAS_RETRANSMITTABLE_DATA)) {
    if (!visitor_->SendProbingData()) {
      QUIC_DVLOG(1)
          << "Cannot send probing retransmissions: nothing to retransmit.";
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

bool QuicConnection::ShouldGeneratePacket(
    HasRetransmittableData retransmittable,
    IsHandshake handshake) {
  QUICHE_DCHECK(handshake != IS_HANDSHAKE ||
                QuicVersionUsesCryptoFrames(transport_version()))
      << ENDPOINT
      << "Handshake in STREAM frames should not check ShouldGeneratePacket";
  if (!count_bytes_on_alternative_path_separately_) {
    return CanWrite(retransmittable);
  }
  QUIC_CODE_COUNT_N(quic_count_bytes_on_alternative_path_seperately, 4, 5);
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

const QuicFrames QuicConnection::MaybeBundleAckOpportunistically() {
  if (!ack_frequency_sent_ && sent_packet_manager_.CanSendAckFrequency()) {
    if (packet_creator_.NextSendingPacketNumber() >=
        FirstSendingPacketNumber() + kMinReceivedBeforeAckDecimation) {
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_can_send_ack_frequency, 3, 3);
      ack_frequency_sent_ = true;
      auto frame = sent_packet_manager_.GetUpdatedAckFrequencyFrame();
      visitor_->SendAckFrequency(frame);
    }
  }

  QuicFrames frames;
  const bool has_pending_ack =
      uber_received_packet_manager_
          .GetAckTimeout(QuicUtils::GetPacketNumberSpace(encryption_level_))
          .IsInitialized();
  if (!has_pending_ack && stop_waiting_count_ <= 1) {
    // No need to send an ACK.
    return frames;
  }
  ResetAckStates();

  QUIC_DVLOG(1) << ENDPOINT << "Bundle an ACK opportunistically";
  QuicFrame updated_ack_frame = GetUpdatedAckFrame();
  QUIC_BUG_IF(updated_ack_frame.ack_frame->packets.Empty())
      << ENDPOINT << "Attempted to opportunistically bundle an empty "
      << encryption_level_ << " ACK, " << (has_pending_ack ? "" : "!")
      << "has_pending_ack, stop_waiting_count_ " << stop_waiting_count_;
  frames.push_back(updated_ack_frame);

  if (!no_stop_waiting_frames_) {
    QuicStopWaitingFrame stop_waiting;
    PopulateStopWaitingFrame(&stop_waiting);
    frames.push_back(QuicFrame(stop_waiting));
  }
  return frames;
}

bool QuicConnection::CanWrite(HasRetransmittableData retransmittable) {
  if (!connected_) {
    return false;
  }

  if (fill_coalesced_packet_) {
    // Try to coalesce packet, only allow to write when creator is on soft max
    // packet length. Given the next created packet is going to fill current
    // coalesced packet, do not check amplification factor.
    return packet_creator_.HasSoftMaxPacketLength();
  }

  if (LimitedByAmplificationFactor()) {
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

  if (sent_packet_manager_.pending_timer_transmission_count() > 0) {
    // Force sending the retransmissions for HANDSHAKE, TLP, RTO, PROBING cases.
    return true;
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
  if (!supports_release_time_ || per_packet_options_ == nullptr) {
    // Don't change the release delay.
    return now;
  }

  auto next_release_time_result = sent_packet_manager_.GetNextReleaseTime();

  // Release before |now| is impossible.
  QuicTime next_release_time =
      std::max(now, next_release_time_result.release_time);
  per_packet_options_->release_time_delay = next_release_time - now;
  per_packet_options_->allow_burst = next_release_time_result.allow_burst;
  return next_release_time;
}

bool QuicConnection::WritePacket(SerializedPacket* packet) {
  if (sent_packet_manager_.GetLargestSentPacket().IsInitialized() &&
      packet->packet_number < sent_packet_manager_.GetLargestSentPacket()) {
    QUIC_BUG << "Attempt to write packet:" << packet->packet_number
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
  QuicSocketAddress send_to_address =
      (send_path_response_) ? packet->peer_address : peer_address();
  // Self address is always the default self address on this code path.
  bool send_on_current_path = send_to_address == peer_address();
  switch (fate) {
    case DISCARD:
      ++stats_.packets_discarded;
      return true;
    case COALESCE:
      QUIC_BUG_IF(!version().CanSendCoalescedPackets() || coalescing_done_);
      if (!coalesced_packet_.MaybeCoalescePacket(
              *packet, self_address(), send_to_address,
              helper_->GetStreamSendBufferAllocator(),
              packet_creator_.max_packet_length())) {
        // Failed to coalesce packet, flush current coalesced packet.
        if (!FlushCoalescedPacket()) {
          // Failed to flush coalesced packet, write error has been handled.
          return false;
        }
        if (!coalesced_packet_.MaybeCoalescePacket(
                *packet, self_address(), send_to_address,
                helper_->GetStreamSendBufferAllocator(),
                packet_creator_.max_packet_length())) {
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
      break;
    case BUFFER:
      QUIC_DVLOG(1) << ENDPOINT << "Adding packet: " << packet->packet_number
                    << " to buffered packets";
      buffered_packets_.emplace_back(*packet, self_address(), send_to_address);
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
      result = writer_->WritePacket(packet->encrypted_buffer, encrypted_length,
                                    self_address().host(), send_to_address,
                                    per_packet_options_);
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
    case LEGACY_VERSION_ENCAPSULATE: {
      QUICHE_DCHECK(!is_mtu_discovery);
      QUICHE_DCHECK_EQ(perspective_, Perspective::IS_CLIENT);
      QUICHE_DCHECK_EQ(packet->encryption_level, ENCRYPTION_INITIAL);
      QUICHE_DCHECK(legacy_version_encapsulation_enabled_);
      QUICHE_DCHECK(legacy_version_encapsulation_in_progress_);
      QuicPacketLength encapsulated_length =
          QuicLegacyVersionEncapsulator::Encapsulate(
              legacy_version_encapsulation_sni_,
              absl::string_view(packet->encrypted_buffer,
                                packet->encrypted_length),
              server_connection_id_, framer_.creation_time(),
              GetLimitedMaxPacketSize(long_term_mtu_),
              const_cast<char*>(packet->encrypted_buffer));
      if (encapsulated_length != 0) {
        stats_.sent_legacy_version_encapsulated_packets++;
        packet->encrypted_length = encapsulated_length;
        encrypted_length = encapsulated_length;
        QUIC_DVLOG(2)
            << ENDPOINT
            << "Successfully performed Legacy Version Encapsulation on "
            << packet->encryption_level << " packet number " << packet_number
            << " of length " << encrypted_length << ": " << std::endl
            << quiche::QuicheTextUtils::HexDump(absl::string_view(
                   packet->encrypted_buffer, encrypted_length));
      } else {
        QUIC_BUG << ENDPOINT
                 << "Failed to perform Legacy Version Encapsulation on "
                 << packet->encryption_level << " packet number "
                 << packet_number << " of length " << encrypted_length;
      }
      if (!buffered_packets_.empty() || HandleWriteBlocked()) {
        // Buffer the packet.
        buffered_packets_.emplace_back(*packet, self_address(),
                                       send_to_address);
      } else {  // Send the packet to the writer.
        // writer_->WritePacket transfers buffer ownership back to the writer.
        packet->release_encrypted_buffer = nullptr;
        result = writer_->WritePacket(packet->encrypted_buffer,
                                      encrypted_length, self_address().host(),
                                      send_to_address, per_packet_options_);
      }
    } break;
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
      buffered_packets_.emplace_back(*packet, self_address(), send_to_address);
    }
  }

  // In some cases, an MTU probe can cause EMSGSIZE. This indicates that the
  // MTU discovery is permanently unsuccessful.
  if (IsMsgTooBig(result)) {
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
    if (use_path_validator_ && !send_on_current_path) {
      // Only handle MSG_TOO_BIG as error on current path.
      return true;
    }
  }

  if (IsWriteError(result.status)) {
    QUIC_LOG_FIRST_N(ERROR, 10)
        << ENDPOINT << "Failed writing packet " << packet_number << " of "
        << encrypted_length << " bytes from " << self_address().host() << " to "
        << send_to_address << ", with error code " << result.error_code
        << ". long_term_mtu_:" << long_term_mtu_
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

  if (!count_bytes_on_alternative_path_separately_) {
    if (EnforceAntiAmplificationLimit()) {
      // Include bytes sent even if they are not in flight.
      default_path_.bytes_sent_before_address_validation += encrypted_length;
    }
  } else {
    QUIC_CODE_COUNT_N(quic_count_bytes_on_alternative_path_seperately, 2, 5);
    if (IsDefaultPath(default_path_.self_address, send_to_address)) {
      if (EnforceAntiAmplificationLimit()) {
        // Include bytes sent even if they are not in flight.
        default_path_.bytes_sent_before_address_validation += encrypted_length;
      }
    } else {
      MaybeUpdateBytesSentToAlternativeAddress(send_to_address,
                                               encrypted_length);
    }
  }

  // Do not measure rtt of this packet if it's not sent on current path.
  QUIC_DLOG_IF(INFO, !send_on_current_path)
      << ENDPOINT << " Sent packet " << packet->packet_number
      << " on a different path with remote address " << send_to_address
      << " while current path has peer address " << peer_address();
  const bool in_flight = sent_packet_manager_.OnPacketSent(
      packet, packet_send_time, packet->transmission_type,
      IsRetransmittable(*packet), /*measure_rtt=*/send_on_current_path);
  QUIC_BUG_IF(default_enable_5rto_blackhole_detection_ &&
              blackhole_detector_.IsDetectionInProgress() &&
              !sent_packet_manager_.HasInFlightPackets())
      << ENDPOINT
      << "Trying to start blackhole detection without no bytes in flight";

  if (debug_visitor_ != nullptr) {
    if (sent_packet_manager_.unacked_packets().empty()) {
      QUIC_BUG << "Unacked map is empty right after packet is sent";
    } else {
      debug_visitor_->OnPacketSent(
          packet->packet_number, packet->encrypted_length,
          packet->has_crypto_handshake, packet->transmission_type,
          packet->encryption_level,
          sent_packet_manager_.unacked_packets()
              .rbegin()
              ->retransmittable_frames,
          packet->nonretransmittable_frames, packet_send_time);
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

  // The packet number length must be updated after OnPacketSent, because it
  // may change the packet number length in packet.
  packet_creator_.UpdatePacketNumberLength(
      sent_packet_manager_.GetLeastPacketAwaitedByPeer(encryption_level_),
      sent_packet_manager_.EstimateMaxPacketsInFlight(max_packet_length()));

  stats_.bytes_sent += result.bytes_written;
  ++stats_.packets_sent;
  if (packet->transmission_type != NOT_RETRANSMISSION) {
    stats_.bytes_retransmitted += result.bytes_written;
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
    QUIC_BUG
        << "MaybeHandleAeadConfidentialityLimits called on non 1-RTT packet";
    return false;
  }
  if (!lowest_packet_sent_in_current_key_phase_.IsInitialized()) {
    QUIC_BUG << "lowest_packet_sent_in_current_key_phase_ must be initialized "
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
    QUIC_BUG << error_details;
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
      GetQuicFlag(FLAGS_quic_key_update_confidentiality_limit);
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

bool QuicConnection::IsMsgTooBig(const WriteResult& result) {
  return (result.status == WRITE_STATUS_MSG_TOO_BIG) ||
         (IsWriteError(result.status) && result.error_code == QUIC_EMSGSIZE);
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
  switch (error_code) {
    case QUIC_EMSGSIZE:
      CloseConnection(QUIC_PACKET_WRITE_ERROR, error_details,
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      break;
    default:
      // We can't send an error as the socket is presumably borked.
      if (version().HasIetfInvariantHeader()) {
        QUIC_CODE_COUNT(quic_tear_down_local_connection_on_write_error_ietf);
      } else {
        QUIC_CODE_COUNT(
            quic_tear_down_local_connection_on_write_error_non_ietf);
      }
      CloseConnection(QUIC_PACKET_WRITE_ERROR, error_details,
                      ConnectionCloseBehavior::SILENT_CLOSE);
  }
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
    if (version().HasIetfInvariantHeader()) {
      QUIC_CODE_COUNT(
          quic_tear_down_local_connection_on_serialized_packet_ietf);
    } else {
      QUIC_CODE_COUNT(
          quic_tear_down_local_connection_on_serialized_packet_non_ietf);
    }
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
  SendOrQueuePacket(std::move(serialized_packet));
}

void QuicConnection::OnUnrecoverableError(QuicErrorCode error,
                                          const std::string& error_details) {
  // The packet creator or generator encountered an unrecoverable error: tear
  // down local connection state immediately.
  if (version().HasIetfInvariantHeader()) {
    QUIC_CODE_COUNT(
        quic_tear_down_local_connection_on_unrecoverable_error_ietf);
  } else {
    QUIC_CODE_COUNT(
        quic_tear_down_local_connection_on_unrecoverable_error_non_ietf);
  }
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

void QuicConnection::OnHandshakeComplete() {
  sent_packet_manager_.SetHandshakeConfirmed();
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
}

void QuicConnection::SendOrQueuePacket(SerializedPacket packet) {
  // The caller of this function is responsible for checking CanWrite().
  WritePacket(&packet);
}

void QuicConnection::OnPingTimeout() {
  if (retransmission_alarm_->IsSet() ||
      !visitor_->ShouldKeepConnectionAlive()) {
    return;
  }
  SendPingAtLevel(use_encryption_level_context_
                      ? framer().GetEncryptionLevelToSendApplicationData()
                      : encryption_level_);
}

void QuicConnection::SendAck() {
  QUICHE_DCHECK(!SupportsMultiplePacketNumberSpaces());
  QUIC_DVLOG(1) << ENDPOINT << "Sending an ACK proactively";
  QuicFrames frames;
  frames.push_back(GetUpdatedAckFrame());
  if (!no_stop_waiting_frames_) {
    QuicStopWaitingFrame stop_waiting;
    PopulateStopWaitingFrame(&stop_waiting);
    frames.push_back(QuicFrame(stop_waiting));
  }
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

void QuicConnection::OnRetransmissionTimeout() {
#ifndef NDEBUG
  if (sent_packet_manager_.unacked_packets().empty()) {
    QUICHE_DCHECK(sent_packet_manager_.handshake_mode_disabled());
    QUICHE_DCHECK(!IsHandshakeComplete());
  }
#endif
  if (!connected_) {
    return;
  }

  QuicPacketNumber previous_created_packet_number =
      packet_creator_.packet_number();
  const auto retransmission_mode =
      sent_packet_manager_.OnRetransmissionTimeout();
  if (sent_packet_manager_.skip_packet_number_for_pto() &&
      retransmission_mode == QuicSentPacketManager::PTO_MODE &&
      sent_packet_manager_.pending_timer_transmission_count() == 1) {
    // Skip a packet number when a single PTO packet is sent to elicit an
    // immediate ACK.
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
    blackhole_detector_.StopDetection();
  }
  WriteIfNotBlocked();

  // A write failure can result in the connection being closed, don't attempt to
  // write further packets, or to set alarms.
  if (!connected_) {
    return;
  }

  // In the PTO and TLP cases, the SentPacketManager gives the connection the
  // opportunity to send new data before retransmitting.
  if (sent_packet_manager_.pto_enabled()) {
    sent_packet_manager_.MaybeSendProbePackets();
  } else if (sent_packet_manager_.MaybeRetransmitTailLossProbe()) {
    // Send the pending retransmission now that it's been queued.
    WriteIfNotBlocked();
  }

  if (packet_creator_.packet_number() == previous_created_packet_number &&
      (retransmission_mode == QuicSentPacketManager::TLP_MODE ||
       retransmission_mode == QuicSentPacketManager::RTO_MODE ||
       retransmission_mode == QuicSentPacketManager::PTO_MODE) &&
      !visitor_->WillingAndAbleToWrite()) {
    // Send PING if timer fires in TLP/RTO/PTO mode but there is no data to
    // send.
    QUIC_DLOG(INFO) << ENDPOINT
                    << "No packet gets sent when timer fires in mode "
                    << retransmission_mode << ", send PING";
    QUICHE_DCHECK_LT(0u,
                     sent_packet_manager_.pending_timer_transmission_count());
    EncryptionLevel level = encryption_level_;
    PacketNumberSpace packet_number_space = NUM_PACKET_NUMBER_SPACES;
    if (SupportsMultiplePacketNumberSpaces() &&
        sent_packet_manager_
            .GetEarliestPacketSentTimeForPto(&packet_number_space)
            .IsInitialized()) {
      level = QuicUtils::GetEncryptionLevel(packet_number_space);
    }
    SendPingAtLevel(level);
  }
  if (retransmission_mode == QuicSentPacketManager::PTO_MODE) {
    sent_packet_manager_.AdjustPendingTimerTransmissions();
  }
  if (retransmission_mode != QuicSentPacketManager::LOSS_MODE &&
      retransmission_mode != QuicSentPacketManager::HANDSHAKE_MODE) {
    // When timer fires in TLP/RTO/PTO mode, ensure 1) at least one packet is
    // created, or there is data to send and available credit (such that
    // packets will be sent eventually).
    QUIC_BUG_IF(packet_creator_.packet_number() ==
                    previous_created_packet_number &&
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
  QUIC_BUG_IF(!framer_.HasEncrypterOfEncryptionLevel(level))
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
    EncryptionLevel level,
    std::unique_ptr<QuicDecrypter> decrypter,
    bool latch_once_used) {
  framer_.SetAlternativeDecrypter(level, std::move(decrypter), latch_once_used);

  if (!undecryptable_packets_.empty() &&
      !process_undecryptable_packets_alarm_->IsSet()) {
    process_undecryptable_packets_alarm_->Set(clock_->ApproximateNow());
  }
}

void QuicConnection::InstallDecrypter(
    EncryptionLevel level,
    std::unique_ptr<QuicDecrypter> decrypter) {
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
    QUIC_BUG << "key update not allowed";
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
    const QuicEncryptedPacket& packet,
    EncryptionLevel decryption_level) {
  for (const auto& saved_packet : undecryptable_packets_) {
    if (packet.data() == saved_packet.packet->data() &&
        packet.length() == saved_packet.packet->length()) {
      QUIC_DVLOG(1) << ENDPOINT << "Not queueing known undecryptable packet";
      return;
    }
  }
  QUIC_DVLOG(1) << ENDPOINT << "Queueing undecryptable packet.";
  undecryptable_packets_.emplace_back(packet, decryption_level);
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
    if (framer_.ProcessPacket(*undecryptable_packet->packet)) {
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

  // Once forward secure encryption is in use, there will be no
  // new keys installed and hence any undecryptable packets will
  // never be able to be decrypted.
  if (encryption_level_ == ENCRYPTION_FORWARD_SECURE) {
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

void QuicConnection::MaybeProcessCoalescedPackets() {
  bool processed = false;
  while (connected_ && !received_coalesced_packets_.empty()) {
    // Making sure there are no pending frames when processing the next
    // coalesced packet because the queued ack frame may change.
    packet_creator_.FlushCurrentPacket();
    if (!connected_) {
      return;
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
  }
}

void QuicConnection::CloseConnection(
    QuicErrorCode error,
    const std::string& details,
    ConnectionCloseBehavior connection_close_behavior) {
  CloseConnection(error, NO_IETF_QUIC_ERROR, details,
                  connection_close_behavior);
}

void QuicConnection::CloseConnection(
    QuicErrorCode error,
    QuicIetfTransportErrorCodes ietf_error,
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
    QuicErrorCode error,
    QuicIetfTransportErrorCodes ietf_error,
    const std::string& details) {
  // Always use the current path to send CONNECTION_CLOSE.
  QuicPacketCreator::ScopedPeerAddressContext context(&packet_creator_,
                                                      peer_address());
  if (!SupportsMultiplePacketNumberSpaces()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Sending connection close packet.";
    if (!use_encryption_level_context_) {
      SetDefaultEncryptionLevel(GetConnectionCloseEncryptionLevel());
    }
    ScopedEncryptionLevelContext context(
        use_encryption_level_context_ ? this : nullptr,
        GetConnectionCloseEncryptionLevel());
    if (version().CanSendCoalescedPackets()) {
      coalesced_packet_.Clear();
    }
    ClearQueuedPackets();
    // If there was a packet write error, write the smallest close possible.
    ScopedPacketFlusher flusher(this);
    // Always bundle an ACK with connection close for debugging purpose.
    bool send_ack = error != QUIC_PACKET_WRITE_ERROR &&
                    !uber_received_packet_manager_.IsAckFrameEmpty(
                        QuicUtils::GetPacketNumberSpace(encryption_level_));
    if (GetQuicReloadableFlag(quic_single_ack_in_packet2)) {
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_single_ack_in_packet2, 1, 2);
      send_ack = !packet_creator_.has_ack() && send_ack;
    }
    if (send_ack) {
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
  const EncryptionLevel current_encryption_level = encryption_level_;
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
    if (!use_encryption_level_context_) {
      SetDefaultEncryptionLevel(level);
    }
    ScopedEncryptionLevelContext context(
        use_encryption_level_context_ ? this : nullptr, level);
    // Bundle an ACK of the corresponding packet number space for debugging
    // purpose.
    bool send_ack = error != QUIC_PACKET_WRITE_ERROR &&
                    !uber_received_packet_manager_.IsAckFrameEmpty(
                        QuicUtils::GetPacketNumberSpace(encryption_level_));
    if (GetQuicReloadableFlag(quic_single_ack_in_packet2)) {
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_single_ack_in_packet2, 2, 2);
      send_ack = !packet_creator_.has_ack() && send_ack;
    }
    if (send_ack) {
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
  if (!use_encryption_level_context_) {
    SetDefaultEncryptionLevel(current_encryption_level);
  }
}

void QuicConnection::TearDownLocalConnectionState(
    QuicErrorCode error,
    QuicIetfTransportErrorCodes ietf_error,
    const std::string& error_details,
    ConnectionCloseSource source) {
  QuicConnectionCloseFrame frame(transport_version(), error, ietf_error,
                                 error_details,
                                 framer_.current_received_frame_type());
  return TearDownLocalConnectionState(frame, source);
}

void QuicConnection::TearDownLocalConnectionState(
    const QuicConnectionCloseFrame& frame,
    ConnectionCloseSource source) {
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
  if (use_path_validator_) {
    CancelPathValidation();
  }
}

void QuicConnection::CancelAllAlarms() {
  QUIC_DVLOG(1) << "Cancelling all QuicConnection alarms.";

  ack_alarm_->Cancel();
  ping_alarm_->Cancel();
  retransmission_alarm_->Cancel();
  send_alarm_->Cancel();
  mtu_discovery_alarm_->Cancel();
  process_undecryptable_packets_alarm_->Cancel();
  discard_previous_one_rtt_keys_alarm_->Cancel();
  discard_zero_rtt_decryption_keys_alarm_->Cancel();
  blackhole_detector_.StopDetection();
  idle_network_detector_.StopDetection();
}

QuicByteCount QuicConnection::max_packet_length() const {
  return packet_creator_.max_packet_length();
}

void QuicConnection::SetMaxPacketLength(QuicByteCount length) {
  long_term_mtu_ = length;
  MaybeUpdatePacketCreatorMaxPacketLengthAndPadding();
}

bool QuicConnection::HasQueuedData() const {
  return packet_creator_.HasPendingFrames() || !buffered_packets_.empty();
}

void QuicConnection::SetNetworkTimeouts(QuicTime::Delta handshake_timeout,
                                        QuicTime::Delta idle_timeout) {
  QUIC_BUG_IF(idle_timeout > handshake_timeout)
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
  if (perspective_ == Perspective::IS_SERVER &&
      initial_retransmittable_on_wire_timeout_.IsInfinite()) {
    // The PING alarm exists to support two features:
    // 1) clients send PINGs every 15s to prevent NAT timeouts,
    // 2) both clients and servers can send retransmittable on the wire PINGs
    // (ROWP) while ShouldKeepConnectionAlive is true and there is no packets in
    // flight.
    return;
  }
  if (!visitor_->ShouldKeepConnectionAlive()) {
    ping_alarm_->Cancel();
    // Don't send a ping unless the application (ie: HTTP/3) says to, usually
    // because it is expecting a response from the server.
    return;
  }
  if (initial_retransmittable_on_wire_timeout_.IsInfinite() ||
      sent_packet_manager_.HasInFlightPackets() ||
      retransmittable_on_wire_ping_count_ >
          GetQuicFlag(FLAGS_quic_max_retransmittable_on_wire_ping_count)) {
    if (perspective_ == Perspective::IS_CLIENT) {
      // Clients send 15s PINGs to avoid NATs from timing out.
      ping_alarm_->Update(clock_->ApproximateNow() + ping_timeout_,
                          QuicTime::Delta::FromSeconds(1));
    } else {
      // Servers do not send 15s PINGs.
      ping_alarm_->Cancel();
    }
    return;
  }
  QUICHE_DCHECK_LT(initial_retransmittable_on_wire_timeout_, ping_timeout_);
  QuicTime::Delta retransmittable_on_wire_timeout =
      initial_retransmittable_on_wire_timeout_;
  int max_aggressive_retransmittable_on_wire_ping_count =
      GetQuicFlag(FLAGS_quic_max_aggressive_retransmittable_on_wire_ping_count);
  QUICHE_DCHECK_LE(0, max_aggressive_retransmittable_on_wire_ping_count);
  if (consecutive_retransmittable_on_wire_ping_count_ >
      max_aggressive_retransmittable_on_wire_ping_count) {
    // Exponentially back off the timeout if the number of consecutive
    // retransmittable on wire pings has exceeds the allowance.
    int shift = consecutive_retransmittable_on_wire_ping_count_ -
                max_aggressive_retransmittable_on_wire_ping_count;
    retransmittable_on_wire_timeout =
        initial_retransmittable_on_wire_timeout_ * (1 << shift);
  }
  // If it's already set to an earlier time, then don't update it.
  if (ping_alarm_->IsSet() &&
      ping_alarm_->deadline() <
          clock_->ApproximateNow() + retransmittable_on_wire_timeout) {
    return;
  }

  if (retransmittable_on_wire_timeout < ping_timeout_) {
    // Use a shorter timeout if there are open streams, but nothing on the wire.
    ping_alarm_->Update(
        clock_->ApproximateNow() + retransmittable_on_wire_timeout,
        kAlarmGranularity);
    if (max_aggressive_retransmittable_on_wire_ping_count != 0) {
      consecutive_retransmittable_on_wire_ping_count_++;
    }
    retransmittable_on_wire_ping_count_++;
    return;
  }

  ping_alarm_->Update(clock_->ApproximateNow() + ping_timeout_,
                      kAlarmGranularity);
}

void QuicConnection::SetRetransmissionAlarm() {
  if (!connected_) {
    if (retransmission_alarm_->IsSet()) {
      QUIC_BUG << ENDPOINT << "Retransmission alarm is set while disconnected";
      retransmission_alarm_->Cancel();
    }
    return;
  }
  if (packet_creator_.PacketFlusherAttached()) {
    pending_retransmission_alarm_ = true;
    return;
  }
  if (LimitedByAmplificationFactor()) {
    // Do not set retransmission timer if connection is anti-amplification limit
    // throttled. Otherwise, nothing can be sent when timer fires.
    retransmission_alarm_->Cancel();
    return;
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
    connection_->packet_creator_.Flush();
    if (connection_->version().CanSendCoalescedPackets()) {
      connection_->MaybeCoalescePacketOfHigherSpace();
      connection_->FlushCoalescedPacket();
    }
    connection_->FlushPackets();
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
    QuicConnection* connection,
    EncryptionLevel encryption_level)
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
    const SerializedPacket& packet,
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address)
    : encrypted_buffer(CopyBuffer(packet), packet.encrypted_length),
      self_address(self_address),
      peer_address(peer_address) {}

QuicConnection::BufferedPacket::BufferedPacket(
    char* encrypted_buffer,
    QuicPacketLength encrypted_length,
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address)
    : encrypted_buffer(CopyBuffer(encrypted_buffer, encrypted_length),
                       encrypted_length),
      self_address(self_address),
      peer_address(peer_address) {}

QuicConnection::BufferedPacket::~BufferedPacket() {
  delete[] encrypted_buffer.data();
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
    QUIC_BUG << "Attempted to use a connection without a valid peer address";
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
    QuicPacketWriter* probing_writer,
    const QuicSocketAddress& peer_address) {
  return SendGenericPathProbePacket(probing_writer, peer_address,
                                    /* is_response= */ false);
}

void QuicConnection::SendConnectivityProbingResponsePacket(
    const QuicSocketAddress& peer_address) {
  SendGenericPathProbePacket(nullptr, peer_address,
                             /* is_response= */ true);
}

bool QuicConnection::SendGenericPathProbePacket(
    QuicPacketWriter* probing_writer,
    const QuicSocketAddress& peer_address,
    bool is_response) {
  QUICHE_DCHECK(peer_address.IsInitialized());
  if (!connected_) {
    QUIC_BUG << "Not sending connectivity probing packet as connection is "
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
                  << server_connection_id_;

  std::unique_ptr<SerializedPacket> probing_packet;
  if (!version().HasIetfQuicFrames()) {
    // Non-IETF QUIC, generate a padded ping regardless of whether this is a
    // request or a response.
    probing_packet = packet_creator_.SerializeConnectivityProbingPacket();
  } else if (is_response) {
    QUICHE_DCHECK(!send_path_response_);
    // IETF QUIC path response.
    // Respond to path probe request using IETF QUIC PATH_RESPONSE frame.
    probing_packet =
        packet_creator_.SerializePathResponseConnectivityProbingPacket(
            received_path_challenge_payloads_,
            /*is_padded=*/false);
    received_path_challenge_payloads_.clear();
  } else {
    // IETF QUIC path challenge.
    // Send a path probe request using IETF QUIC PATH_CHALLENGE frame.
    transmitted_connectivity_probe_payload_ =
        std::make_unique<QuicPathFrameBuffer>();
    random_generator_->RandBytes(transmitted_connectivity_probe_payload_.get(),
                                 sizeof(QuicPathFrameBuffer));
    probing_packet =
        packet_creator_.SerializePathChallengeConnectivityProbingPacket(
            *transmitted_connectivity_probe_payload_);
    if (!probing_packet) {
      transmitted_connectivity_probe_payload_ = nullptr;
    }
  }
  QUICHE_DCHECK_EQ(IsRetransmittable(*probing_packet), NO_RETRANSMITTABLE_DATA);
  return WritePacketUsingWriter(std::move(probing_packet), probing_writer,
                                self_address(), peer_address,
                                /*measure_rtt=*/true);
}

bool QuicConnection::WritePacketUsingWriter(
    std::unique_ptr<SerializedPacket> packet,
    QuicPacketWriter* writer,
    const QuicSocketAddress& self_address,
    const QuicSocketAddress& peer_address,
    bool measure_rtt) {
  const QuicTime packet_send_time = clock_->Now();
  QUIC_DVLOG(2) << ENDPOINT
                << "Sending path probe packet for server connection ID "
                << server_connection_id_ << std::endl
                << quiche::QuicheTextUtils::HexDump(absl::string_view(
                       packet->encrypted_buffer, packet->encrypted_length));
  WriteResult result = writer->WritePacket(
      packet->encrypted_buffer, packet->encrypted_length, self_address.host(),
      peer_address, per_packet_options_);

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
  sent_packet_manager_.OnPacketSent(packet.get(), packet_send_time,
                                    packet->transmission_type,
                                    NO_RETRANSMITTABLE_DATA, measure_rtt);

  if (debug_visitor_ != nullptr) {
    if (sent_packet_manager_.unacked_packets().empty()) {
      QUIC_BUG << "Unacked map is empty right after packet is sent";
    } else {
      debug_visitor_->OnPacketSent(
          packet->packet_number, packet->encrypted_length,
          packet->has_crypto_handshake, packet->transmission_type,
          packet->encryption_level,
          sent_packet_manager_.unacked_packets()
              .rbegin()
              ->retransmittable_frames,
          packet->nonretransmittable_frames, packet_send_time);
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

void QuicConnection::OnEffectivePeerMigrationValidated() {
  if (active_effective_peer_migration_type_ == NO_CHANGE) {
    QUIC_BUG << "No migration underway.";
    return;
  }
  highest_packet_sent_before_effective_peer_migration_.Clear();
  const bool send_address_token =
      active_effective_peer_migration_type_ != PORT_CHANGE;
  active_effective_peer_migration_type_ = NO_CHANGE;
  ++stats_.num_validated_peer_migration;
  if (!validate_client_addresses_) {
    return;
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
  if (!validate_client_addresses_) {
    if (type == NO_CHANGE) {
      QUIC_BUG << "EffectivePeerMigration started without address change.";
      return;
    }
    QUIC_DLOG(INFO) << ENDPOINT << "Effective peer's ip:port changed from "
                    << default_path_.peer_address.ToString() << " to "
                    << GetEffectivePeerAddressFromCurrentPacket().ToString()
                    << ", address change type is " << type
                    << ", migrating connection.";

    highest_packet_sent_before_effective_peer_migration_ =
        sent_packet_manager_.GetLargestSentPacket();
    default_path_.peer_address = GetEffectivePeerAddressFromCurrentPacket();
    active_effective_peer_migration_type_ = type;

    OnConnectionMigration();
    return;
  }

  if (type == NO_CHANGE) {
    UpdatePeerAddress(last_packet_source_address_);
    QUIC_BUG << "EffectivePeerMigration started without address change.";
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
    previous_default_path.send_algorithm =
        sent_packet_manager_.OnConnectionMigration(
            /*reset_send_algorithm=*/true);
    // OnConnectionMigration() might have marked in-flight packets to be
    // retransmitted if there is any.
    QUICHE_DCHECK(!sent_packet_manager_.HasInFlightPackets());
    // Stop detections in quiecense.
    blackhole_detector_.StopDetection();

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
  UpdatePeerAddress(last_packet_source_address_);
  // Update the default path.
  if (IsAlternativePath(last_packet_destination_address_,
                        current_effective_peer_address)) {
    default_path_ = std::move(alternative_path_);
  } else {
    default_path_ = PathState(last_packet_destination_address_,
                              current_effective_peer_address);
    // The path is considered validated if its peer IP address matches any
    // validated path's peer IP address.
    default_path_.validated =
        (alternative_path_.peer_address.host() ==
             current_effective_peer_address.host() &&
         alternative_path_.validated) ||
        (previous_default_path.validated && type == PORT_CHANGE);
  }
  if (!current_incoming_packet_received_bytes_counted_) {
    // Increment bytes counting on the new default path.
    default_path_.bytes_received_before_address_validation += last_size_;
    current_incoming_packet_received_bytes_counted_ = true;
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
    OnEffectivePeerMigrationValidated();
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
                     this, previous_direct_peer_address));
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
      !validate_client_addresses_) {
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
  return absl::string_view(current_packet_data_, last_size_);
}

bool QuicConnection::MaybeConsiderAsMemoryCorruption(
    const QuicStreamFrame& frame) {
  if (QuicUtils::IsCryptoStreamId(transport_version(), frame.stream_id) ||
      last_decrypted_packet_level_ != ENCRYPTION_INITIAL) {
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

void QuicConnection::MaybeSendProbingRetransmissions() {
  QUICHE_DCHECK(fill_up_link_during_probing_);

  // Don't send probing retransmissions until the handshake has completed.
  if (!IsHandshakeComplete() ||
      sent_packet_manager().HasUnackedCryptoPackets()) {
    return;
  }

  if (probing_retransmission_pending_) {
    QUIC_BUG << "MaybeSendProbingRetransmissions is called while another call "
                "to it is already in progress";
    return;
  }

  probing_retransmission_pending_ = true;
  SendProbingRetransmissions();
  probing_retransmission_pending_ = false;
}

void QuicConnection::CheckIfApplicationLimited() {
  if (!connected_ || probing_retransmission_pending_) {
    return;
  }

  bool application_limited =
      buffered_packets_.empty() && !visitor_->WillingAndAbleToWrite();

  if (!application_limited) {
    return;
  }

  if (fill_up_link_during_probing_) {
    MaybeSendProbingRetransmissions();
    if (!CanWrite(HAS_RETRANSMITTABLE_DATA)) {
      return;
    }
  }

  sent_packet_manager_.OnApplicationLimited();
}

bool QuicConnection::UpdatePacketContent(QuicFrameType type) {
  if (update_packet_content_returns_connected_) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_update_packet_content_returns_connected);
  }
  most_recent_frame_type_ = type;
  if (version().HasIetfQuicFrames()) {
    if (!QuicUtils::IsProbingFrame(type)) {
      MaybeStartIetfPeerMigration();
      return !update_packet_content_returns_connected_ || connected_;
    }
    QuicSocketAddress current_effective_peer_address =
        GetEffectivePeerAddressFromCurrentPacket();
    if (!count_bytes_on_alternative_path_separately_ ||
        IsDefaultPath(last_packet_destination_address_,
                      last_packet_source_address_)) {
      return !update_packet_content_returns_connected_ || connected_;
    }
    QUIC_CODE_COUNT_N(quic_count_bytes_on_alternative_path_seperately, 3, 5);
    if (type == PATH_CHALLENGE_FRAME &&
        !IsAlternativePath(last_packet_destination_address_,
                           current_effective_peer_address)) {
      QUIC_DVLOG(1)
          << "The peer is probing a new path with effective peer address "
          << current_effective_peer_address << ",  self address "
          << last_packet_destination_address_;
      if (!validate_client_addresses_) {
        alternative_path_ = PathState(last_packet_destination_address_,
                                      current_effective_peer_address);
      } else if (!default_path_.validated) {
        // Skip reverse path validation because either handshake hasn't
        // completed or the connection is validating the default path. Using
        // PATH_CHALLENGE to validate alternative client address before
        // handshake gets comfirmed is meaningless because anyone can respond to
        // it. If the connection is validating the default path, this
        // alternative path is currently the only validated path which shouldn't
        // be overridden.
        QUIC_DVLOG(1) << "The connection hasn't finished handshake or is "
                         "validating a recent peer address change.";
        QUIC_BUG_IF(IsHandshakeConfirmed() && !alternative_path_.validated)
            << "No validated peer address to send after handshake comfirmed.";
      } else if (!IsReceivedPeerAddressValidated()) {
        // Only override alternative path state upon receiving a PATH_CHALLENGE
        // from an unvalidated peer address, and the connection isn't validating
        // a recent peer migration.
        alternative_path_ = PathState(last_packet_destination_address_,
                                      current_effective_peer_address);
        // Conditions to proactively validate peer address:
        // The perspective is server
        // The PATH_CHALLENGE is received on an unvalidated alternative path.
        // The connection isn't validating migrated peer address, which is of
        // higher prority.
        QUIC_DVLOG(1) << "Proactively validate the effective peer address "
                      << current_effective_peer_address;
        ValidatePath(
            std::make_unique<ReversePathValidationContext>(
                default_path_.self_address, current_effective_peer_address,
                current_effective_peer_address, this),
            std::make_unique<ReversePathValidationResultDelegate>(
                this, peer_address()));
      }
    }
    MaybeUpdateBytesReceivedFromAlternativeAddress(last_size_);
    return !update_packet_content_returns_connected_ || connected_;
  }
  // Packet content is tracked to identify connectivity probe in non-IETF
  // version, where a connectivity probe is defined as
  // - a padded PING packet with peer address change received by server,
  // - a padded PING packet on new path received by client.

  if (current_packet_content_ == NOT_PADDED_PING) {
    // We have already learned the current packet is not a connectivity
    // probing packet. Peer migration should have already been started earlier
    // if needed.
    return !update_packet_content_returns_connected_ || connected_;
  }

  if (type == PING_FRAME) {
    if (current_packet_content_ == NO_FRAMES_RECEIVED) {
      current_packet_content_ = FIRST_FRAME_IS_PING;
      return !update_packet_content_returns_connected_ || connected_;
    }
  }

  // In Google QUIC, we look for a packet with just a PING and PADDING.
  // If the condition is met, mark things as connectivity-probing, causing
  // later processing to generate the correct response.
  if (type == PADDING_FRAME && current_packet_content_ == FIRST_FRAME_IS_PING) {
    current_packet_content_ = SECOND_FRAME_IS_PADDING;
    if (perspective_ == Perspective::IS_SERVER) {
      is_current_packet_connectivity_probing_ =
          current_effective_peer_migration_type_ != NO_CHANGE;
      QUIC_DLOG_IF(INFO, is_current_packet_connectivity_probing_)
          << ENDPOINT
          << "Detected connectivity probing packet. "
             "current_effective_peer_migration_type_:"
          << current_effective_peer_migration_type_;
    } else {
      is_current_packet_connectivity_probing_ =
          (last_packet_source_address_ != peer_address()) ||
          (last_packet_destination_address_ != default_path_.self_address);
      QUIC_DLOG_IF(INFO, is_current_packet_connectivity_probing_)
          << ENDPOINT
          << "Detected connectivity probing packet. "
             "last_packet_source_address_:"
          << last_packet_source_address_ << ", peer_address_:" << peer_address()
          << ", last_packet_destination_address_:"
          << last_packet_destination_address_
          << ", default path self_address :" << default_path_.self_address;
    }
    return !update_packet_content_returns_connected_ || connected_;
  }

  current_packet_content_ = NOT_PADDED_PING;
  if (GetLargestReceivedPacket().IsInitialized() &&
      last_header_.packet_number == GetLargestReceivedPacket()) {
    UpdatePeerAddress(last_packet_source_address_);
    if (current_effective_peer_migration_type_ != NO_CHANGE) {
      // Start effective peer migration immediately when the current packet is
      // confirmed not a connectivity probing packet.
      StartEffectivePeerMigration(current_effective_peer_migration_type_);
    }
  }
  current_effective_peer_migration_type_ = NO_CHANGE;
  return !update_packet_content_returns_connected_ || connected_;
}

void QuicConnection::MaybeStartIetfPeerMigration() {
  QUICHE_DCHECK(version().HasIetfQuicFrames());
  if (!start_peer_migration_earlier_) {
    return;
  }
  QUIC_CODE_COUNT(quic_start_peer_migration_earlier);
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
      last_header_.packet_number == GetLargestReceivedPacket()) {
    if (current_effective_peer_migration_type_ != NO_CHANGE) {
      // Start effective peer migration when the current packet contains a
      // non-probing frame.
      // TODO(fayang): When multiple packet number spaces is supported, only
      // start peer migration for the application data.
      if (!validate_client_addresses_) {
        UpdatePeerAddress(last_packet_source_address_);
      }
      StartEffectivePeerMigration(current_effective_peer_migration_type_);
    } else {
      UpdatePeerAddress(last_packet_source_address_);
    }
  }
  current_effective_peer_migration_type_ = NO_CHANGE;
}

void QuicConnection::PostProcessAfterAckFrame(bool send_stop_waiting,
                                              bool acked_new_packet) {
  if (no_stop_waiting_frames_ && !packet_creator_.has_ack()) {
    uber_received_packet_manager_.DontWaitForPacketsBefore(
        last_decrypted_packet_level_,
        SupportsMultiplePacketNumberSpaces()
            ? sent_packet_manager_.GetLargestPacketPeerKnowsIsAcked(
                  last_decrypted_packet_level_)
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
    blackhole_detector_.StopDetection();
  }

  if (send_stop_waiting) {
    ++stop_waiting_count_;
  } else {
    stop_waiting_count_ = 0;
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
      std::min(
          QuicTime::Delta::FromMilliseconds(
              GetQuicFlag(FLAGS_quic_max_pace_time_into_future_ms)),
          sent_packet_manager_.GetRttStats()->SmoothedOrInitialRtt() *
              GetQuicFlag(FLAGS_quic_pace_time_into_future_srtt_fraction)));
  QUIC_DVLOG(3) << "Updated max release time delay from "
                << prior_max_release_time << " to "
                << release_time_into_future_;
}

void QuicConnection::ResetAckStates() {
  ack_alarm_->Cancel();
  stop_waiting_count_ = 0;
  uber_received_packet_manager_.ResetAckStates(encryption_level_);
}

MessageStatus QuicConnection::SendMessage(QuicMessageId message_id,
                                          QuicMemSliceSpan message,
                                          bool flush) {
  if (!VersionSupportsMessageFrames(transport_version())) {
    QUIC_BUG << "MESSAGE frame is not supported for version "
             << transport_version();
    return MESSAGE_STATUS_UNSUPPORTED;
  }
  if (message.total_length() > GetCurrentLargestMessagePayload()) {
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
    return framer_.GetDecrypter(last_decrypted_packet_level_)->cipher_id();
  }
  return framer_.decrypter()->cipher_id();
}

EncryptionLevel QuicConnection::GetConnectionCloseEncryptionLevel() const {
  if (perspective_ == Perspective::IS_CLIENT) {
    return encryption_level_;
  }
  if (IsHandshakeComplete()) {
    // A forward secure packet has been received.
    QUIC_BUG_IF(encryption_level_ != ENCRYPTION_FORWARD_SECURE)
        << ENDPOINT << "Unexpected connection close encryption level "
        << encryption_level_;
    return ENCRYPTION_FORWARD_SECURE;
  }
  if (framer_.HasEncrypterOfEncryptionLevel(ENCRYPTION_ZERO_RTT)) {
    if (encryption_level_ != ENCRYPTION_ZERO_RTT) {
      if (version().HasIetfInvariantHeader()) {
        QUIC_CODE_COUNT(quic_wrong_encryption_level_connection_close_ietf);
      } else {
        QUIC_CODE_COUNT(quic_wrong_encryption_level_connection_close);
      }
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
    QUIC_BUG << ENDPOINT
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
  QUIC_BUG_IF(!earliest_ack_timeout.IsInitialized());
  MaybeBundleCryptoDataWithAcks();
  earliest_ack_timeout = uber_received_packet_manager_.GetEarliestAckTimeout();
  if (!earliest_ack_timeout.IsInitialized()) {
    return;
  }
  // Latches current encryption level.
  const EncryptionLevel current_encryption_level = encryption_level_;
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
    // Switch to the appropriate encryption level.
    if (!use_encryption_level_context_) {
      SetDefaultEncryptionLevel(
          QuicUtils::GetEncryptionLevel(static_cast<PacketNumberSpace>(i)));
    }
    ScopedEncryptionLevelContext context(
        use_encryption_level_context_ ? this : nullptr,
        QuicUtils::GetEncryptionLevel(static_cast<PacketNumberSpace>(i)));
    QuicFrames frames;
    frames.push_back(uber_received_packet_manager_.GetUpdatedAckFrame(
        static_cast<PacketNumberSpace>(i), clock_->ApproximateNow()));
    const bool flushed = packet_creator_.FlushAckFrame(frames);
    if (!flushed) {
      // Connection is write blocked.
      QUIC_BUG_IF(!writer_->IsWriteBlocked() && !LimitedByAmplificationFactor())
          << "Writer not blocked and not throttled by amplification factor, "
             "but ACK not flushed for packet space:"
          << i;
      break;
    }
    ResetAckStates();
  }
  if (!use_encryption_level_context_) {
    // Restores encryption level.
    SetDefaultEncryptionLevel(current_encryption_level);
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
      (sent_packet_manager_.GetConsecutiveRtoCount() > 0 ||
       sent_packet_manager_.GetConsecutivePtoCount() > 0)) {
    // Bundle a retransmittable frame with an ACK if the PTO or RTO has fired
    // in order to recover more quickly in cases of temporary network outage.
    return true;
  }
  return false;
}

void QuicConnection::MaybeCoalescePacketOfHigherSpace() {
  if (!connected() || !packet_creator_.HasSoftMaxPacketLength() ||
      fill_coalesced_packet_) {
    // Make sure MaybeCoalescePacketOfHigherSpace is not re-entrant.
    return;
  }
  // INITIAL or HANDSHAKE retransmission could cause peer to derive new
  // keys, such that the buffered undecryptable packets may be processed.
  // This endpoint would derive an inflated RTT sample (which includes the PTO
  // timeout) when receiving ACKs of those undecryptable packets. To mitigate
  // this, tries to coalesce a packet of higher encryption level.
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
    QUIC_BUG_IF(coalesced_packet_.length() > 0);
    return true;
  }
  if (coalesced_packet_.ContainsPacketOfEncryptionLevel(ENCRYPTION_INITIAL) &&
      !framer_.HasEncrypterOfEncryptionLevel(ENCRYPTION_INITIAL)) {
    // Initial packet will be re-serialized. Neuter it in case initial key has
    // been dropped.
    QUIC_BUG << ENDPOINT
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
    return false;
  }
  QUIC_DVLOG(1) << ENDPOINT << "Sending coalesced packet "
                << coalesced_packet_.ToString(length);

  if (!buffered_packets_.empty() || HandleWriteBlocked()) {
    QUIC_DVLOG(1) << ENDPOINT
                  << "Buffering coalesced packet of len: " << length;
    buffered_packets_.emplace_back(
        buffer, static_cast<QuicPacketLength>(length),
        coalesced_packet_.self_address(), coalesced_packet_.peer_address());
    if (debug_visitor_ != nullptr) {
      debug_visitor_->OnCoalescedPacketSent(coalesced_packet_, length);
    }
    return true;
  }

  WriteResult result = writer_->WritePacket(
      buffer, length, coalesced_packet_.self_address().host(),
      coalesced_packet_.peer_address(), per_packet_options_);
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
          coalesced_packet_.self_address(), coalesced_packet_.peer_address());
    }
  }
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnCoalescedPacketSent(coalesced_packet_, length);
  }
  // Account for added padding.
  if (length > coalesced_packet_.length()) {
    size_t padding_size = length - coalesced_packet_.length();
    if (!count_bytes_on_alternative_path_separately_) {
      if (EnforceAntiAmplificationLimit()) {
        default_path_.bytes_sent_before_address_validation += padding_size;
      }
    } else {
      QUIC_CODE_COUNT_N(quic_count_bytes_on_alternative_path_seperately, 5, 5);
      if (IsDefaultPath(coalesced_packet_.self_address(),
                        coalesced_packet_.peer_address())) {
        if (EnforceAntiAmplificationLimit()) {
          // Include bytes sent even if they are not in flight.
          default_path_.bytes_sent_before_address_validation += padding_size;
        }
      } else {
        MaybeUpdateBytesSentToAlternativeAddress(
            coalesced_packet_.peer_address(), padding_size);
      }
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
        last_decrypted_packet_level_)] = new_value;
  } else {
    largest_seen_packet_with_ack_ = new_value;
  }
}

void QuicConnection::OnForwardProgressMade() {
  if (is_path_degrading_) {
    visitor_->OnForwardProgressMadeAfterPathDegrading();
    is_path_degrading_ = false;
  }
  if (sent_packet_manager_.HasInFlightPackets()) {
    // Restart detections if forward progress has been made.
    blackhole_detector_.RestartDetection(GetPathDegradingDeadline(),
                                         GetNetworkBlackholeDeadline(),
                                         GetPathMtuReductionDeadline());
  } else {
    // Stop detections in quiecense.
    blackhole_detector_.StopDetection();
  }
  QUIC_BUG_IF(default_enable_5rto_blackhole_detection_ &&
              blackhole_detector_.IsDetectionInProgress() &&
              !sent_packet_manager_.HasInFlightPackets())
      << ENDPOINT
      << "Trying to start blackhole detection without no bytes in flight";
}

QuicPacketNumber QuicConnection::GetLargestReceivedPacketWithAck() const {
  if (SupportsMultiplePacketNumberSpaces()) {
    return largest_seen_packets_with_ack_[QuicUtils::GetPacketNumberSpace(
        last_decrypted_packet_level_)];
  }
  return largest_seen_packet_with_ack_;
}

QuicPacketNumber QuicConnection::GetLargestAckedPacket() const {
  if (SupportsMultiplePacketNumberSpaces()) {
    return sent_packet_manager_.GetLargestAckedPacket(
        last_decrypted_packet_level_);
  }
  return sent_packet_manager_.GetLargestObserved();
}

QuicPacketNumber QuicConnection::GetLargestReceivedPacket() const {
  return uber_received_packet_manager_.GetLargestObserved(
      last_decrypted_packet_level_);
}

bool QuicConnection::EnforceAntiAmplificationLimit() const {
  return version().SupportsAntiAmplificationLimit() &&
         perspective_ == Perspective::IS_SERVER && !default_path_.validated;
}

// TODO(danzh) Pass in path object or its reference of some sort to use this
// method to check anti-amplification limit on non-default path.
bool QuicConnection::LimitedByAmplificationFactor() const {
  return EnforceAntiAmplificationLimit() &&
         default_path_.bytes_sent_before_address_validation >=
             anti_amplification_factor_ *
                 default_path_.bytes_received_before_address_validation;
}

SerializedPacketFate QuicConnection::GetSerializedPacketFate(
    bool is_mtu_discovery,
    EncryptionLevel encryption_level) {
  if (ShouldDiscardPacket(encryption_level)) {
    return DISCARD;
  }
  if (legacy_version_encapsulation_in_progress_) {
    QUICHE_DCHECK(!is_mtu_discovery);
    return LEGACY_VERSION_ENCAPSULATE;
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
        QuicUtils::GetPacketNumberSpace(last_decrypted_packet_level_));
  }
  return uber_received_packet_manager_.ack_frame();
}

void QuicConnection::set_client_connection_id(
    QuicConnectionId client_connection_id) {
  if (!version().SupportsClientConnectionIds()) {
    QUIC_BUG_IF(!client_connection_id.IsEmpty())
        << ENDPOINT << "Attempted to use client connection ID "
        << client_connection_id << " with unsupported version " << version();
    return;
  }
  client_connection_id_ = client_connection_id;
  client_connection_id_is_set_ = true;
  QUIC_DLOG(INFO) << ENDPOINT << "setting client connection ID to "
                  << client_connection_id_
                  << " for connection with server connection ID "
                  << server_connection_id_;
  packet_creator_.SetClientConnectionId(client_connection_id_);
  framer_.SetExpectedClientConnectionIdLength(client_connection_id_.length());
}

void QuicConnection::OnPathDegradingDetected() {
  is_path_degrading_ = true;
  visitor_->OnPathDegrading();
}

void QuicConnection::OnBlackholeDetected() {
  if (default_enable_5rto_blackhole_detection_ &&
      !sent_packet_manager_.HasInFlightPackets()) {
    QUIC_BUG << ENDPOINT
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
  QUIC_DVLOG(1) << ENDPOINT << error_details;
  const bool has_consecutive_pto =
      sent_packet_manager_.GetConsecutiveTlpCount() > 0 ||
      sent_packet_manager_.GetConsecutiveRtoCount() > 0 ||
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

void QuicConnection::MaybeUpdateAckTimeout() {
  if (should_last_packet_instigate_acks_) {
    return;
  }
  should_last_packet_instigate_acks_ = true;
  uber_received_packet_manager_.MaybeUpdateAckTimeout(
      /*should_last_packet_instigate_acks=*/true, last_decrypted_packet_level_,
      last_header_.packet_number, clock_->ApproximateNow(),
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
  return clock_->ApproximateNow() +
         sent_packet_manager_.GetNetworkBlackholeDelay(
             num_rtos_for_blackhole_detection_);
}

bool QuicConnection::ShouldDetectBlackhole() const {
  if (!connected_ || blackhole_detection_disabled_) {
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
    const QuicSocketAddress& /*effective_peer_address*/,
    QuicPacketWriter* writer) {
  if (writer == writer_) {
    {
      // It's on current path, add the PATH_CHALLENGE the same way as other
      // frames.
      QuicPacketCreator::ScopedPeerAddressContext context(&packet_creator_,
                                                          peer_address);
      // This may cause connection to be closed.
      packet_creator_.AddPathChallengeFrame(data_buffer);
    }
    // Return outside of the scope so that the flush result can be reflected.
    return connected_;
  }
  std::unique_ptr<SerializedPacket> probing_packet =
      packet_creator_.SerializePathChallengeConnectivityProbingPacket(
          data_buffer);
  QUICHE_DCHECK_EQ(IsRetransmittable(*probing_packet), NO_RETRANSMITTABLE_DATA);
  QUICHE_DCHECK_EQ(self_address, alternative_path_.self_address);
  WritePacketUsingWriter(std::move(probing_packet), writer, self_address,
                         peer_address, /*measure_rtt=*/false);
  return true;
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
    std::unique_ptr<QuicPathValidator::ResultDelegate> result_delegate) {
  QUICHE_DCHECK(use_path_validator_);
  if (perspective_ == Perspective::IS_CLIENT &&
      !IsDefaultPath(context->self_address(), context->peer_address())) {
    alternative_path_ =
        PathState(context->self_address(), context->peer_address());
  }
  if (path_validator_.HasPendingPathValidation()) {
    // Cancel and fail any earlier validation.
    path_validator_.CancelPathValidation();
  }
  path_validator_.StartPathValidation(std::move(context),
                                      std::move(result_delegate));
}

bool QuicConnection::SendPathResponse(const QuicPathFrameBuffer& data_buffer,
                                      QuicSocketAddress peer_address_to_send) {
  // Send PATH_RESPONSE using the provided peer address. If the creator has been
  // using a different peer address, it will flush before and after serializing
  // the current PATH_RESPONSE.
  QuicPacketCreator::ScopedPeerAddressContext context(&packet_creator_,
                                                      peer_address_to_send);
  QUIC_DVLOG(1) << ENDPOINT << "Send PATH_RESPONSE to " << peer_address_to_send;
  if (default_path_.self_address == last_packet_destination_address_) {
    // The PATH_CHALLENGE is received on the default socket. Respond on the same
    // socket.
    return packet_creator_.AddPathResponseFrame(data_buffer);
  }

  QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, perspective_);
  // This PATH_CHALLENGE is received on an alternative socket which should be
  // used to send PATH_RESPONSE.
  if (!path_validator_.HasPendingPathValidation() ||
      path_validator_.GetContext()->self_address() !=
          last_packet_destination_address_) {
    // Ignore this PATH_CHALLENGE if it's received from an uninteresting socket.
    return true;
  }
  QuicPacketWriter* writer = path_validator_.GetContext()->WriterToUse();

  std::unique_ptr<SerializedPacket> probing_packet =
      packet_creator_.SerializePathResponseConnectivityProbingPacket(
          {data_buffer}, /*is_padded=*/true);
  QUICHE_DCHECK_EQ(IsRetransmittable(*probing_packet), NO_RETRANSMITTABLE_DATA);
  QUIC_DVLOG(1) << ENDPOINT
                << "Send PATH_RESPONSE from alternative socket with address "
                << last_packet_destination_address_;
  // Ignore the return value to treat write error on the alternative writer as
  // part of network error. If the writer becomes blocked, wait for the peer to
  // send another PATH_CHALLENGE.
  WritePacketUsingWriter(std::move(probing_packet), writer,
                         last_packet_destination_address_, peer_address_to_send,
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
  QUICHE_DCHECK(use_path_validator_);
  return path_validator_.HasPendingPathValidation();
}

QuicPathValidationContext* QuicConnection::GetPathValidationContext() const {
  QUICHE_DCHECK(use_path_validator_);
  return path_validator_.GetContext();
}

void QuicConnection::CancelPathValidation() {
  QUICHE_DCHECK(use_path_validator_);
  path_validator_.CancelPathValidation();
}

void QuicConnection::MigratePath(const QuicSocketAddress& self_address,
                                 const QuicSocketAddress& peer_address,
                                 QuicPacketWriter* writer,
                                 bool owns_writer) {
  if (!connected_) {
    return;
  }
  const bool is_port_change =
      QuicUtils::DetermineAddressChangeType(default_path_.self_address,
                                            self_address) == PORT_CHANGE &&
      QuicUtils::DetermineAddressChangeType(default_path_.peer_address,
                                            peer_address) == PORT_CHANGE;
  SetSelfAddress(self_address);
  UpdatePeerAddress(peer_address);
  SetQuicPacketWriter(writer, owns_writer);
  OnSuccessfulMigration(is_port_change);
}

std::vector<QuicConnectionId> QuicConnection::GetActiveServerConnectionIds()
    const {
  return {server_connection_id_};
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
    const QuicSocketAddress& peer_address,
    QuicByteCount sent_packet_size) {
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
      !IsAlternativePath(last_packet_destination_address_,
                         GetEffectivePeerAddressFromCurrentPacket()) ||
      current_incoming_packet_received_bytes_counted_) {
    return;
  }
  // Only update bytes received if this probing frame is received on the most
  // recent alternative path.
  QUICHE_DCHECK(!IsDefaultPath(last_packet_destination_address_,
                               GetEffectivePeerAddressFromCurrentPacket()));
  if (!alternative_path_.validated) {
    alternative_path_.bytes_received_before_address_validation +=
        received_packet_size;
  }
  current_incoming_packet_received_bytes_counted_ = true;
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
  validated = false;
  bytes_received_before_address_validation = 0;
  bytes_sent_before_address_validation = 0;
  send_algorithm = nullptr;
  rtt_stats = absl::nullopt;
}

QuicConnection::PathState::PathState(PathState&& other) {
  *this = std::move(other);
}

QuicConnection::PathState& QuicConnection::PathState::operator=(
    QuicConnection::PathState&& other) {
  if (this != &other) {
    self_address = other.self_address;
    peer_address = other.peer_address;
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

QuicConnection::ReversePathValidationResultDelegate::
    ReversePathValidationResultDelegate(
        QuicConnection* connection,
        const QuicSocketAddress& direct_peer_address)
    : QuicPathValidator::ResultDelegate(),
      connection_(connection),
      original_direct_peer_address_(direct_peer_address) {}

void QuicConnection::ReversePathValidationResultDelegate::
    OnPathValidationSuccess(
        std::unique_ptr<QuicPathValidationContext> context) {
  QUIC_DLOG(INFO) << "Successfully validated new path " << *context;
  if (connection_->IsDefaultPath(context->self_address(),
                                 context->peer_address())) {
    connection_->OnEffectivePeerMigrationValidated();
  } else {
    QUICHE_DCHECK(connection_->IsAlternativePath(
        context->self_address(), context->effective_peer_address()));
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
    connection_->RestoreToLastValidatedPath(original_direct_peer_address_);
  } else if (connection_->IsAlternativePath(
                 context->self_address(), context->effective_peer_address())) {
    connection_->alternative_path_.Clear();
  }
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

  // Revert congestion control context to old state.
  sent_packet_manager_.OnConnectionMigration(true);
  QUICHE_DCHECK(!sent_packet_manager_.HasInFlightPackets());
  // Stop detections in quiecense.
  blackhole_detector_.StopDetection();

  if (alternative_path_.send_algorithm != nullptr) {
    sent_packet_manager_.SetSendAlgorithm(
        alternative_path_.send_algorithm.release());
    sent_packet_manager_.SetRttStats(alternative_path_.rtt_stats.value());
  } else {
    QUIC_BUG << "Fail to store congestion controller before migration.";
  }

  UpdatePeerAddress(original_direct_peer_address);
  default_path_ = std::move(alternative_path_);

  active_effective_peer_migration_type_ = NO_CHANGE;
  ++stats_.num_invalid_peer_migration;
  // The reverse path validation failed because of alarm firing, flush all the
  // pending writes previously throttled by anti-amplification limit.
  WriteIfNotBlocked();
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic

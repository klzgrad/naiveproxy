// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_connection.h"

#include <string.h>
#include <sys/types.h>

#include <algorithm>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_utils.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/proto/cached_network_parameters_proto.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_config.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_generator.h"
#include "net/third_party/quiche/src/quic/core/quic_pending_retransmission.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_client_stats.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_error_code_wrappers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_exported_stats.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_map_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

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
    DCHECK(connection_->ack_frame_updated());
    QuicConnection::ScopedPacketFlusher flusher(connection_);
    if (connection_->SupportsMultiplePacketNumberSpaces()) {
      connection_->SendAllPendingAcks();
    } else {
      DCHECK(!connection_->GetUpdatedAckFrame().ack_frame->packets.Empty());
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

  void OnAlarm() override { connection_->OnRetransmissionTimeout(); }

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

  void OnAlarm() override { connection_->WriteAndBundleAcksIfNotBlocked(); }

 private:
  QuicConnection* connection_;
};

class PathDegradingAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit PathDegradingAlarmDelegate(QuicConnection* connection)
      : connection_(connection) {}
  PathDegradingAlarmDelegate(const PathDegradingAlarmDelegate&) = delete;
  PathDegradingAlarmDelegate& operator=(const PathDegradingAlarmDelegate&) =
      delete;

  void OnAlarm() override { connection_->OnPathDegradingTimeout(); }

 private:
  QuicConnection* connection_;
};

class TimeoutAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit TimeoutAlarmDelegate(QuicConnection* connection)
      : connection_(connection) {}
  TimeoutAlarmDelegate(const TimeoutAlarmDelegate&) = delete;
  TimeoutAlarmDelegate& operator=(const TimeoutAlarmDelegate&) = delete;

  void OnAlarm() override { connection_->CheckForTimeout(); }

 private:
  QuicConnection* connection_;
};

class PingAlarmDelegate : public QuicAlarm::Delegate {
 public:
  explicit PingAlarmDelegate(QuicConnection* connection)
      : connection_(connection) {}
  PingAlarmDelegate(const PingAlarmDelegate&) = delete;
  PingAlarmDelegate& operator=(const PingAlarmDelegate&) = delete;

  void OnAlarm() override { connection_->OnPingTimeout(); }

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

  void OnAlarm() override { connection_->DiscoverMtu(); }

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
    QuicConnection::ScopedPacketFlusher flusher(connection_);
    connection_->MaybeProcessUndecryptablePackets();
  }

 private:
  QuicConnection* connection_;
};

// Whether this incoming packet is allowed to replace our connection ID.
bool PacketCanReplaceConnectionId(const QuicPacketHeader& header,
                                  Perspective perspective) {
  return perspective == Perspective::IS_CLIENT &&
         header.form == IETF_QUIC_LONG_HEADER_PACKET &&
         QuicUtils::VariableLengthConnectionIdAllowedForVersion(
             header.version.transport_version) &&
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
      peer_address_(initial_peer_address),
      direct_peer_address_(initial_peer_address),
      active_effective_peer_migration_type_(NO_CHANGE),
      last_packet_decrypted_(false),
      last_size_(0),
      current_packet_data_(nullptr),
      last_decrypted_packet_level_(ENCRYPTION_INITIAL),
      should_last_packet_instigate_acks_(false),
      max_undecryptable_packets_(0),
      max_tracked_packets_(GetQuicFlag(FLAGS_quic_max_tracked_packet_count)),
      pending_version_negotiation_packet_(false),
      send_ietf_version_negotiation_packet_(false),
      send_version_negotiation_packet_with_prefixed_lengths_(false),
      idle_timeout_connection_close_behavior_(
          ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET),
      close_connection_after_five_rtos_(false),
      uber_received_packet_manager_(&stats_),
      stop_waiting_count_(0),
      pending_retransmission_alarm_(false),
      defer_send_in_response_to_packets_(false),
      ping_timeout_(QuicTime::Delta::FromSeconds(kPingTimeoutSecs)),
      retransmittable_on_wire_timeout_(QuicTime::Delta::Infinite()),
      arena_(),
      ack_alarm_(alarm_factory_->CreateAlarm(arena_.New<AckAlarmDelegate>(this),
                                             &arena_)),
      retransmission_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<RetransmissionAlarmDelegate>(this),
          &arena_)),
      send_alarm_(
          alarm_factory_->CreateAlarm(arena_.New<SendAlarmDelegate>(this),
                                      &arena_)),
      timeout_alarm_(
          alarm_factory_->CreateAlarm(arena_.New<TimeoutAlarmDelegate>(this),
                                      &arena_)),
      ping_alarm_(
          alarm_factory_->CreateAlarm(arena_.New<PingAlarmDelegate>(this),
                                      &arena_)),
      mtu_discovery_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<MtuDiscoveryAlarmDelegate>(this),
          &arena_)),
      path_degrading_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<PathDegradingAlarmDelegate>(this),
          &arena_)),
      process_undecryptable_packets_alarm_(alarm_factory_->CreateAlarm(
          arena_.New<ProcessUndecryptablePacketsAlarmDelegate>(this),
          &arena_)),
      visitor_(nullptr),
      debug_visitor_(nullptr),
      packet_generator_(server_connection_id_,
                        &framer_,
                        random_generator_,
                        this),
      idle_network_timeout_(QuicTime::Delta::Infinite()),
      handshake_timeout_(QuicTime::Delta::Infinite()),
      time_of_first_packet_sent_after_receiving_(QuicTime::Zero()),
      time_of_last_received_packet_(clock_->ApproximateNow()),
      sent_packet_manager_(perspective,
                           clock_,
                           random_generator_,
                           &stats_,
                           GetDefaultCongestionControlType(),
                           kNack),
      version_negotiated_(false),
      perspective_(perspective),
      connected_(true),
      can_truncate_connection_ids_(perspective == Perspective::IS_SERVER),
      mtu_discovery_target_(0),
      mtu_probe_count_(0),
      packets_between_mtu_probes_(kPacketsBetweenMtuProbesBase),
      next_mtu_probe_at_(kPacketsBetweenMtuProbesBase),
      largest_received_packet_size_(0),
      write_error_occurred_(false),
      no_stop_waiting_frames_(
          VersionHasIetfInvariantHeader(transport_version())),
      consecutive_num_packets_with_no_retransmittable_frames_(0),
      max_consecutive_num_packets_with_no_retransmittable_frames_(
          kMaxConsecutiveNonRetransmittablePackets),
      fill_up_link_during_probing_(false),
      probing_retransmission_pending_(false),
      stateless_reset_token_received_(false),
      received_stateless_reset_token_(0),
      last_control_frame_id_(kInvalidControlFrameId),
      is_path_degrading_(false),
      processing_ack_frame_(false),
      supports_release_time_(false),
      release_time_into_future_(QuicTime::Delta::Zero()),
      retry_has_been_parsed_(false),
      max_consecutive_ptos_(0),
      bytes_received_before_address_validation_(0),
      bytes_sent_before_address_validation_(0),
      address_validated_(false) {
  QUIC_DLOG(INFO) << ENDPOINT << "Created connection with server connection ID "
                  << server_connection_id
                  << " and version: " << ParsedQuicVersionToString(version());

  QUIC_BUG_IF(!QuicUtils::IsConnectionIdValidForVersion(server_connection_id,
                                                        transport_version()))
      << "QuicConnection: attempted to use server connection ID "
      << server_connection_id << " which is invalid with version "
      << QuicVersionToString(transport_version());

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
  MaybeEnableSessionDecidesWhatToWrite();
  MaybeEnableMultiplePacketNumberSpacesSupport();
  DCHECK(perspective_ == Perspective::IS_CLIENT ||
         supported_versions.size() == 1);
  InstallInitialCrypters(server_connection_id_);
}

void QuicConnection::InstallInitialCrypters(QuicConnectionId connection_id) {
  if (version().handshake_protocol != PROTOCOL_TLS1_3) {
    // Initial crypters are currently only supported with TLS.
    return;
  }
  CrypterPair crypters;
  CryptoUtils::CreateTlsInitialCrypters(perspective_, transport_version(),
                                        connection_id, &crypters);
  SetEncrypter(ENCRYPTION_INITIAL, std::move(crypters.encrypter));
  InstallDecrypter(ENCRYPTION_INITIAL, std::move(crypters.decrypter));
}

QuicConnection::~QuicConnection() {
  if (owns_writer_) {
    delete writer_;
  }
  ClearQueuedPackets();
}

void QuicConnection::ClearQueuedPackets() {
  for (auto it = queued_packets_.begin(); it != queued_packets_.end(); ++it) {
    // Delete the buffer before calling ClearSerializedPacket, which sets
    // encrypted_buffer to nullptr.
    delete[] it->encrypted_buffer;
    ClearSerializedPacket(&(*it));
  }
  queued_packets_.clear();
}

void QuicConnection::SetFromConfig(const QuicConfig& config) {
  if (config.negotiated()) {
    // Handshake complete, set handshake timeout to Infinite.
    SetNetworkTimeouts(QuicTime::Delta::Infinite(),
                       config.IdleNetworkTimeout());
    if (config.SilentClose()) {
      idle_timeout_connection_close_behavior_ =
          ConnectionCloseBehavior::SILENT_CLOSE;
    }
  } else {
    SetNetworkTimeouts(config.max_time_before_crypto_handshake(),
                       config.max_idle_time_before_crypto_handshake());
  }

  sent_packet_manager_.SetFromConfig(config);
  if (config.HasReceivedBytesForConnectionId() &&
      can_truncate_connection_ids_) {
    packet_generator_.SetServerConnectionIdLength(
        config.ReceivedBytesForConnectionId());
  }
  max_undecryptable_packets_ = config.max_undecryptable_packets();

  if (config.HasClientSentConnectionOption(kMTUH, perspective_)) {
    SetMtuDiscoveryTarget(kMtuDiscoveryTargetPacketSizeHigh);
  }
  if (config.HasClientSentConnectionOption(kMTUL, perspective_)) {
    SetMtuDiscoveryTarget(kMtuDiscoveryTargetPacketSizeLow);
  }
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnSetFromConfig(config);
  }
  uber_received_packet_manager_.SetFromConfig(config, perspective_);
  if (config.HasClientSentConnectionOption(k5RTO, perspective_)) {
    close_connection_after_five_rtos_ = true;
  }
  if (sent_packet_manager_.pto_enabled()) {
    if (config.HasClientSentConnectionOption(k7PTO, perspective_)) {
      max_consecutive_ptos_ = 6;
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_enable_pto, 3, 4);
    }
    if (config.HasClientSentConnectionOption(k8PTO, perspective_)) {
      max_consecutive_ptos_ = 7;
      QUIC_RELOADABLE_FLAG_COUNT_N(quic_enable_pto, 4, 4);
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

  supports_release_time_ =
      writer_ != nullptr && writer_->SupportsReleaseTime() &&
      !config.HasClientSentConnectionOption(kNPCO, perspective_);

  if (supports_release_time_) {
    UpdateReleaseTimeIntoFuture();
  }
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

void QuicConnection::AdjustNetworkParameters(QuicBandwidth bandwidth,
                                             QuicTime::Delta rtt,
                                             bool allow_cwnd_to_decrease) {
  sent_packet_manager_.AdjustNetworkParameters(bandwidth, rtt,
                                               allow_cwnd_to_decrease);
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
  DCHECK_EQ(server_connection_id_, packet.connection_id);
  DCHECK_EQ(perspective_, Perspective::IS_CLIENT);
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPublicResetPacket(packet);
  }
  std::string error_details = "Received public reset.";
  if (perspective_ == Perspective::IS_CLIENT && !packet.endpoint_id.empty()) {
    QuicStrAppend(&error_details, " From ", packet.endpoint_id, ".");
  }
  QUIC_DLOG(INFO) << ENDPOINT << error_details;
  QUIC_CODE_COUNT(quic_tear_down_local_connection_on_public_reset);
  TearDownLocalConnectionState(QUIC_PUBLIC_RESET, error_details,
                               ConnectionCloseSource::FROM_PEER);
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
  DCHECK_EQ(server_connection_id_, packet.connection_id);
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
    const std::string error_details = QuicStrCat(
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
      QuicStrCat(
          "Client may support one of the versions in the server's list, but "
          "it's going to close the connection anyway. Supported versions: {",
          ParsedQuicVersionVectorToString(framer_.supported_versions()),
          "}, peer supported versions: {",
          ParsedQuicVersionVectorToString(packet.versions), "}"),
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

// Handles retry for client connection.
void QuicConnection::OnRetryPacket(QuicConnectionId original_connection_id,
                                   QuicConnectionId new_connection_id,
                                   QuicStringPiece retry_token) {
  DCHECK_EQ(Perspective::IS_CLIENT, perspective_);
  if (original_connection_id != server_connection_id_) {
    QUIC_DLOG(ERROR) << "Ignoring RETRY with original connection ID "
                     << original_connection_id << " not matching expected "
                     << server_connection_id_ << " token "
                     << QuicTextUtils::HexEncode(retry_token);
    return;
  }
  if (retry_has_been_parsed_) {
    QUIC_DLOG(ERROR) << "Ignoring non-first RETRY with token "
                     << QuicTextUtils::HexEncode(retry_token);
    return;
  }
  retry_has_been_parsed_ = true;
  QUIC_DLOG(INFO) << "Received RETRY, replacing connection ID "
                  << server_connection_id_ << " with " << new_connection_id
                  << ", received token "
                  << QuicTextUtils::HexEncode(retry_token);
  server_connection_id_ = new_connection_id;
  packet_generator_.SetServerConnectionId(server_connection_id_);
  packet_generator_.SetRetryToken(retry_token);

  // Reinstall initial crypters because the connection ID changed.
  InstallInitialCrypters(server_connection_id_);
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

void QuicConnection::AddIncomingConnectionId(QuicConnectionId connection_id) {
  if (HasIncomingConnectionId(connection_id)) {
    return;
  }
  incoming_connection_ids_.push_back(connection_id);
}

bool QuicConnection::OnUnauthenticatedPublicHeader(
    const QuicPacketHeader& header) {
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
    DCHECK_NE(Perspective::IS_SERVER, perspective_);
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
  DCHECK(GetServerConnectionIdAsRecipient(header, perspective_) ==
             server_connection_id_ ||
         HasIncomingConnectionId(
             GetServerConnectionIdAsRecipient(header, perspective_)) ||
         PacketCanReplaceConnectionId(header, perspective_));

  if (packet_generator_.HasPendingFrames()) {
    // Incoming packets may change a queued ACK frame.
    const std::string error_details =
        "Pending frames must be serialized before incoming packets are "
        "processed.";
    QUIC_BUG << error_details << ", received header: " << header;
    CloseConnection(QUIC_INTERNAL_ERROR, error_details,
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  if (!version_negotiated_ && perspective_ == Perspective::IS_SERVER) {
    if (!header.version_flag) {
      // Packets should have the version flag till version negotiation is
      // done.
      std::string error_details =
          QuicStrCat(ENDPOINT, "Packet ", header.packet_number.ToUint64(),
                     " without version flag before version negotiated.");
      QUIC_DLOG(WARNING) << error_details;
      CloseConnection(QUIC_INVALID_VERSION, error_details,
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return false;
    } else {
      DCHECK_EQ(header.version, version());
      version_negotiated_ = true;
      framer_.InferPacketHeaderTypeFromVersion();
      visitor_->OnSuccessfulVersionNegotiation(version());
      if (debug_visitor_ != nullptr) {
        debug_visitor_->OnSuccessfulVersionNegotiation(version());
      }
    }
    DCHECK(version_negotiated_);
  }

  return true;
}

void QuicConnection::OnDecryptedPacket(EncryptionLevel level) {
  last_decrypted_packet_level_ = level;
  last_packet_decrypted_ = true;
  if (EnforceAntiAmplificationLimit() &&
      last_decrypted_packet_level_ >= ENCRYPTION_HANDSHAKE) {
    // Address is validated by successfully processing a HANDSHAKE packet.
    address_validated_ = true;
  }

  // Once the server receives a forward secure packet, the handshake is
  // confirmed.
  if (level == ENCRYPTION_FORWARD_SECURE &&
      perspective_ == Perspective::IS_SERVER) {
    OnHandshakeComplete();
  }
}

QuicSocketAddress QuicConnection::GetEffectivePeerAddressFromCurrentPacket()
    const {
  // By default, the connection is not proxied, and the effective peer address
  // is the packet's source address, i.e. the direct peer address.
  return last_packet_source_address_;
}

bool QuicConnection::OnPacketHeader(const QuicPacketHeader& header) {
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPacketHeader(header);
  }

  // Will be decremented below if we fall through to return true.
  ++stats_.packets_dropped;

  if (!ProcessValidatedPacket(header)) {
    return false;
  }

  // Initialize the current packet content state.
  current_packet_content_ = NO_FRAMES_RECEIVED;
  is_current_packet_connectivity_probing_ = false;
  current_effective_peer_migration_type_ = NO_CHANGE;

  if (perspective_ == Perspective::IS_CLIENT) {
    if (!GetLargestReceivedPacket().IsInitialized() ||
        header.packet_number > GetLargestReceivedPacket()) {
      // Update peer_address_ and effective_peer_address_ immediately for
      // client connections.
      // TODO(fayang): only change peer addresses in application data packet
      // number space.
      direct_peer_address_ = last_packet_source_address_;
      effective_peer_address_ = GetEffectivePeerAddressFromCurrentPacket();
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
            effective_peer_address_,
            GetEffectivePeerAddressFromCurrentPacket());

    QUIC_DLOG_IF(INFO, current_effective_peer_migration_type_ != NO_CHANGE)
        << ENDPOINT << "Effective peer's ip:port changed from "
        << effective_peer_address_.ToString() << " to "
        << GetEffectivePeerAddressFromCurrentPacket().ToString()
        << ", active_effective_peer_migration_type is "
        << active_effective_peer_migration_type_;
  }

  --stats_.packets_dropped;
  QUIC_DVLOG(1) << ENDPOINT << "Received packet header: " << header;
  last_header_ = header;

  // Record packet receipt to populate ack info before processing stream
  // frames, since the processing may result in sending a bundled ack.
  uber_received_packet_manager_.RecordPacketReceived(
      last_decrypted_packet_level_, last_header_,
      time_of_last_received_packet_);
  DCHECK(connected_);
  return true;
}

bool QuicConnection::OnStreamFrame(const QuicStreamFrame& frame) {
  DCHECK(connected_);

  // Since a stream frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  UpdatePacketContent(NOT_PADDED_PING);

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
  visitor_->OnStreamFrame(frame);
  stats_.stream_bytes_received += frame.data_length;
  should_last_packet_instigate_acks_ = true;
  return connected_;
}

bool QuicConnection::OnCryptoFrame(const QuicCryptoFrame& frame) {
  DCHECK(connected_);

  // Since a CRYPTO frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  UpdatePacketContent(NOT_PADDED_PING);

  visitor_->OnCryptoFrame(frame);
  should_last_packet_instigate_acks_ = true;
  return connected_;
}

bool QuicConnection::OnAckFrameStart(QuicPacketNumber largest_acked,
                                     QuicTime::Delta ack_delay_time) {
  DCHECK(connected_);

  if (processing_ack_frame_) {
    CloseConnection(QUIC_INVALID_ACK_DATA,
                    "Received a new ack while processing an ack frame.",
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  // Since an ack frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  UpdatePacketContent(NOT_PADDED_PING);

  QUIC_DVLOG(1) << ENDPOINT
                << "OnAckFrameStart, largest_acked: " << largest_acked;

  if (GetLargestReceivedPacketWithAck().IsInitialized() &&
      last_header_.packet_number <= GetLargestReceivedPacketWithAck()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received an old ack frame: ignoring";
    return true;
  }

  if (!GetLargestSentPacket().IsInitialized() ||
      largest_acked > GetLargestSentPacket()) {
    QUIC_DLOG(WARNING) << ENDPOINT
                       << "Peer's observed unsent packet:" << largest_acked
                       << " vs " << GetLargestSentPacket();
    // We got an ack for data we have not sent.
    CloseConnection(QUIC_INVALID_ACK_DATA, "Largest observed too high.",
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  if (!GetLargestAckedPacket().IsInitialized() ||
      largest_acked > GetLargestAckedPacket()) {
    visitor_->OnForwardProgressConfirmed();
  }
  processing_ack_frame_ = true;
  sent_packet_manager_.OnAckFrameStart(largest_acked, ack_delay_time,
                                       time_of_last_received_packet_);
  return true;
}

bool QuicConnection::OnAckRange(QuicPacketNumber start, QuicPacketNumber end) {
  DCHECK(connected_);
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
  DCHECK(connected_);
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
  DCHECK(connected_);
  QUIC_DVLOG(1) << ENDPOINT << "OnAckFrameEnd, start: " << start;

  if (GetLargestReceivedPacketWithAck().IsInitialized() &&
      last_header_.packet_number <= GetLargestReceivedPacketWithAck()) {
    QUIC_DLOG(INFO) << ENDPOINT << "Received an old ack frame: ignoring";
    return true;
  }
  const AckResult ack_result = sent_packet_manager_.OnAckFrameEnd(
      time_of_last_received_packet_, last_header_.packet_number,
      last_decrypted_packet_level_);
  if (ack_result != PACKETS_NEWLY_ACKED &&
      ack_result != NO_PACKETS_NEWLY_ACKED) {
    // Error occurred (e.g., this ACK tries to ack packets in wrong packet
    // number space), and this would cause the connection to be closed.
    QUIC_DLOG(ERROR) << ENDPOINT
                     << "Error occurred when processing an ACK frame: "
                     << QuicUtils::AckResultToString(ack_result);
    return false;
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
  bool send_stop_waiting = GetLeastUnacked() > start;
  if (GetQuicReloadableFlag(quic_simplify_stop_waiting) &&
      no_stop_waiting_frames_) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_simplify_stop_waiting);
    send_stop_waiting = false;
  }
  PostProcessAfterAckFrame(send_stop_waiting,
                           ack_result == PACKETS_NEWLY_ACKED);
  processing_ack_frame_ = false;

  return connected_;
}

bool QuicConnection::OnStopWaitingFrame(const QuicStopWaitingFrame& frame) {
  DCHECK(connected_);

  // Since a stop waiting frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  UpdatePacketContent(NOT_PADDED_PING);

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
  DCHECK(connected_);
  UpdatePacketContent(SECOND_FRAME_IS_PADDING);

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPaddingFrame(frame);
  }
  return true;
}

bool QuicConnection::OnPingFrame(const QuicPingFrame& frame) {
  DCHECK(connected_);
  UpdatePacketContent(FIRST_FRAME_IS_PING);

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPingFrame(frame);
  }
  should_last_packet_instigate_acks_ = true;
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
  DCHECK(connected_);

  // Since a reset stream frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  UpdatePacketContent(NOT_PADDED_PING);

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnRstStreamFrame(frame);
  }
  QUIC_DLOG(INFO) << ENDPOINT
                  << "RST_STREAM_FRAME received for stream: " << frame.stream_id
                  << " with error: "
                  << QuicRstStreamErrorCodeToString(frame.error_code);
  visitor_->OnRstStream(frame);
  should_last_packet_instigate_acks_ = true;
  return connected_;
}

bool QuicConnection::OnStopSendingFrame(const QuicStopSendingFrame& frame) {
  DCHECK(connected_);

  // Since a reset stream frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  UpdatePacketContent(NOT_PADDED_PING);

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnStopSendingFrame(frame);
  }

  QUIC_DLOG(INFO) << ENDPOINT << "STOP_SENDING frame received for stream: "
                  << frame.stream_id
                  << " with error: " << frame.application_error_code;

  visitor_->OnStopSendingFrame(frame);
  return connected_;
}

bool QuicConnection::OnPathChallengeFrame(const QuicPathChallengeFrame& frame) {
  // Save the path challenge's payload, for later use in generating the
  // response.
  received_path_challenge_payloads_.push_back(frame.data_buffer);

  // For VERSION 99 we define a "Padded PATH CHALLENGE" to be the same thing
  // as a PADDED PING -- it will start a connectivity check and prevent
  // connection migration. Insofar as the connectivity check and connection
  // migration are concerned, logically the PATH CHALLENGE is the same as the
  // PING, so as a stopgap, tell the FSM that determines whether we have a
  // Padded PING or not that we received a PING.
  UpdatePacketContent(FIRST_FRAME_IS_PING);
  should_last_packet_instigate_acks_ = true;
  return true;
}

bool QuicConnection::OnPathResponseFrame(const QuicPathResponseFrame& frame) {
  should_last_packet_instigate_acks_ = true;
  if (!transmitted_connectivity_probe_payload_ ||
      *transmitted_connectivity_probe_payload_ != frame.data_buffer) {
    // Is not for the probe we sent, ignore it.
    return true;
  }
  // Have received the matching PATH RESPONSE, saved payload no longer valid.
  transmitted_connectivity_probe_payload_ = nullptr;
  UpdatePacketContent(FIRST_FRAME_IS_PING);
  return true;
}

bool QuicConnection::OnConnectionCloseFrame(
    const QuicConnectionCloseFrame& frame) {
  DCHECK(connected_);

  // Since a connection close frame was received, this is not a connectivity
  // probe. A probe only contains a PING and full padding.
  UpdatePacketContent(NOT_PADDED_PING);

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnConnectionCloseFrame(frame);
  }
  switch (frame.close_type) {
    case GOOGLE_QUIC_CONNECTION_CLOSE:
      QUIC_DLOG(INFO) << ENDPOINT << "Received ConnectionClose for connection: "
                      << connection_id() << ", with error: "
                      << QuicErrorCodeToString(frame.extracted_error_code)
                      << " (" << frame.error_details << ")";
      break;
    case IETF_QUIC_TRANSPORT_CONNECTION_CLOSE:
      QUIC_DLOG(INFO) << ENDPOINT
                      << "Received Transport ConnectionClose for connection: "
                      << connection_id() << ", with error: "
                      << QuicErrorCodeToString(frame.extracted_error_code)
                      << " (" << frame.error_details << ")"
                      << ", transport error code: "
                      << frame.transport_error_code << ", error frame type: "
                      << frame.transport_close_frame_type;
      break;
    case IETF_QUIC_APPLICATION_CONNECTION_CLOSE:
      QUIC_DLOG(INFO) << ENDPOINT
                      << "Received Application ConnectionClose for connection: "
                      << connection_id() << ", with error: "
                      << QuicErrorCodeToString(frame.extracted_error_code)
                      << " (" << frame.error_details << ")"
                      << ", application error code: "
                      << frame.application_error_code;
      break;
  }

  if (frame.extracted_error_code == QUIC_BAD_MULTIPATH_FLAG) {
    QUIC_LOG_FIRST_N(ERROR, 10) << "Unexpected QUIC_BAD_MULTIPATH_FLAG error."
                                << " last_received_header: " << last_header_
                                << " encryption_level: " << encryption_level_;
  }
  TearDownLocalConnectionState(frame, ConnectionCloseSource::FROM_PEER);
  return connected_;
}

bool QuicConnection::OnMaxStreamsFrame(const QuicMaxStreamsFrame& frame) {
  return visitor_->OnMaxStreamsFrame(frame);
}

bool QuicConnection::OnStreamsBlockedFrame(
    const QuicStreamsBlockedFrame& frame) {
  return visitor_->OnStreamsBlockedFrame(frame);
}

bool QuicConnection::OnGoAwayFrame(const QuicGoAwayFrame& frame) {
  DCHECK(connected_);

  // Since a go away frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  UpdatePacketContent(NOT_PADDED_PING);

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnGoAwayFrame(frame);
  }
  QUIC_DLOG(INFO) << ENDPOINT << "GOAWAY_FRAME received with last good stream: "
                  << frame.last_good_stream_id
                  << " and error: " << QuicErrorCodeToString(frame.error_code)
                  << " and reason: " << frame.reason_phrase;

  visitor_->OnGoAway(frame);
  should_last_packet_instigate_acks_ = true;
  return connected_;
}

bool QuicConnection::OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) {
  DCHECK(connected_);

  // Since a window update frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  UpdatePacketContent(NOT_PADDED_PING);

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnWindowUpdateFrame(frame, time_of_last_received_packet_);
  }
  QUIC_DVLOG(1) << ENDPOINT << "WINDOW_UPDATE_FRAME received for stream: "
                << frame.stream_id
                << " with byte offset: " << frame.byte_offset;
  visitor_->OnWindowUpdateFrame(frame);
  should_last_packet_instigate_acks_ = true;
  return connected_;
}

bool QuicConnection::OnNewConnectionIdFrame(
    const QuicNewConnectionIdFrame& /*frame*/) {
  return true;
}

bool QuicConnection::OnRetireConnectionIdFrame(
    const QuicRetireConnectionIdFrame& /*frame*/) {
  return true;
}

bool QuicConnection::OnNewTokenFrame(const QuicNewTokenFrame& /*frame*/) {
  return true;
}

bool QuicConnection::OnMessageFrame(const QuicMessageFrame& frame) {
  DCHECK(connected_);

  // Since a message frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  UpdatePacketContent(NOT_PADDED_PING);

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnMessageFrame(frame);
  }
  visitor_->OnMessageReceived(
      QuicStringPiece(frame.data, frame.message_length));
  should_last_packet_instigate_acks_ = true;
  return connected_;
}

bool QuicConnection::OnBlockedFrame(const QuicBlockedFrame& frame) {
  DCHECK(connected_);

  // Since a blocked frame was received, this is not a connectivity probe.
  // A probe only contains a PING and full padding.
  UpdatePacketContent(NOT_PADDED_PING);

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnBlockedFrame(frame);
  }
  QUIC_DLOG(INFO) << ENDPOINT
                  << "BLOCKED_FRAME received for stream: " << frame.stream_id;
  visitor_->OnBlockedFrame(frame);
  stats_.blocked_frames_received++;
  should_last_packet_instigate_acks_ = true;
  return connected_;
}

void QuicConnection::OnPacketComplete() {
  // Don't do anything if this packet closed the connection.
  if (!connected_) {
    ClearLastFrames();
    return;
  }

  if (IsCurrentPacketConnectivityProbing()) {
    ++stats_.num_connectivity_probing_received;
  }

  QUIC_DVLOG(1) << ENDPOINT << "Got packet " << last_header_.packet_number
                << " for "
                << GetServerConnectionIdAsRecipient(last_header_, perspective_);

  QUIC_DLOG_IF(INFO, current_packet_content_ == SECOND_FRAME_IS_PADDING)
      << ENDPOINT << "Received a padded PING packet. is_probing: "
      << IsCurrentPacketConnectivityProbing();

  if (IsCurrentPacketConnectivityProbing()) {
    QUIC_DVLOG(1) << ENDPOINT << "Received a connectivity probing packet for "
                  << GetServerConnectionIdAsRecipient(last_header_,
                                                      perspective_)
                  << " from ip:port: " << last_packet_source_address_.ToString()
                  << " to ip:port: "
                  << last_packet_destination_address_.ToString();
    visitor_->OnPacketReceived(last_packet_destination_address_,
                               last_packet_source_address_,
                               /*is_connectivity_probe=*/true);
  } else if (perspective_ == Perspective::IS_CLIENT) {
    // This node is a client, notify that a speculative connectivity probing
    // packet has been received anyway.
    QUIC_DVLOG(1) << ENDPOINT
                  << "Received a speculative connectivity probing packet for "
                  << GetServerConnectionIdAsRecipient(last_header_,
                                                      perspective_)
                  << " from ip:port: " << last_packet_source_address_.ToString()
                  << " to ip:port: "
                  << last_packet_destination_address_.ToString();
    visitor_->OnPacketReceived(last_packet_destination_address_,
                               last_packet_source_address_,
                               /*is_connectivity_probe=*/false);
  } else {
    // This node is not a client (is a server) AND the received packet was
    // NOT connectivity-probing. If the packet had PATH CHALLENGES, send
    // appropriate RESPONSE. Then deal with possible peer migration.
    if (VersionHasIetfQuicFrames(transport_version()) &&
        !received_path_challenge_payloads_.empty()) {
      // If a PATH CHALLENGE was in a "Padded PING (or PATH CHALLENGE)"
      // then it is taken care of above. This handles the case where a PATH
      // CHALLENGE appeared someplace else (eg, the peer randomly added a PATH
      // CHALLENGE frame to some other packet.
      // There was at least one PATH CHALLENGE in the received packet,
      // Generate the required PATH RESPONSE.
      SendGenericPathProbePacket(nullptr, last_packet_source_address_,
                                 /* is_response= */ true);
    }

    if (last_header_.packet_number == GetLargestReceivedPacket()) {
      direct_peer_address_ = last_packet_source_address_;
      if (current_effective_peer_migration_type_ != NO_CHANGE) {
        // TODO(fayang): When multiple packet number spaces is supported, only
        // start peer migration for the application data.
        StartEffectivePeerMigration(current_effective_peer_migration_type_);
      }
    }
  }

  current_effective_peer_migration_type_ = NO_CHANGE;

  // Some encryption levels share a packet number space, it is therefore
  // possible for us to want to ack some packets even though we do not yet
  // have the appropriate keys to encrypt the acks. In this scenario we
  // do not update the ACK timeout. This can happen for example with
  // IETF QUIC on the server when we receive 0-RTT packets and do not yet
  // have 1-RTT keys (0-RTT packets are acked at the 1-RTT level).
  // Note that this could cause slight performance degradations in the edge
  // case where one packet is received, then the encrypter is installed,
  // then a second packet is received; as that could cause the ACK for the
  // second packet to be delayed instead of immediate. This is currently
  // considered to be small enough of an edge case to not be optimized for.
  if (!SupportsMultiplePacketNumberSpaces() ||
      framer_.HasEncrypterOfEncryptionLevel(QuicUtils::GetEncryptionLevel(
          QuicUtils::GetPacketNumberSpace(last_decrypted_packet_level_)))) {
    uber_received_packet_manager_.MaybeUpdateAckTimeout(
        should_last_packet_instigate_acks_, last_decrypted_packet_level_,
        last_header_.packet_number, time_of_last_received_packet_,
        clock_->ApproximateNow(), sent_packet_manager_.GetRttStats());
  } else {
    QUIC_DLOG(INFO) << ENDPOINT << "Not updating ACK timeout for "
                    << QuicUtils::EncryptionLevelToString(
                           last_decrypted_packet_level_)
                    << " as we do not have the corresponding encrypter";
  }

  ClearLastFrames();
  CloseIfTooManyOutstandingSentPackets();
}

bool QuicConnection::IsValidStatelessResetToken(QuicUint128 token) const {
  return stateless_reset_token_received_ &&
         token == received_stateless_reset_token_;
}

void QuicConnection::OnAuthenticatedIetfStatelessResetPacket(
    const QuicIetfStatelessResetPacket& /*packet*/) {
  // TODO(fayang): Add OnAuthenticatedIetfStatelessResetPacket to
  // debug_visitor_.
  const std::string error_details = "Received stateless reset.";
  QUIC_CODE_COUNT(quic_tear_down_local_connection_on_stateless_reset);
  TearDownLocalConnectionState(QUIC_PUBLIC_RESET, error_details,
                               ConnectionCloseSource::FROM_PEER);
}

void QuicConnection::ClearLastFrames() {
  should_last_packet_instigate_acks_ = false;
}

void QuicConnection::CloseIfTooManyOutstandingSentPackets() {
  // This occurs if we don't discard old packets we've seen fast enough. It's
  // possible largest observed is less than leaset unacked.
  if (sent_packet_manager_.GetLargestObserved().IsInitialized() &&
      sent_packet_manager_.GetLargestObserved() >
          sent_packet_manager_.GetLeastUnacked() + max_tracked_packets_) {
    CloseConnection(
        QUIC_TOO_MANY_OUTSTANDING_SENT_PACKETS,
        QuicStrCat(
            "More than ", max_tracked_packets_, " outstanding, least_unacked: ",
            sent_packet_manager_.GetLeastUnacked().ToUint64(),
            ", packets_processed: ", stats_.packets_processed,
            ", last_decrypted_packet_level: ",
            QuicUtils::EncryptionLevelToString(last_decrypted_packet_level_)),
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }
}

const QuicFrame QuicConnection::GetUpdatedAckFrame() {
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

void QuicConnection::SendVersionNegotiationPacket(bool ietf_quic,
                                                  bool has_length_prefix) {
  pending_version_negotiation_packet_ = true;
  send_ietf_version_negotiation_packet_ = ietf_quic;
  send_version_negotiation_packet_with_prefixed_lengths_ = has_length_prefix;

  if (HandleWriteBlocked()) {
    return;
  }

  QUIC_DLOG(INFO) << ENDPOINT << "Sending version negotiation packet: {"
                  << ParsedQuicVersionVectorToString(
                         framer_.supported_versions())
                  << "}, " << (ietf_quic ? "" : "!") << "ietf_quic";
  std::unique_ptr<QuicEncryptedPacket> version_packet(
      packet_generator_.SerializeVersionNegotiationPacket(
          ietf_quic, has_length_prefix, framer_.supported_versions()));
  QUIC_DVLOG(2) << ENDPOINT << "Sending version negotiation packet: {"
                << ParsedQuicVersionVectorToString(framer_.supported_versions())
                << "}, " << (ietf_quic ? "" : "!") << "ietf_quic:" << std::endl
                << QuicTextUtils::HexDump(QuicStringPiece(
                       version_packet->data(), version_packet->length()));
  WriteResult result = writer_->WritePacket(
      version_packet->data(), version_packet->length(), self_address().host(),
      peer_address(), per_packet_options_);

  if (IsWriteError(result.status)) {
    OnWriteError(result.error_code);
    return;
  }
  if (IsWriteBlockedStatus(result.status)) {
    visitor_->OnWriteBlocked();
    if (result.status == WRITE_STATUS_BLOCKED_DATA_BUFFERED) {
      pending_version_negotiation_packet_ = false;
    }
    return;
  }

  pending_version_negotiation_packet_ = false;
}

size_t QuicConnection::SendCryptoData(EncryptionLevel level,
                                      size_t write_length,
                                      QuicStreamOffset offset) {
  if (write_length == 0) {
    QUIC_BUG << "Attempt to send empty crypto frame";
    return 0;
  }
  if (!ShouldGeneratePacket(HAS_RETRANSMITTABLE_DATA, IS_HANDSHAKE)) {
    return 0;
  }
  ScopedPacketFlusher flusher(this);
  return packet_generator_.ConsumeCryptoData(level, write_length, offset);
}

QuicConsumedData QuicConnection::SendStreamData(QuicStreamId id,
                                                size_t write_length,
                                                QuicStreamOffset offset,
                                                StreamSendingState state) {
  if (state == NO_FIN && write_length == 0) {
    QUIC_BUG << "Attempt to send empty stream frame";
    return QuicConsumedData(0, false);
  }

  // Opportunistically bundle an ack with every outgoing packet.
  // Particularly, we want to bundle with handshake packets since we don't know
  // which decrypter will be used on an ack packet following a handshake
  // packet (a handshake packet from client to server could result in a REJ or a
  // SHLO from the server, leading to two different decrypters at the server.)
  ScopedPacketFlusher flusher(this);
  return packet_generator_.ConsumeData(id, write_length, offset, state);
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
                  << " at encryption level: "
                  << QuicUtils::EncryptionLevelToString(encryption_level_);
    return false;
  }
  ScopedPacketFlusher flusher(this);
  const bool consumed =
      packet_generator_.ConsumeRetransmittableControlFrame(frame);
  if (!consumed) {
    QUIC_DVLOG(1) << ENDPOINT << "Failed to send control frame: " << frame;
    return false;
  }
  if (frame.type == PING_FRAME) {
    // Flush PING frame immediately.
    packet_generator_.FlushAllQueuedFrames();
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
  if (packet_generator_.HasPendingStreamFramesOfStream(id)) {
    ScopedPacketFlusher flusher(this);
    packet_generator_.FlushAllQueuedFrames();
  }

  sent_packet_manager_.CancelRetransmissionsForStream(id);
  // Remove all queued packets which only contain data for the reset stream.
  // TODO(fayang): consider removing this because it should be rarely executed.
  auto packet_iterator = queued_packets_.begin();
  while (packet_iterator != queued_packets_.end()) {
    QuicFrames* retransmittable_frames =
        &packet_iterator->retransmittable_frames;
    if (retransmittable_frames->empty()) {
      ++packet_iterator;
      continue;
    }
    // NOTE THAT RemoveFramesForStream removes only STREAM frames
    // for the specified stream.
    RemoveFramesForStream(retransmittable_frames, id);
    if (!retransmittable_frames->empty()) {
      ++packet_iterator;
      continue;
    }
    delete[] packet_iterator->encrypted_buffer;
    ClearSerializedPacket(&(*packet_iterator));
    packet_iterator = queued_packets_.erase(packet_iterator);
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
  stats_.max_packet_size = packet_generator_.GetCurrentMaxPacketLength();
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
                << QuicUtils::EncryptionLevelToString(decryption_level)
                << " while connection is at encryption level "
                << QuicUtils::EncryptionLevelToString(encryption_level_);
  DCHECK(GetQuicRestartFlag(quic_framer_uses_undecryptable_upcall));
  QUIC_RESTART_FLAG_COUNT_N(quic_framer_uses_undecryptable_upcall, 1, 7);
  DCHECK(EncryptionLevelIsValid(decryption_level));
  ++stats_.undecryptable_packets_received;

  bool should_enqueue = true;
  if (encryption_level_ == ENCRYPTION_FORWARD_SECURE) {
    // We do not expect to install any further keys.
    should_enqueue = false;
  } else if (undecryptable_packets_.size() >= max_undecryptable_packets_) {
    // We do not queue more than max_undecryptable_packets_ packets.
    should_enqueue = false;
  } else if (has_decryption_key) {
    // We already have the key for this decryption level, therefore no
    // future keys will allow it be decrypted.
    should_enqueue = false;
  } else if (version().KnowsWhichDecrypterToUse() &&
             decryption_level <= encryption_level_) {
    // On versions that know which decrypter to use, we install keys in order
    // so we will not get newer keys for lower encryption levels.
    should_enqueue = false;
  }

  if (should_enqueue) {
    QueueUndecryptablePacket(packet);
  } else if (debug_visitor_ != nullptr) {
    debug_visitor_->OnUndecryptablePacket();
  }
}

void QuicConnection::ProcessUdpPacket(const QuicSocketAddress& self_address,
                                      const QuicSocketAddress& peer_address,
                                      const QuicReceivedPacket& packet) {
  if (!connected_) {
    return;
  }
  QUIC_DVLOG(2) << ENDPOINT << "Received encrypted " << packet.length()
                << " bytes:" << std::endl
                << QuicTextUtils::HexDump(
                       QuicStringPiece(packet.data(), packet.length()));
  QUIC_BUG_IF(current_packet_data_ != nullptr)
      << "ProcessUdpPacket must not be called while processing a packet.";
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPacketReceived(self_address, peer_address, packet);
  }
  last_size_ = packet.length();
  current_packet_data_ = packet.data();

  last_packet_destination_address_ = self_address;
  last_packet_source_address_ = peer_address;
  if (!self_address_.IsInitialized()) {
    self_address_ = last_packet_destination_address_;
  }

  if (!direct_peer_address_.IsInitialized()) {
    direct_peer_address_ = last_packet_source_address_;
  }

  if (!effective_peer_address_.IsInitialized()) {
    const QuicSocketAddress effective_peer_addr =
        GetEffectivePeerAddressFromCurrentPacket();

    // effective_peer_address_ must be initialized at the beginning of the
    // first packet processed(here). If effective_peer_addr is uninitialized,
    // just set effective_peer_address_ to the direct peer address.
    effective_peer_address_ = effective_peer_addr.IsInitialized()
                                  ? effective_peer_addr
                                  : direct_peer_address_;
  }

  stats_.bytes_received += packet.length();
  ++stats_.packets_received;
  if (EnforceAntiAmplificationLimit()) {
    bytes_received_before_address_validation_ += last_size_;
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
                << time_of_last_received_packet_.ToDebuggingValue();

  ScopedPacketFlusher flusher(this);
  if (!framer_.ProcessPacket(packet)) {
    // If we are unable to decrypt this packet, it might be
    // because the CHLO or SHLO packet was lost.
    if (framer_.error() == QUIC_DECRYPTION_FAILURE &&
        !GetQuicRestartFlag(quic_framer_uses_undecryptable_upcall)) {
      ++stats_.undecryptable_packets_received;
      if (encryption_level_ != ENCRYPTION_FORWARD_SECURE &&
          undecryptable_packets_.size() < max_undecryptable_packets_) {
        QueueUndecryptablePacket(packet);
      } else if (debug_visitor_ != nullptr) {
        debug_visitor_->OnUndecryptablePacket();
      }
    } else if (GetQuicRestartFlag(quic_framer_uses_undecryptable_upcall)) {
      QUIC_RESTART_FLAG_COUNT_N(quic_framer_uses_undecryptable_upcall, 2, 7);
    }
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
  if (active_effective_peer_migration_type_ != NO_CHANGE &&
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
  DCHECK(!writer_->IsWriteBlocked());

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
  if (!session_decides_what_to_write()) {
    WritePendingRetransmissions();
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
  if (perspective_ == Perspective::IS_SERVER && self_address_.IsInitialized() &&
      last_packet_destination_address_.IsInitialized() &&
      self_address_ != last_packet_destination_address_) {
    // Allow change between pure IPv4 and equivalent mapped IPv4 address.
    if (self_address_.port() != last_packet_destination_address_.port() ||
        self_address_.host().Normalized() !=
            last_packet_destination_address_.host().Normalized()) {
      if (!visitor_->AllowSelfAddressChange()) {
        CloseConnection(
            QUIC_ERROR_MIGRATING_ADDRESS,
            "Self address migration is not supported at the server.",
            ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
        return false;
      }
    }
    self_address_ = last_packet_destination_address_;
  }

  if (PacketCanReplaceConnectionId(header, perspective_) &&
      server_connection_id_ != header.source_connection_id) {
    QUIC_DLOG(INFO) << ENDPOINT << "Replacing connection ID "
                    << server_connection_id_ << " with "
                    << header.source_connection_id;
    server_connection_id_ = header.source_connection_id;
    packet_generator_.SetServerConnectionId(server_connection_id_);
  }

  if (!ValidateReceivedPacketNumber(header.packet_number)) {
    return false;
  }

  if (!version_negotiated_) {
    if (perspective_ == Perspective::IS_CLIENT) {
      DCHECK(!header.version_flag || header.form != GOOGLE_QUIC_PACKET);
      if (!VersionHasIetfInvariantHeader(framer_.transport_version())) {
        // If the client gets a packet without the version flag from the server
        // it should stop sending version since the version negotiation is done.
        // IETF QUIC stops sending version once encryption level switches to
        // forward secure.
        packet_generator_.StopSendingVersion();
      }
      version_negotiated_ = true;
      visitor_->OnSuccessfulVersionNegotiation(version());
      if (debug_visitor_ != nullptr) {
        debug_visitor_->OnSuccessfulVersionNegotiation(version());
      }
    }
  }

  if (last_size_ > largest_received_packet_size_) {
    largest_received_packet_size_ = last_size_;
  }

  if (perspective_ == Perspective::IS_SERVER &&
      encryption_level_ == ENCRYPTION_INITIAL &&
      last_size_ > packet_generator_.GetCurrentMaxPacketLength()) {
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
  DCHECK(!writer_->IsWriteBlocked());

  if (pending_version_negotiation_packet_) {
    SendVersionNegotiationPacket(
        send_ietf_version_negotiation_packet_,
        send_version_negotiation_packet_with_prefixed_lengths_);
  }

  QUIC_CLIENT_HISTOGRAM_COUNTS("QuicSession.NumQueuedPacketsBeforeWrite",
                               queued_packets_.size(), 1, 1000, 50, "");
  while (!queued_packets_.empty()) {
    // WritePacket() can potentially clear all queued packets, so we need to
    // save the first queued packet to a local variable before calling it.
    SerializedPacket packet(std::move(queued_packets_.front()));
    queued_packets_.pop_front();

    const bool write_result = WritePacket(&packet);

    if (connected_ && !write_result) {
      // Write failed but connection is open, re-insert |packet| into the
      // front of the queue, it will be retried later.
      queued_packets_.emplace_front(std::move(packet));
      break;
    }

    delete[] packet.encrypted_buffer;
    ClearSerializedPacket(&packet);
    if (!connected_) {
      DCHECK(queued_packets_.empty()) << "Queued packets should have been "
                                         "cleared while closing connection";
      break;
    }

    // Continue to send the next packet in queue.
  }
}

void QuicConnection::WritePendingRetransmissions() {
  DCHECK(!session_decides_what_to_write());
  // Keep writing as long as there's a pending retransmission which can be
  // written.
  while (sent_packet_manager_.HasPendingRetransmissions() &&
         CanWrite(HAS_RETRANSMITTABLE_DATA)) {
    const QuicPendingRetransmission pending =
        sent_packet_manager_.NextPendingRetransmission();

    // Re-packetize the frames with a new packet number for retransmission.
    // Retransmitted packets use the same packet number length as the
    // original.
    // Flush the packet generator before making a new packet.
    // TODO(ianswett): Implement ReserializeAllFrames as a separate path that
    // does not require the creator to be flushed.
    // TODO(fayang): FlushAllQueuedFrames should only be called once, and should
    // be moved outside of the loop. Also, CanWrite is not checked after the
    // generator is flushed.
    {
      ScopedPacketFlusher flusher(this);
      packet_generator_.FlushAllQueuedFrames();
    }
    DCHECK(!packet_generator_.HasPendingFrames());
    char buffer[kMaxOutgoingPacketSize];
    packet_generator_.ReserializeAllFrames(pending, buffer,
                                           kMaxOutgoingPacketSize);
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

    if (!session_decides_what_to_write()) {
      DCHECK(sent_packet_manager_.HasPendingRetransmissions());
      WritePendingRetransmissions();
    }
  }
}

void QuicConnection::RetransmitUnackedPackets(
    TransmissionType retransmission_type) {
  sent_packet_manager_.RetransmitUnackedPackets(retransmission_type);

  WriteIfNotBlocked();
}

void QuicConnection::NeuterUnencryptedPackets() {
  sent_packet_manager_.NeuterUnencryptedPackets();
  // This may have changed the retransmission timer, so re-arm it.
  SetRetransmissionAlarm();
}

bool QuicConnection::ShouldGeneratePacket(
    HasRetransmittableData retransmittable,
    IsHandshake handshake) {
  // We should serialize handshake packets immediately to ensure that they
  // end up sent at the right encryption level.
  if (handshake == IS_HANDSHAKE) {
    if (LimitedByAmplificationFactor()) {
      // Server is constrained by the amplification restriction.
      QUIC_DVLOG(1) << ENDPOINT << "Constrained by amplification restriction";
      return false;
    }
    return true;
  }

  return CanWrite(retransmittable);
}

const QuicFrames QuicConnection::MaybeBundleAckOpportunistically() {
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
      << QuicUtils::EncryptionLevelToString(encryption_level_) << " ACK, "
      << (has_pending_ack ? "" : "!") << "has_pending_ack, stop_waiting_count_ "
      << stop_waiting_count_;
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

  if (session_decides_what_to_write() &&
      sent_packet_manager_.pending_timer_transmission_count() > 0) {
    // Force sending the retransmissions for HANDSHAKE, TLP, RTO, PROBING cases.
    return true;
  }

  if (HandleWriteBlocked()) {
    return false;
  }

  // Allow acks to be sent immediately.
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
    send_alarm_->Update(now + delay, QuicTime::Delta::FromMilliseconds(1));
    QUIC_DVLOG(1) << ENDPOINT << "Delaying sending " << delay.ToMilliseconds()
                  << "ms";
    return false;
  }
  return true;
}

bool QuicConnection::WritePacket(SerializedPacket* packet) {
  if (ShouldDiscardPacket(*packet)) {
    ++stats_.packets_discarded;
    return true;
  }
  if (sent_packet_manager_.GetLargestSentPacket().IsInitialized() &&
      packet->packet_number < sent_packet_manager_.GetLargestSentPacket()) {
    QUIC_BUG << "Attempt to write packet:" << packet->packet_number
             << " after:" << sent_packet_manager_.GetLargestSentPacket();
    QUIC_CLIENT_HISTOGRAM_COUNTS("QuicSession.NumQueuedPacketsAtOutOfOrder",
                                 queued_packets_.size(), 1, 1000, 50, "");
    CloseConnection(QUIC_INTERNAL_ERROR, "Packet written out of order.",
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return true;
  }
  // Termination packets are encrypted and saved, so don't exit early.
  const bool is_termination_packet = IsTerminationPacket(*packet);
  if (HandleWriteBlocked() && !is_termination_packet) {
    return false;
  }

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
    // This assures we won't try to write *forced* packets when blocked.
    // Return true to stop processing.
    if (HandleWriteBlocked()) {
      return true;
    }
  }

  DCHECK_LE(encrypted_length, kMaxOutgoingPacketSize);
  DCHECK_LE(encrypted_length, packet_generator_.GetCurrentMaxPacketLength());
  QUIC_DVLOG(1) << ENDPOINT << "Sending packet " << packet_number << " : "
                << (IsRetransmittable(*packet) == HAS_RETRANSMITTABLE_DATA
                        ? "data bearing "
                        : " ack only ")
                << ", encryption level: "
                << QuicUtils::EncryptionLevelToString(packet->encryption_level)
                << ", encrypted length:" << encrypted_length;
  QUIC_DVLOG(2) << ENDPOINT << "packet(" << packet_number << "): " << std::endl
                << QuicTextUtils::HexDump(QuicStringPiece(
                       packet->encrypted_buffer, encrypted_length));

  // Measure the RTT from before the write begins to avoid underestimating the
  // min_rtt_, especially in cases where the thread blocks or gets swapped out
  // during the WritePacket below.
  QuicTime packet_send_time = clock_->Now();
  if (supports_release_time_ && per_packet_options_ != nullptr) {
    QuicTime next_release_time = sent_packet_manager_.GetNextReleaseTime();
    QuicTime::Delta release_time_delay = QuicTime::Delta::Zero();
    QuicTime now = packet_send_time;
    if (next_release_time > now) {
      release_time_delay = next_release_time - now;
      // Set packet_send_time to the future to make the RTT estimation accurate.
      packet_send_time = next_release_time;
    }
    per_packet_options_->release_time_delay = release_time_delay;
  }
  WriteResult result = writer_->WritePacket(
      packet->encrypted_buffer, encrypted_length, self_address().host(),
      peer_address(), per_packet_options_);

  QUIC_HISTOGRAM_ENUM(
      "QuicConnection.WritePacketStatus", result.status,
      WRITE_STATUS_NUM_VALUES,
      "Status code returned by writer_->WritePacket() in QuicConnection.");

  if (IsWriteBlockedStatus(result.status)) {
    // Ensure the writer is still write blocked, otherwise QUIC may continue
    // trying to write when it will not be able to.
    DCHECK(writer_->IsWriteBlocked());
    visitor_->OnWriteBlocked();
    // If the socket buffers the data, then the packet should not
    // be queued and sent again, which would result in an unnecessary
    // duplicate packet being sent.  The helper must call OnCanWrite
    // when the write completes, and OnWriteError if an error occurs.
    if (result.status != WRITE_STATUS_BLOCKED_DATA_BUFFERED) {
      return false;
    }
  }

  // In some cases, an MTU probe can cause EMSGSIZE. This indicates that the
  // MTU discovery is permanently unsuccessful.
  if (IsMsgTooBig(result) && packet->retransmittable_frames.empty() &&
      packet->encrypted_length > long_term_mtu_) {
    mtu_discovery_target_ = 0;
    mtu_discovery_alarm_->Cancel();
    // The write failed, but the writer is not blocked, so return true.
    return true;
  }

  if (IsWriteError(result.status)) {
    OnWriteError(result.error_code);
    QUIC_LOG_FIRST_N(ERROR, 10)
        << ENDPOINT << "failed writing " << encrypted_length
        << " bytes from host " << self_address().host().ToString()
        << " to address " << peer_address().ToString() << " with error code "
        << result.error_code;
    return false;
  }

  if (debug_visitor_ != nullptr) {
    // Pass the write result to the visitor.
    debug_visitor_->OnPacketSent(*packet, packet->original_packet_number,
                                 packet->transmission_type, packet_send_time);
  }
  if (IsRetransmittable(*packet) == HAS_RETRANSMITTABLE_DATA) {
    if (!is_path_degrading_ && !path_degrading_alarm_->IsSet()) {
      // This is the first retransmittable packet on the working path.
      // Start the path degrading alarm to detect new path degrading.
      SetPathDegradingAlarm();
    }

    // Update |time_of_first_packet_sent_after_receiving_| if this is the
    // first packet sent after the last packet was received. If it were
    // updated on every sent packet, then sending into a black hole might
    // never timeout.
    if (time_of_first_packet_sent_after_receiving_ <
        time_of_last_received_packet_) {
      time_of_first_packet_sent_after_receiving_ = packet_send_time;
    }
  }

  MaybeSetMtuAlarm(packet_number);
  QUIC_DVLOG(1) << ENDPOINT << "time we began writing last sent packet: "
                << packet_send_time.ToDebuggingValue();

  if (EnforceAntiAmplificationLimit()) {
    // Include bytes sent even if they are not in flight.
    bytes_sent_before_address_validation_ += packet->encrypted_length;
  }

  const bool in_flight = sent_packet_manager_.OnPacketSent(
      packet, packet->original_packet_number, packet_send_time,
      packet->transmission_type, IsRetransmittable(*packet));

  if (in_flight || !retransmission_alarm_->IsSet()) {
    SetRetransmissionAlarm();
  }
  SetPingAlarm();

  // The packet number length must be updated after OnPacketSent, because it
  // may change the packet number length in packet.
  packet_generator_.UpdatePacketNumberLength(
      sent_packet_manager_.GetLeastUnacked(),
      sent_packet_manager_.EstimateMaxPacketsInFlight(max_packet_length()));

  stats_.bytes_sent += result.bytes_written;
  ++stats_.packets_sent;
  if (packet->transmission_type != NOT_RETRANSMISSION) {
    stats_.bytes_retransmitted += result.bytes_written;
    ++stats_.packets_retransmitted;
  }

  return true;
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

  if (HandleWriteBlocked()) {
    DCHECK_EQ(WRITE_STATUS_BLOCKED, result.status)
        << "Unexpected flush result:" << result;
    QUIC_DLOG(INFO) << ENDPOINT << "Write blocked in FlushPackets.";
    return;
  }

  if (IsWriteError(result.status)) {
    OnWriteError(result.error_code);
  }
}

bool QuicConnection::IsMsgTooBig(const WriteResult& result) {
  return (result.status == WRITE_STATUS_MSG_TOO_BIG) ||
         (IsWriteError(result.status) && result.error_code == QUIC_EMSGSIZE);
}

bool QuicConnection::ShouldDiscardPacket(const SerializedPacket& packet) {
  if (!connected_) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Not sending packet as connection is disconnected.";
    return true;
  }

  QuicPacketNumber packet_number = packet.packet_number;
  if (encryption_level_ == ENCRYPTION_FORWARD_SECURE &&
      packet.encryption_level == ENCRYPTION_INITIAL) {
    // Drop packets that are NULL encrypted since the peer won't accept them
    // anymore.
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Dropping NULL encrypted packet: " << packet_number
                    << " since the connection is forward secure.";
    return true;
  }

  return false;
}

void QuicConnection::OnWriteError(int error_code) {
  if (write_error_occurred_) {
    // A write error already occurred. The connection is being closed.
    return;
  }
  write_error_occurred_ = true;

  const std::string error_details = QuicStrCat(
      "Write failed with error: ", error_code, " (", strerror(error_code), ")");
  QUIC_LOG_FIRST_N(ERROR, 2) << ENDPOINT << error_details;
  switch (error_code) {
    case QUIC_EMSGSIZE:
      CloseConnection(QUIC_PACKET_WRITE_ERROR, error_details,
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      break;
    default:
      // We can't send an error as the socket is presumably borked.
      if (VersionHasIetfInvariantHeader(transport_version())) {
        QUIC_CODE_COUNT(quic_tear_down_local_connection_on_write_error_ietf);
      } else {
        QUIC_CODE_COUNT(
            quic_tear_down_local_connection_on_write_error_non_ietf);
      }
      CloseConnection(QUIC_PACKET_WRITE_ERROR, error_details,
                      ConnectionCloseBehavior::SILENT_CLOSE);
  }
}

char* QuicConnection::GetPacketBuffer() {
  return writer_->GetNextWriteLocation(self_address().host(), peer_address());
}

void QuicConnection::OnSerializedPacket(SerializedPacket* serialized_packet) {
  if (serialized_packet->encrypted_buffer == nullptr) {
    // We failed to serialize the packet, so close the connection.
    // Specify that the close is silent, that no packet be sent, so no infinite
    // loop here.
    // TODO(ianswett): This is actually an internal error, not an
    // encryption failure.
    if (VersionHasIetfInvariantHeader(transport_version())) {
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

  if (serialized_packet->retransmittable_frames.empty() &&
      !serialized_packet->original_packet_number.IsInitialized()) {
    // Increment consecutive_num_packets_with_no_retransmittable_frames_ if
    // this packet is a new transmission with no retransmittable frames.
    ++consecutive_num_packets_with_no_retransmittable_frames_;
  } else {
    consecutive_num_packets_with_no_retransmittable_frames_ = 0;
  }
  SendOrQueuePacket(serialized_packet);
}

void QuicConnection::OnUnrecoverableError(QuicErrorCode error,
                                          const std::string& error_details) {
  // The packet creator or generator encountered an unrecoverable error: tear
  // down local connection state immediately.
  if (VersionHasIetfInvariantHeader(transport_version())) {
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
    SetMaxPacketLength(packet_size);
  }
}

void QuicConnection::OnHandshakeComplete() {
  sent_packet_manager_.SetHandshakeConfirmed();
  // This may have changed the retransmission timer, so re-arm it.
  SetRetransmissionAlarm();
  // The client should immediately ack the SHLO to confirm the handshake is
  // complete with the server.
  if (perspective_ == Perspective::IS_CLIENT && ack_frame_updated()) {
    ack_alarm_->Update(clock_->ApproximateNow(), QuicTime::Delta::Zero());
  }
}

void QuicConnection::SendOrQueuePacket(SerializedPacket* packet) {
  // The caller of this function is responsible for checking CanWrite().
  if (packet->encrypted_buffer == nullptr) {
    QUIC_BUG << "packet.encrypted_buffer == nullptr in to SendOrQueuePacket";
    return;
  }
  // If there are already queued packets, queue this one immediately to ensure
  // it's written in sequence number order.
  if (!queued_packets_.empty() || !WritePacket(packet)) {
    // Take ownership of the underlying encrypted packet.
    packet->encrypted_buffer = CopyBuffer(*packet);
    queued_packets_.push_back(*packet);
    packet->retransmittable_frames.clear();
  }

  ClearSerializedPacket(packet);
}

void QuicConnection::OnPingTimeout() {
  if (!retransmission_alarm_->IsSet()) {
    visitor_->SendPing();
  }
}

void QuicConnection::SendAck() {
  DCHECK(!SupportsMultiplePacketNumberSpaces());
  QUIC_DVLOG(1) << ENDPOINT << "Sending an ACK proactively";
  QuicFrames frames;
  frames.push_back(GetUpdatedAckFrame());
  if (!no_stop_waiting_frames_) {
    QuicStopWaitingFrame stop_waiting;
    PopulateStopWaitingFrame(&stop_waiting);
    frames.push_back(QuicFrame(stop_waiting));
  }
  if (!packet_generator_.FlushAckFrame(frames)) {
    return;
  }
  ResetAckStates();
  if (consecutive_num_packets_with_no_retransmittable_frames_ <
      max_consecutive_num_packets_with_no_retransmittable_frames_) {
    return;
  }
  consecutive_num_packets_with_no_retransmittable_frames_ = 0;
  if (packet_generator_.HasRetransmittableFrames() ||
      visitor_->WillingAndAbleToWrite()) {
    // There are pending retransmittable frames.
    return;
  }

  visitor_->OnAckNeedsRetransmittableFrame();
}

void QuicConnection::OnPathDegradingTimeout() {
  is_path_degrading_ = true;
  visitor_->OnPathDegrading();
}

void QuicConnection::OnRetransmissionTimeout() {
  DCHECK(!sent_packet_manager_.unacked_packets().empty() ||
         (sent_packet_manager_.handshake_mode_disabled() &&
          !sent_packet_manager_.handshake_confirmed()));
  const QuicPacketNumber previous_created_packet_number =
      packet_generator_.packet_number();
  if (close_connection_after_five_rtos_ &&
      sent_packet_manager_.GetConsecutiveRtoCount() >= 4) {
    // Close on the 5th consecutive RTO, so after 4 previous RTOs have occurred.
    CloseConnection(QUIC_TOO_MANY_RTOS, "5 consecutive retransmission timeouts",
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  if (sent_packet_manager_.pto_enabled() && max_consecutive_ptos_ > 0 &&
      sent_packet_manager_.GetConsecutivePtoCount() >= max_consecutive_ptos_) {
    CloseConnection(QUIC_TOO_MANY_RTOS,
                    QuicStrCat(max_consecutive_ptos_ + 1,
                               "consecutive retransmission timeouts"),
                    ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  const auto retransmission_mode =
      sent_packet_manager_.OnRetransmissionTimeout();
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

  if (sent_packet_manager_.fix_rto_retransmission()) {
    if (packet_generator_.packet_number() == previous_created_packet_number &&
        (retransmission_mode == QuicSentPacketManager::TLP_MODE ||
         retransmission_mode == QuicSentPacketManager::RTO_MODE ||
         retransmission_mode == QuicSentPacketManager::PTO_MODE) &&
        !visitor_->WillingAndAbleToWrite()) {
      // Send PING if timer fires in RTO or PTO mode but there is no data to
      // send.
      // When TLP fires, either new data or tail loss probe should be sent.
      // There is corner case where TLP fires after RTO because packets get
      // acked. Two packets are marked RTO_RETRANSMITTED, but the first packet
      // is retransmitted as two packets because of packet number length
      // increases (please see QuicConnectionTest.RtoPacketAsTwo).
      QUIC_BUG_IF(retransmission_mode == QuicSentPacketManager::TLP_MODE &&
                  stats_.rto_count == 0);
      DCHECK_LT(0u, sent_packet_manager_.pending_timer_transmission_count());
      visitor_->SendPing();
    }
    if (retransmission_mode == QuicSentPacketManager::PTO_MODE) {
      sent_packet_manager_.AdjustPendingTimerTransmissions();
    }
    if (retransmission_mode != QuicSentPacketManager::LOSS_MODE) {
      // When timer fires in TLP or RTO mode, ensure 1) at least one packet is
      // created, or there is data to send and available credit (such that
      // packets will be sent eventually).
      QUIC_BUG_IF(
          packet_generator_.packet_number() == previous_created_packet_number &&
          (!visitor_->WillingAndAbleToWrite() ||
           sent_packet_manager_.pending_timer_transmission_count() == 0u))
          << "retransmission_mode: " << retransmission_mode
          << ", packet_number: " << packet_generator_.packet_number()
          << ", session has data to write: "
          << visitor_->WillingAndAbleToWrite()
          << ", writer is blocked: " << writer_->IsWriteBlocked()
          << ", pending_timer_transmission_count: "
          << sent_packet_manager_.pending_timer_transmission_count();
    }
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
  packet_generator_.SetEncrypter(level, std::move(encrypter));
}

void QuicConnection::SetDiversificationNonce(
    const DiversificationNonce& nonce) {
  DCHECK_EQ(Perspective::IS_SERVER, perspective_);
  packet_generator_.SetDiversificationNonce(nonce);
}

void QuicConnection::SetDefaultEncryptionLevel(EncryptionLevel level) {
  QUIC_DVLOG(1) << ENDPOINT << "Setting default encryption level from "
                << QuicUtils::EncryptionLevelToString(encryption_level_)
                << " to " << QuicUtils::EncryptionLevelToString(level);
  if (level != encryption_level_ && packet_generator_.HasPendingFrames()) {
    // Flush all queued frames when encryption level changes.
    ScopedPacketFlusher flusher(this);
    packet_generator_.FlushAllQueuedFrames();
  }
  encryption_level_ = level;
  packet_generator_.set_encryption_level(level);
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
  framer_.InstallDecrypter(level, std::move(decrypter));
  if (!undecryptable_packets_.empty() &&
      !process_undecryptable_packets_alarm_->IsSet()) {
    process_undecryptable_packets_alarm_->Set(clock_->ApproximateNow());
  }
}

void QuicConnection::RemoveDecrypter(EncryptionLevel level) {
  framer_.RemoveDecrypter(level);
}

const QuicDecrypter* QuicConnection::decrypter() const {
  return framer_.decrypter();
}

const QuicDecrypter* QuicConnection::alternative_decrypter() const {
  return framer_.alternative_decrypter();
}

void QuicConnection::QueueUndecryptablePacket(
    const QuicEncryptedPacket& packet) {
  if (GetQuicRestartFlag(quic_framer_uses_undecryptable_upcall)) {
    QUIC_RESTART_FLAG_COUNT_N(quic_framer_uses_undecryptable_upcall, 3, 7);
    for (const auto& saved_packet : undecryptable_packets_) {
      if (packet.data() == saved_packet->data() &&
          packet.length() == saved_packet->length()) {
        QUIC_DVLOG(1) << ENDPOINT << "Not queueing known undecryptable packet";
        return;
      }
    }
  }
  QUIC_DVLOG(1) << ENDPOINT << "Queueing undecryptable packet.";
  undecryptable_packets_.push_back(packet.Clone());
}

void QuicConnection::MaybeProcessUndecryptablePackets() {
  process_undecryptable_packets_alarm_->Cancel();

  if (undecryptable_packets_.empty() ||
      encryption_level_ == ENCRYPTION_INITIAL) {
    return;
  }

  while (connected_ && !undecryptable_packets_.empty()) {
    // Making sure there is no pending frames when processing next undecrypted
    // packet because the queued ack frame may change.
    packet_generator_.FlushAllQueuedFrames();
    if (!connected_) {
      return;
    }
    QUIC_DVLOG(1) << ENDPOINT << "Attempting to process undecryptable packet";
    QuicEncryptedPacket* packet = undecryptable_packets_.front().get();
    if (!framer_.ProcessPacket(*packet) &&
        framer_.error() == QUIC_DECRYPTION_FAILURE) {
      QUIC_DVLOG(1) << ENDPOINT << "Unable to process undecryptable packet...";
      break;
    }
    QUIC_DVLOG(1) << ENDPOINT << "Processed undecryptable packet!";
    ++stats_.packets_processed;
    undecryptable_packets_.pop_front();
  }

  // Once forward secure encryption is in use, there will be no
  // new keys installed and hence any undecryptable packets will
  // never be able to be decrypted.
  if (encryption_level_ == ENCRYPTION_FORWARD_SECURE) {
    if (debug_visitor_ != nullptr) {
      // TODO(rtenneti): perhaps more efficient to pass the number of
      // undecryptable packets as the argument to OnUndecryptablePacket so that
      // we just need to call OnUndecryptablePacket once?
      for (size_t i = 0; i < undecryptable_packets_.size(); ++i) {
        debug_visitor_->OnUndecryptablePacket();
      }
    }
    undecryptable_packets_.clear();
  }
}

void QuicConnection::QueueCoalescedPacket(const QuicEncryptedPacket& packet) {
  QUIC_DVLOG(1) << ENDPOINT << "Queueing coalesced packet.";
  coalesced_packets_.push_back(packet.Clone());
}

void QuicConnection::MaybeProcessCoalescedPackets() {
  bool processed = false;
  while (connected_ && !coalesced_packets_.empty()) {
    // Making sure there are no pending frames when processing the next
    // coalesced packet because the queued ack frame may change.
    packet_generator_.FlushAllQueuedFrames();
    if (!connected_) {
      return;
    }

    std::unique_ptr<QuicEncryptedPacket> packet =
        std::move(coalesced_packets_.front());
    coalesced_packets_.pop_front();

    QUIC_DVLOG(1) << ENDPOINT << "Processing coalesced packet";
    if (framer_.ProcessPacket(*packet)) {
      processed = true;
    } else {
      // If we are unable to decrypt this packet, it might be
      // because the CHLO or SHLO packet was lost.
      if (framer_.error() == QUIC_DECRYPTION_FAILURE &&
          !GetQuicRestartFlag(quic_framer_uses_undecryptable_upcall)) {
        ++stats_.undecryptable_packets_received;
        if (encryption_level_ != ENCRYPTION_FORWARD_SECURE &&
            undecryptable_packets_.size() < max_undecryptable_packets_) {
          QueueUndecryptablePacket(*packet);
        } else if (debug_visitor_ != nullptr) {
          debug_visitor_->OnUndecryptablePacket();
        }
      } else if (GetQuicRestartFlag(quic_framer_uses_undecryptable_upcall)) {
        QUIC_RESTART_FLAG_COUNT_N(quic_framer_uses_undecryptable_upcall, 4, 7);
      }
    }
  }
  if (processed) {
    MaybeProcessUndecryptablePackets();
  }
}

void QuicConnection::CloseConnection(
    QuicErrorCode error,
    const std::string& error_details,
    ConnectionCloseBehavior connection_close_behavior) {
  DCHECK(!error_details.empty());
  if (!connected_) {
    QUIC_DLOG(INFO) << "Connection is already closed.";
    return;
  }

  QUIC_DLOG(INFO) << ENDPOINT << "Closing connection: " << connection_id()
                  << ", with error: " << QuicErrorCodeToString(error) << " ("
                  << error << "), and details:  " << error_details;

  if (connection_close_behavior != ConnectionCloseBehavior::SILENT_CLOSE) {
    SendConnectionClosePacket(error, error_details);
  }

  TearDownLocalConnectionState(error, error_details,
                               ConnectionCloseSource::FROM_SELF);
}

void QuicConnection::SendConnectionClosePacket(QuicErrorCode error,
                                               const std::string& details) {
  QUIC_DLOG(INFO) << ENDPOINT << "Sending connection close packet.";
  SetDefaultEncryptionLevel(GetConnectionCloseEncryptionLevel());
  ClearQueuedPackets();
  // If there was a packet write error, write the smallest close possible.
  ScopedPacketFlusher flusher(this);
  // When multiple packet number spaces is supported, an ACK frame will be
  // bundled when connection is not write blocked.
  if (!SupportsMultiplePacketNumberSpaces() &&
      error != QUIC_PACKET_WRITE_ERROR &&
      !GetUpdatedAckFrame().ack_frame->packets.Empty()) {
    SendAck();
  }
  QuicConnectionCloseFrame* frame;
  if (VersionHasIetfQuicFrames(transport_version())) {
    QuicErrorCodeToIetfMapping mapping =
        QuicErrorCodeToTransportErrorCode(error);
    if (mapping.is_transport_close_) {
      frame = new QuicConnectionCloseFrame(
          error, details, mapping.transport_error_code_,
          framer_.current_received_frame_type());
    } else {
      // Maps to an application close.
      frame = new QuicConnectionCloseFrame(error, details,
                                           mapping.application_error_code_);
    }
  } else {
    frame = new QuicConnectionCloseFrame(error, details);
  }
  packet_generator_.ConsumeRetransmittableControlFrame(QuicFrame(frame));
  packet_generator_.FlushAllQueuedFrames();
  if (GetQuicReloadableFlag(quic_clear_queued_packets_on_connection_close)) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_clear_queued_packets_on_connection_close);
    ClearQueuedPackets();
  }
}

void QuicConnection::TearDownLocalConnectionState(
    QuicErrorCode error,
    const std::string& error_details,
    ConnectionCloseSource source) {
  QuicConnectionCloseFrame frame(error, error_details);
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
  DCHECK(visitor_ != nullptr);
  visitor_->OnConnectionClosed(frame, source);
  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnConnectionClosed(frame, source);
  }
  // Cancel the alarms so they don't trigger any action now that the
  // connection is closed.
  CancelAllAlarms();
}

void QuicConnection::CancelAllAlarms() {
  QUIC_DVLOG(1) << "Cancelling all QuicConnection alarms.";

  ack_alarm_->Cancel();
  ping_alarm_->Cancel();
  retransmission_alarm_->Cancel();
  send_alarm_->Cancel();
  timeout_alarm_->Cancel();
  mtu_discovery_alarm_->Cancel();
  path_degrading_alarm_->Cancel();
  process_undecryptable_packets_alarm_->Cancel();
}

QuicByteCount QuicConnection::max_packet_length() const {
  return packet_generator_.GetCurrentMaxPacketLength();
}

void QuicConnection::SetMaxPacketLength(QuicByteCount length) {
  long_term_mtu_ = length;
  packet_generator_.SetMaxPacketLength(GetLimitedMaxPacketSize(length));
}

bool QuicConnection::HasQueuedData() const {
  return pending_version_negotiation_packet_ || !queued_packets_.empty() ||
         packet_generator_.HasPendingFrames();
}

bool QuicConnection::CanWriteStreamData() {
  // Don't write stream data if there are negotiation or queued data packets
  // to send. Otherwise, continue and bundle as many frames as possible.
  if (pending_version_negotiation_packet_ || !queued_packets_.empty()) {
    return false;
  }

  IsHandshake pending_handshake =
      visitor_->HasPendingHandshake() ? IS_HANDSHAKE : NOT_HANDSHAKE;
  // Sending queued packets may have caused the socket to become write blocked,
  // or the congestion manager to prohibit sending.  If we've sent everything
  // we had queued and we're still not blocked, let the visitor know it can
  // write more.
  return ShouldGeneratePacket(HAS_RETRANSMITTABLE_DATA, pending_handshake);
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
  handshake_timeout_ = handshake_timeout;
  idle_network_timeout_ = idle_timeout;

  SetTimeoutAlarm();
}

void QuicConnection::CheckForTimeout() {
  QuicTime now = clock_->ApproximateNow();
  QuicTime time_of_last_packet =
      std::max(time_of_last_received_packet_,
               time_of_first_packet_sent_after_receiving_);

  // |delta| can be < 0 as |now| is approximate time but |time_of_last_packet|
  // is accurate time. However, this should not change the behavior of
  // timeout handling.
  QuicTime::Delta idle_duration = now - time_of_last_packet;
  QUIC_DVLOG(1) << ENDPOINT << "last packet "
                << time_of_last_packet.ToDebuggingValue()
                << " now:" << now.ToDebuggingValue()
                << " idle_duration:" << idle_duration.ToMicroseconds()
                << " idle_network_timeout: "
                << idle_network_timeout_.ToMicroseconds();
  if (idle_duration >= idle_network_timeout_) {
    const std::string error_details = "No recent network activity.";
    QUIC_DVLOG(1) << ENDPOINT << error_details;
    if ((sent_packet_manager_.GetConsecutiveTlpCount() > 0 ||
         sent_packet_manager_.GetConsecutiveRtoCount() > 0 ||
         visitor_->ShouldKeepConnectionAlive())) {
      CloseConnection(QUIC_NETWORK_IDLE_TIMEOUT, error_details,
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    } else {
      CloseConnection(QUIC_NETWORK_IDLE_TIMEOUT, error_details,
                      idle_timeout_connection_close_behavior_);
    }
    return;
  }

  if (!handshake_timeout_.IsInfinite()) {
    QuicTime::Delta connected_duration = now - stats_.connection_creation_time;
    QUIC_DVLOG(1) << ENDPOINT
                  << "connection time: " << connected_duration.ToMicroseconds()
                  << " handshake timeout: "
                  << handshake_timeout_.ToMicroseconds();
    if (connected_duration >= handshake_timeout_) {
      const std::string error_details = "Handshake timeout expired.";
      QUIC_DVLOG(1) << ENDPOINT << error_details;
      CloseConnection(QUIC_HANDSHAKE_TIMEOUT, error_details,
                      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
      return;
    }
  }

  SetTimeoutAlarm();
}

void QuicConnection::SetTimeoutAlarm() {
  QuicTime time_of_last_packet =
      std::max(time_of_last_received_packet_,
               time_of_first_packet_sent_after_receiving_);

  QuicTime deadline = time_of_last_packet + idle_network_timeout_;
  if (!handshake_timeout_.IsInfinite()) {
    deadline = std::min(deadline,
                        stats_.connection_creation_time + handshake_timeout_);
  }

  timeout_alarm_->Update(deadline, QuicTime::Delta::Zero());
}

void QuicConnection::SetPingAlarm() {
  if (perspective_ == Perspective::IS_SERVER) {
    // Only clients send pings to avoid NATs from timing out.
    return;
  }
  if (!visitor_->ShouldKeepConnectionAlive()) {
    ping_alarm_->Cancel();
    // Don't send a ping unless the application (ie: HTTP/3) says to, usually
    // because it is expecting a response from the server.
    return;
  }
  if (retransmittable_on_wire_timeout_.IsInfinite() ||
      sent_packet_manager_.HasInFlightPackets()) {
    // Extend the ping alarm.
    ping_alarm_->Update(clock_->ApproximateNow() + ping_timeout_,
                        QuicTime::Delta::FromSeconds(1));
    return;
  }
  DCHECK_LT(retransmittable_on_wire_timeout_, ping_timeout_);
  // If it's already set to an earlier time, then don't update it.
  if (ping_alarm_->IsSet() &&
      ping_alarm_->deadline() <
          clock_->ApproximateNow() + retransmittable_on_wire_timeout_) {
    return;
  }
  // Use a shorter timeout if there are open streams, but nothing on the wire.
  ping_alarm_->Update(
      clock_->ApproximateNow() + retransmittable_on_wire_timeout_,
      QuicTime::Delta::FromMilliseconds(1));
}

void QuicConnection::SetRetransmissionAlarm() {
  if (packet_generator_.PacketFlusherAttached()) {
    pending_retransmission_alarm_ = true;
    return;
  }
  if (LimitedByAmplificationFactor()) {
    // Do not set retransmission timer if connection is anti-amplification limit
    // throttled. Otherwise, nothing can be sent when timer fires.
    retransmission_alarm_->Cancel();
    return;
  }

  retransmission_alarm_->Update(sent_packet_manager_.GetRetransmissionTime(),
                                QuicTime::Delta::FromMilliseconds(1));
}

void QuicConnection::SetPathDegradingAlarm() {
  if (perspective_ == Perspective::IS_SERVER) {
    return;
  }
  const QuicTime::Delta delay = sent_packet_manager_.GetPathDegradingDelay();
  path_degrading_alarm_->Update(clock_->ApproximateNow() + delay,
                                QuicTime::Delta::FromMilliseconds(1));
}

void QuicConnection::MaybeSetMtuAlarm(QuicPacketNumber sent_packet_number) {
  // Do not set the alarm if the target size is less than the current size.
  // This covers the case when |mtu_discovery_target_| is at its default value,
  // zero.
  if (mtu_discovery_target_ <= max_packet_length()) {
    return;
  }

  if (mtu_probe_count_ >= kMtuDiscoveryAttempts) {
    return;
  }

  if (mtu_discovery_alarm_->IsSet()) {
    return;
  }

  if (sent_packet_number >= next_mtu_probe_at_) {
    // Use an alarm to send the MTU probe to ensure that no ScopedPacketFlushers
    // are active.
    mtu_discovery_alarm_->Set(clock_->ApproximateNow());
  }
}

void QuicConnection::MaybeSetAckAlarmTo(QuicTime time) {
  if (!ack_alarm_->IsSet() || ack_alarm_->deadline() > time) {
    ack_alarm_->Update(time, QuicTime::Delta::Zero());
  }
}

QuicConnection::ScopedPacketFlusher::ScopedPacketFlusher(
    QuicConnection* connection)
    : connection_(connection),
      flush_and_set_pending_retransmission_alarm_on_delete_(false) {
  if (connection_ == nullptr) {
    return;
  }

  if (!connection_->packet_generator_.PacketFlusherAttached()) {
    flush_and_set_pending_retransmission_alarm_on_delete_ = true;
    connection->packet_generator_.AttachPacketFlusher();
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
      } else {
        connection_->MaybeSetAckAlarmTo(ack_timeout);
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
    connection_->packet_generator_.Flush();
    connection_->FlushPackets();
    if (connection_->session_decides_what_to_write()) {
      // Reset transmission type.
      connection_->SetTransmissionType(NOT_RETRANSMISSION);
    }

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
  DCHECK_EQ(flush_and_set_pending_retransmission_alarm_on_delete_,
            !connection_->packet_generator_.PacketFlusherAttached());
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

bool QuicConnection::IsTerminationPacket(const SerializedPacket& packet) {
  if (packet.retransmittable_frames.empty()) {
    return false;
  }
  for (const QuicFrame& frame : packet.retransmittable_frames) {
    if (frame.type == CONNECTION_CLOSE_FRAME) {
      return true;
    }
  }
  return false;
}

void QuicConnection::SetMtuDiscoveryTarget(QuicByteCount target) {
  mtu_discovery_target_ = GetLimitedMaxPacketSize(target);
}

QuicByteCount QuicConnection::GetLimitedMaxPacketSize(
    QuicByteCount suggested_max_packet_size) {
  if (!peer_address_.IsInitialized()) {
    QUIC_BUG << "Attempted to use a connection without a valid peer address";
    return suggested_max_packet_size;
  }

  const QuicByteCount writer_limit = writer_->GetMaxPacketSize(peer_address());

  QuicByteCount max_packet_size = suggested_max_packet_size;
  if (max_packet_size > writer_limit) {
    max_packet_size = writer_limit;
  }
  if (max_packet_size > kMaxOutgoingPacketSize) {
    max_packet_size = kMaxOutgoingPacketSize;
  }
  return max_packet_size;
}

void QuicConnection::SendMtuDiscoveryPacket(QuicByteCount target_mtu) {
  // Currently, this limit is ensured by the caller.
  DCHECK_EQ(target_mtu, GetLimitedMaxPacketSize(target_mtu));

  // Send the probe.
  packet_generator_.GenerateMtuDiscoveryPacket(target_mtu);
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
  DCHECK(peer_address.IsInitialized());
  if (!connected_) {
    QUIC_BUG << "Not sending connectivity probing packet as connection is "
             << "disconnected.";
    return false;
  }
  if (perspective_ == Perspective::IS_SERVER && probing_writer == nullptr) {
    // Server can use default packet writer to write packet.
    probing_writer = writer_;
  }
  DCHECK(probing_writer);

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

  OwningSerializedPacketPointer probing_packet;
  if (!VersionHasIetfQuicFrames(transport_version())) {
    // Non-IETF QUIC, generate a padded ping regardless of whether this is a
    // request or a response.
    probing_packet = packet_generator_.SerializeConnectivityProbingPacket();
  } else {
    if (is_response) {
      // Respond using IETF QUIC PATH_RESPONSE frame
      if (IsCurrentPacketConnectivityProbing()) {
        // Pad the response if the request was a google connectivity probe
        // (padded).
        probing_packet =
            packet_generator_.SerializePathResponseConnectivityProbingPacket(
                received_path_challenge_payloads_, /* is_padded = */ true);
        received_path_challenge_payloads_.clear();
      } else {
        // Do not pad the response if the path challenge was not a google
        // connectivity probe.
        probing_packet =
            packet_generator_.SerializePathResponseConnectivityProbingPacket(
                received_path_challenge_payloads_,
                /* is_padded = */ false);
        received_path_challenge_payloads_.clear();
      }
    } else {
      // Request using IETF QUIC PATH_CHALLENGE frame
      transmitted_connectivity_probe_payload_ =
          QuicMakeUnique<QuicPathFrameBuffer>();
      probing_packet =
          packet_generator_.SerializePathChallengeConnectivityProbingPacket(
              transmitted_connectivity_probe_payload_.get());
      if (!probing_packet) {
        transmitted_connectivity_probe_payload_ = nullptr;
      }
    }
  }

  DCHECK_EQ(IsRetransmittable(*probing_packet), NO_RETRANSMITTABLE_DATA);

  const QuicTime packet_send_time = clock_->Now();
  QUIC_DVLOG(2) << ENDPOINT
                << "Sending path probe packet for server connection ID "
                << server_connection_id_ << std::endl
                << QuicTextUtils::HexDump(
                       QuicStringPiece(probing_packet->encrypted_buffer,
                                       probing_packet->encrypted_length));
  WriteResult result = probing_writer->WritePacket(
      probing_packet->encrypted_buffer, probing_packet->encrypted_length,
      self_address().host(), peer_address, per_packet_options_);

  // If using a batch writer and the probing packet is buffered, flush it.
  if (probing_writer->IsBatchMode() && result.status == WRITE_STATUS_OK &&
      result.bytes_written == 0) {
    result = probing_writer->Flush();
  }

  if (IsWriteError(result.status)) {
    // Write error for any connectivity probe should not affect the connection
    // as it is sent on a different path.
    QUIC_DLOG(INFO) << ENDPOINT << "Write probing packet failed with error = "
                    << result.error_code;
    return false;
  }

  if (debug_visitor_ != nullptr) {
    debug_visitor_->OnPacketSent(
        *probing_packet, probing_packet->original_packet_number,
        probing_packet->transmission_type, packet_send_time);
  }

  // Call OnPacketSent regardless of the write result.
  sent_packet_manager_.OnPacketSent(
      probing_packet.get(), probing_packet->original_packet_number,
      packet_send_time, probing_packet->transmission_type,
      NO_RETRANSMITTABLE_DATA);

  if (IsWriteBlockedStatus(result.status)) {
    if (probing_writer == writer_) {
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

void QuicConnection::DiscoverMtu() {
  DCHECK(!mtu_discovery_alarm_->IsSet());

  // Check if the MTU has been already increased.
  if (mtu_discovery_target_ <= max_packet_length()) {
    return;
  }

  // Calculate the packet number of the next probe *before* sending the current
  // one.  Otherwise, when SendMtuDiscoveryPacket() is called,
  // MaybeSetMtuAlarm() will not realize that the probe has been just sent, and
  // will reschedule this probe again.
  packets_between_mtu_probes_ *= 2;
  next_mtu_probe_at_ = sent_packet_manager_.GetLargestSentPacket() +
                       packets_between_mtu_probes_ + 1;
  ++mtu_probe_count_;

  QUIC_DVLOG(2) << "Sending a path MTU discovery packet #" << mtu_probe_count_;
  SendMtuDiscoveryPacket(mtu_discovery_target_);

  DCHECK(!mtu_discovery_alarm_->IsSet());
}

void QuicConnection::OnEffectivePeerMigrationValidated() {
  if (active_effective_peer_migration_type_ == NO_CHANGE) {
    QUIC_BUG << "No migration underway.";
    return;
  }
  highest_packet_sent_before_effective_peer_migration_.Clear();
  active_effective_peer_migration_type_ = NO_CHANGE;
}

void QuicConnection::StartEffectivePeerMigration(AddressChangeType type) {
  // TODO(fayang): Currently, all peer address change type are allowed. Need to
  // add a method ShouldAllowPeerAddressChange(PeerAddressChangeType type) to
  // determine whether |type| is allowed.
  if (type == NO_CHANGE) {
    QUIC_BUG << "EffectivePeerMigration started without address change.";
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Effective peer's ip:port changed from "
                  << effective_peer_address_.ToString() << " to "
                  << GetEffectivePeerAddressFromCurrentPacket().ToString()
                  << ", address change type is " << type
                  << ", migrating connection.";

  highest_packet_sent_before_effective_peer_migration_ =
      sent_packet_manager_.GetLargestSentPacket();
  effective_peer_address_ = GetEffectivePeerAddressFromCurrentPacket();
  active_effective_peer_migration_type_ = type;

  // TODO(wub): Move these calls to OnEffectivePeerMigrationValidated.
  OnConnectionMigration(type);
}

void QuicConnection::OnConnectionMigration(AddressChangeType addr_change_type) {
  visitor_->OnConnectionMigration(addr_change_type);
  sent_packet_manager_.OnConnectionMigration(addr_change_type);
}

bool QuicConnection::IsCurrentPacketConnectivityProbing() const {
  return is_current_packet_connectivity_probing_;
}

bool QuicConnection::ack_frame_updated() const {
  return uber_received_packet_manager_.IsAckFrameUpdated();
}

QuicStringPiece QuicConnection::GetCurrentPacket() {
  if (current_packet_data_ == nullptr) {
    return QuicStringPiece();
  }
  return QuicStringPiece(current_packet_data_, last_size_);
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
  DCHECK(fill_up_link_during_probing_);

  // Don't send probing retransmissions until the handshake has completed.
  if (!sent_packet_manager_.handshake_confirmed() ||
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
  if (session_decides_what_to_write() && probing_retransmission_pending_) {
    return;
  }

  bool application_limited =
      queued_packets_.empty() &&
      !sent_packet_manager_.HasPendingRetransmissions() &&
      !visitor_->WillingAndAbleToWrite();

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

void QuicConnection::UpdatePacketContent(PacketContent type) {
  if (current_packet_content_ == NOT_PADDED_PING) {
    // We have already learned the current packet is not a connectivity
    // probing packet. Peer migration should have already been started earlier
    // if needed.
    return;
  }

  if (type == NO_FRAMES_RECEIVED) {
    return;
  }

  if (type == FIRST_FRAME_IS_PING) {
    if (current_packet_content_ == NO_FRAMES_RECEIVED) {
      current_packet_content_ = FIRST_FRAME_IS_PING;
      return;
    }
  }

  // In Google QUIC we look for a packet with just a PING and PADDING.
  // For IETF QUIC, the packet must consist of just a PATH_CHALLENGE frame,
  // followed by PADDING. If the condition is met, mark things as
  // connectivity-probing, causing later processing to generate the correct
  // response.
  if (type == SECOND_FRAME_IS_PADDING &&
      current_packet_content_ == FIRST_FRAME_IS_PING) {
    current_packet_content_ = SECOND_FRAME_IS_PADDING;
    if (perspective_ == Perspective::IS_SERVER) {
      is_current_packet_connectivity_probing_ =
          current_effective_peer_migration_type_ != NO_CHANGE;
    } else {
      is_current_packet_connectivity_probing_ =
          (last_packet_source_address_ != peer_address_) ||
          (last_packet_destination_address_ != self_address_);
    }
    return;
  }

  current_packet_content_ = NOT_PADDED_PING;
  if (GetLargestReceivedPacket().IsInitialized() &&
      last_header_.packet_number == GetLargestReceivedPacket()) {
    direct_peer_address_ = last_packet_source_address_;
    if (current_effective_peer_migration_type_ != NO_CHANGE) {
      // Start effective peer migration immediately when the current packet is
      // confirmed not a connectivity probing packet.
      // TODO(fayang): When multiple packet number spaces is supported, only
      // start peer migration for the application data.
      StartEffectivePeerMigration(current_effective_peer_migration_type_);
    }
  }
  current_effective_peer_migration_type_ = NO_CHANGE;
}

void QuicConnection::MaybeEnableSessionDecidesWhatToWrite() {
  // Only enable session decides what to write code path for version 42+,
  // because it needs the receiver to allow receiving overlapping stream data.
  const bool enable_session_decides_what_to_write =
      transport_version() > QUIC_VERSION_39;
  sent_packet_manager_.SetSessionDecideWhatToWrite(
      enable_session_decides_what_to_write);
  if (version().SupportsAntiAmplificationLimit()) {
    sent_packet_manager_.DisableHandshakeMode();
  }
  packet_generator_.SetCanSetTransmissionType(
      enable_session_decides_what_to_write);
}

void QuicConnection::PostProcessAfterAckFrame(bool send_stop_waiting,
                                              bool acked_new_packet) {
  if (no_stop_waiting_frames_) {
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
  MaybeSetPathDegradingAlarm(acked_new_packet);

  if (send_stop_waiting) {
    ++stop_waiting_count_;
  } else {
    stop_waiting_count_ = 0;
  }
}

void QuicConnection::MaybeSetPathDegradingAlarm(bool acked_new_packet) {
  if (!sent_packet_manager_.HasInFlightPackets()) {
    // There are no retransmittable packets on the wire, so it's impossible to
    // say if the connection has degraded.
    path_degrading_alarm_->Cancel();
  } else if (acked_new_packet) {
    // A previously-unacked packet has been acked, which means forward progress
    // has been made. Unset |is_path_degrading| if the path was considered as
    // degrading previously. Set/update the path degrading alarm.
    is_path_degrading_ = false;
    SetPathDegradingAlarm();
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
  packet_generator_.SetTransmissionType(type);
}

bool QuicConnection::session_decides_what_to_write() const {
  return sent_packet_manager_.session_decides_what_to_write();
}

void QuicConnection::UpdateReleaseTimeIntoFuture() {
  DCHECK(supports_release_time_);

  release_time_into_future_ = std::max(
      QuicTime::Delta::FromMilliseconds(kMinReleaseTimeIntoFutureMs),
      std::min(
          QuicTime::Delta::FromMilliseconds(
              GetQuicFlag(FLAGS_quic_max_pace_time_into_future_ms)),
          sent_packet_manager_.GetRttStats()->SmoothedOrInitialRtt() *
              GetQuicFlag(FLAGS_quic_pace_time_into_future_srtt_fraction)));
}

void QuicConnection::ResetAckStates() {
  ack_alarm_->Cancel();
  stop_waiting_count_ = 0;
  uber_received_packet_manager_.ResetAckStates(encryption_level_);
}

MessageStatus QuicConnection::SendMessage(QuicMessageId message_id,
                                          QuicMemSliceSpan message) {
  if (!VersionSupportsMessageFrames(transport_version())) {
    QUIC_BUG << "MESSAGE frame is not supported for version "
             << transport_version();
    return MESSAGE_STATUS_UNSUPPORTED;
  }
  if (message.total_length() > GetCurrentLargestMessagePayload()) {
    return MESSAGE_STATUS_TOO_LARGE;
  }
  if (!CanWrite(HAS_RETRANSMITTABLE_DATA)) {
    return MESSAGE_STATUS_BLOCKED;
  }
  ScopedPacketFlusher flusher(this);
  return packet_generator_.AddMessageFrame(message_id, message);
}

QuicPacketLength QuicConnection::GetCurrentLargestMessagePayload() const {
  return packet_generator_.GetCurrentLargestMessagePayload();
}

QuicPacketLength QuicConnection::GetGuaranteedLargestMessagePayload() const {
  return packet_generator_.GetGuaranteedLargestMessagePayload();
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
  if (sent_packet_manager_.handshake_confirmed()) {
    // A forward secure packet has been received.
    QUIC_BUG_IF(encryption_level_ != ENCRYPTION_FORWARD_SECURE)
        << ENDPOINT << "Unexpected connection close encryption level "
        << QuicUtils::EncryptionLevelToString(encryption_level_);
    return ENCRYPTION_FORWARD_SECURE;
  }
  if (framer_.HasEncrypterOfEncryptionLevel(ENCRYPTION_ZERO_RTT)) {
    if (encryption_level_ != ENCRYPTION_ZERO_RTT) {
      if (VersionHasIetfInvariantHeader(transport_version())) {
        QUIC_CODE_COUNT(quic_wrong_encryption_level_connection_close_ietf);
      } else {
        QUIC_CODE_COUNT(quic_wrong_encryption_level_connection_close);
      }
    }
    return ENCRYPTION_ZERO_RTT;
  }
  return ENCRYPTION_INITIAL;
}

void QuicConnection::SendAllPendingAcks() {
  DCHECK(SupportsMultiplePacketNumberSpaces());
  QUIC_DVLOG(1) << ENDPOINT << "Trying to send all pending ACKs";
  ack_alarm_->Cancel();
  // Latches current encryption level.
  const EncryptionLevel current_encryption_level = encryption_level_;
  for (int8_t i = INITIAL_DATA; i <= APPLICATION_DATA; ++i) {
    const QuicTime ack_timeout = uber_received_packet_manager_.GetAckTimeout(
        static_cast<PacketNumberSpace>(i));
    if (!ack_timeout.IsInitialized() ||
        ack_timeout > clock_->ApproximateNow()) {
      continue;
    }
    if (!framer_.HasEncrypterOfEncryptionLevel(
            QuicUtils::GetEncryptionLevel(static_cast<PacketNumberSpace>(i)))) {
      QUIC_BUG << ENDPOINT << "Cannot send ACKs for packet number space "
               << static_cast<uint32_t>(i)
               << " without corresponding encrypter";
      continue;
    }
    QUIC_DVLOG(1) << ENDPOINT << "Sending ACK of packet number space: "
                  << static_cast<uint32_t>(i);
    // Switch to the appropriate encryption level.
    SetDefaultEncryptionLevel(
        QuicUtils::GetEncryptionLevel(static_cast<PacketNumberSpace>(i)));
    QuicFrames frames;
    frames.push_back(uber_received_packet_manager_.GetUpdatedAckFrame(
        static_cast<PacketNumberSpace>(i), clock_->ApproximateNow()));
    const bool flushed = packet_generator_.FlushAckFrame(frames);
    if (!flushed) {
      // Connection is write blocked.
      QUIC_BUG_IF(!writer_->IsWriteBlocked())
          << "Writer not blocked, but ACK not flushed for packet space:" << i;
      break;
    }
    ResetAckStates();
  }
  // Restores encryption level.
  SetDefaultEncryptionLevel(current_encryption_level);

  const QuicTime timeout =
      uber_received_packet_manager_.GetEarliestAckTimeout();
  if (timeout.IsInitialized()) {
    // If there are ACKs pending, re-arm ack alarm.
    ack_alarm_->Set(timeout);
  }
  // Only try to bundle retransmittable data with ACK frame if default
  // encryption level is forward secure.
  if (encryption_level_ != ENCRYPTION_FORWARD_SECURE ||
      consecutive_num_packets_with_no_retransmittable_frames_ <
          max_consecutive_num_packets_with_no_retransmittable_frames_) {
    return;
  }
  consecutive_num_packets_with_no_retransmittable_frames_ = 0;
  if (packet_generator_.HasRetransmittableFrames() ||
      visitor_->WillingAndAbleToWrite()) {
    // There are pending retransmittable frames.
    return;
  }

  visitor_->OnAckNeedsRetransmittableFrame();
}

void QuicConnection::MaybeEnableMultiplePacketNumberSpacesSupport() {
  if (version().handshake_protocol != PROTOCOL_TLS1_3) {
    return;
  }
  QUIC_DVLOG(1) << ENDPOINT << "connection " << connection_id()
                << " supports multiple packet number spaces";
  framer_.EnableMultiplePacketNumberSpacesSupport();
  sent_packet_manager_.EnableMultiplePacketNumberSpacesSupport();
  uber_received_packet_manager_.EnableMultiplePacketNumberSpacesSupport();
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

QuicPacketNumber QuicConnection::GetLargestReceivedPacketWithAck() const {
  if (SupportsMultiplePacketNumberSpaces()) {
    return largest_seen_packets_with_ack_[QuicUtils::GetPacketNumberSpace(
        last_decrypted_packet_level_)];
  }
  return largest_seen_packet_with_ack_;
}

QuicPacketNumber QuicConnection::GetLargestSentPacket() const {
  if (SupportsMultiplePacketNumberSpaces()) {
    return sent_packet_manager_.GetLargestSentPacket(
        last_decrypted_packet_level_);
  }
  return sent_packet_manager_.GetLargestSentPacket();
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
         perspective_ == Perspective::IS_SERVER && !address_validated_;
}

bool QuicConnection::LimitedByAmplificationFactor() const {
  return EnforceAntiAmplificationLimit() &&
         bytes_sent_before_address_validation_ >=
             GetQuicFlag(FLAGS_quic_anti_amplification_factor) *
                 bytes_received_before_address_validation_;
}

size_t QuicConnection::min_received_before_ack_decimation() const {
  return uber_received_packet_manager_.min_received_before_ack_decimation();
}

void QuicConnection::set_min_received_before_ack_decimation(size_t new_value) {
  uber_received_packet_manager_.set_min_received_before_ack_decimation(
      new_value);
}

size_t QuicConnection::ack_frequency_before_ack_decimation() const {
  return uber_received_packet_manager_.ack_frequency_before_ack_decimation();
}

void QuicConnection::set_ack_frequency_before_ack_decimation(size_t new_value) {
  DCHECK_GT(new_value, 0u);
  uber_received_packet_manager_.set_ack_frequency_before_ack_decimation(
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
  packet_generator_.SetClientConnectionId(client_connection_id_);
  framer_.SetExpectedClientConnectionIdLength(client_connection_id_.length());
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic

// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_dispatcher.h"

#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/chlo_extractor.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/core/quic_time_wait_list_manager.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_stack_trace.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {

typedef QuicBufferedPacketStore::BufferedPacket BufferedPacket;
typedef QuicBufferedPacketStore::BufferedPacketList BufferedPacketList;
typedef QuicBufferedPacketStore::EnqueuePacketResult EnqueuePacketResult;

namespace {

// Minimal INITIAL packet length sent by clients is 1200.
const QuicPacketLength kMinClientInitialPacketLength = 1200;

// An alarm that informs the QuicDispatcher to delete old sessions.
class DeleteSessionsAlarm : public QuicAlarm::Delegate {
 public:
  explicit DeleteSessionsAlarm(QuicDispatcher* dispatcher)
      : dispatcher_(dispatcher) {}
  DeleteSessionsAlarm(const DeleteSessionsAlarm&) = delete;
  DeleteSessionsAlarm& operator=(const DeleteSessionsAlarm&) = delete;

  void OnAlarm() override { dispatcher_->DeleteSessions(); }

 private:
  // Not owned.
  QuicDispatcher* dispatcher_;
};

// Collects packets serialized by a QuicPacketCreator in order
// to be handed off to the time wait list manager.
class PacketCollector : public QuicPacketCreator::DelegateInterface,
                        public QuicStreamFrameDataProducer {
 public:
  explicit PacketCollector(QuicBufferAllocator* allocator)
      : send_buffer_(allocator) {}
  ~PacketCollector() override = default;

  // QuicPacketCreator::DelegateInterface methods:
  void OnSerializedPacket(SerializedPacket* serialized_packet) override {
    // Make a copy of the serialized packet to send later.
    packets_.emplace_back(
        new QuicEncryptedPacket(CopyBuffer(*serialized_packet),
                                serialized_packet->encrypted_length, true));
    serialized_packet->encrypted_buffer = nullptr;
    DeleteFrames(&(serialized_packet->retransmittable_frames));
    serialized_packet->retransmittable_frames.clear();
  }

  char* GetPacketBuffer() override {
    // Let QuicPacketCreator to serialize packets on stack buffer.
    return nullptr;
  }

  void OnUnrecoverableError(QuicErrorCode /*error*/,
                            const std::string& /*error_details*/) override {}

  bool ShouldGeneratePacket(HasRetransmittableData /*retransmittable*/,
                            IsHandshake /*handshake*/) override {
    DCHECK(false);
    return true;
  }

  const QuicFrames MaybeBundleAckOpportunistically() override {
    DCHECK(false);
    return {};
  }

  // QuicStreamFrameDataProducer
  WriteStreamDataResult WriteStreamData(QuicStreamId /*id*/,
                                        QuicStreamOffset offset,
                                        QuicByteCount data_length,
                                        QuicDataWriter* writer) override {
    if (send_buffer_.WriteStreamData(offset, data_length, writer)) {
      return WRITE_SUCCESS;
    }
    return WRITE_FAILED;
  }
  bool WriteCryptoData(EncryptionLevel /*level*/,
                       QuicStreamOffset offset,
                       QuicByteCount data_length,
                       QuicDataWriter* writer) override {
    return send_buffer_.WriteStreamData(offset, data_length, writer);
  }

  std::vector<std::unique_ptr<QuicEncryptedPacket>>* packets() {
    return &packets_;
  }

 private:
  std::vector<std::unique_ptr<QuicEncryptedPacket>> packets_;
  // This is only needed until the packets are encrypted. Once packets are
  // encrypted, the stream data is no longer required.
  QuicStreamSendBuffer send_buffer_;
};

// Helper for statelessly closing connections by generating the
// correct termination packets and adding the connection to the time wait
// list manager.
class StatelessConnectionTerminator {
 public:
  StatelessConnectionTerminator(QuicConnectionId server_connection_id,
                                const ParsedQuicVersion version,
                                QuicConnectionHelperInterface* helper,
                                QuicTimeWaitListManager* time_wait_list_manager)
      : server_connection_id_(server_connection_id),
        framer_(ParsedQuicVersionVector{version},
                /*unused*/ QuicTime::Zero(),
                Perspective::IS_SERVER,
                /*unused*/ kQuicDefaultConnectionIdLength),
        collector_(helper->GetStreamSendBufferAllocator()),
        creator_(server_connection_id, &framer_, &collector_),
        time_wait_list_manager_(time_wait_list_manager) {
    framer_.set_data_producer(&collector_);
    framer_.SetInitialObfuscators(server_connection_id);
  }

  ~StatelessConnectionTerminator() {
    // Clear framer's producer.
    framer_.set_data_producer(nullptr);
  }

  // Generates a packet containing a CONNECTION_CLOSE frame specifying
  // |error_code| and |error_details| and add the connection to time wait.
  void CloseConnection(QuicErrorCode error_code,
                       const std::string& error_details,
                       bool ietf_quic) {
    SerializeConnectionClosePacket(error_code, error_details);

    time_wait_list_manager_->AddConnectionIdToTimeWait(
        server_connection_id_, ietf_quic,
        QuicTimeWaitListManager::SEND_TERMINATION_PACKETS,
        quic::ENCRYPTION_INITIAL, collector_.packets());
  }

 private:
  void SerializeConnectionClosePacket(QuicErrorCode error_code,
                                      const std::string& error_details) {
    QuicConnectionCloseFrame* frame = new QuicConnectionCloseFrame(
        framer_.transport_version(), error_code, error_details,
        /*transport_close_frame_type=*/0);

    if (!creator_.AddFrame(QuicFrame(frame), NOT_RETRANSMISSION)) {
      QUIC_BUG << "Unable to add frame to an empty packet";
      delete frame;
      return;
    }
    creator_.FlushCurrentPacket();
    DCHECK_EQ(1u, collector_.packets()->size());
  }

  QuicConnectionId server_connection_id_;
  QuicFramer framer_;
  // Set as the visitor of |creator_| to collect any generated packets.
  PacketCollector collector_;
  QuicPacketCreator creator_;
  QuicTimeWaitListManager* time_wait_list_manager_;
};

// Class which extracts the ALPN from a CHLO packet.
class ChloAlpnExtractor : public ChloExtractor::Delegate {
 public:
  void OnChlo(QuicTransportVersion /*version*/,
              QuicConnectionId /*server_connection_id*/,
              const CryptoHandshakeMessage& chlo) override {
    quiche::QuicheStringPiece alpn_value;
    if (chlo.GetStringPiece(kALPN, &alpn_value)) {
      alpn_ = std::string(alpn_value);
    }
  }

  std::string&& ConsumeAlpn() { return std::move(alpn_); }

 private:
  std::string alpn_;
};

}  // namespace

QuicDispatcher::QuicDispatcher(
    const QuicConfig* config,
    const QuicCryptoServerConfig* crypto_config,
    QuicVersionManager* version_manager,
    std::unique_ptr<QuicConnectionHelperInterface> helper,
    std::unique_ptr<QuicCryptoServerStreamBase::Helper> session_helper,
    std::unique_ptr<QuicAlarmFactory> alarm_factory,
    uint8_t expected_server_connection_id_length)
    : config_(config),
      crypto_config_(crypto_config),
      compressed_certs_cache_(
          QuicCompressedCertsCache::kQuicCompressedCertsCacheSize),
      helper_(std::move(helper)),
      session_helper_(std::move(session_helper)),
      alarm_factory_(std::move(alarm_factory)),
      delete_sessions_alarm_(
          alarm_factory_->CreateAlarm(new DeleteSessionsAlarm(this))),
      buffered_packets_(this, helper_->GetClock(), alarm_factory_.get()),
      version_manager_(version_manager),
      last_error_(QUIC_NO_ERROR),
      new_sessions_allowed_per_event_loop_(0u),
      accept_new_connections_(true),
      allow_short_initial_server_connection_ids_(false),
      expected_server_connection_id_length_(
          expected_server_connection_id_length),
      should_update_expected_server_connection_id_length_(false) {
  QUIC_BUG_IF(GetSupportedVersions().empty())
      << "Trying to create dispatcher without any supported versions";
  QUIC_DLOG(INFO) << "Created QuicDispatcher with versions: "
                  << ParsedQuicVersionVectorToString(GetSupportedVersions());
}

QuicDispatcher::~QuicDispatcher() {
  session_map_.clear();
  closed_session_list_.clear();
}

void QuicDispatcher::InitializeWithWriter(QuicPacketWriter* writer) {
  DCHECK(writer_ == nullptr);
  writer_.reset(writer);
  time_wait_list_manager_.reset(CreateQuicTimeWaitListManager());
}

void QuicDispatcher::ProcessPacket(const QuicSocketAddress& self_address,
                                   const QuicSocketAddress& peer_address,
                                   const QuicReceivedPacket& packet) {
  QUIC_DVLOG(2) << "Dispatcher received encrypted " << packet.length()
                << " bytes:" << std::endl
                << quiche::QuicheTextUtils::HexDump(quiche::QuicheStringPiece(
                       packet.data(), packet.length()));
  ReceivedPacketInfo packet_info(self_address, peer_address, packet);
  std::string detailed_error;
  bool retry_token_present;
  quiche::QuicheStringPiece retry_token;
  const QuicErrorCode error = QuicFramer::ParsePublicHeaderDispatcher(
      packet, expected_server_connection_id_length_, &packet_info.form,
      &packet_info.long_packet_type, &packet_info.version_flag,
      &packet_info.use_length_prefix, &packet_info.version_label,
      &packet_info.version, &packet_info.destination_connection_id,
      &packet_info.source_connection_id, &retry_token_present, &retry_token,
      &detailed_error);
  if (error != QUIC_NO_ERROR) {
    // Packet has framing error.
    SetLastError(error);
    QUIC_DLOG(ERROR) << detailed_error;
    return;
  }
  if (packet_info.destination_connection_id.length() !=
          expected_server_connection_id_length_ &&
      !should_update_expected_server_connection_id_length_ &&
      packet_info.version.IsKnown() &&
      !packet_info.version.AllowsVariableLengthConnectionIds()) {
    SetLastError(QUIC_INVALID_PACKET_HEADER);
    QUIC_DLOG(ERROR) << "Invalid Connection Id Length";
    return;
  }

  if (packet_info.version_flag && IsSupportedVersion(packet_info.version)) {
    if (!QuicUtils::IsConnectionIdValidForVersion(
            packet_info.destination_connection_id,
            packet_info.version.transport_version)) {
      SetLastError(QUIC_INVALID_PACKET_HEADER);
      QUIC_DLOG(ERROR)
          << "Invalid destination connection ID length for version";
      return;
    }
    if (packet_info.version.SupportsClientConnectionIds() &&
        !QuicUtils::IsConnectionIdValidForVersion(
            packet_info.source_connection_id,
            packet_info.version.transport_version)) {
      SetLastError(QUIC_INVALID_PACKET_HEADER);
      QUIC_DLOG(ERROR) << "Invalid source connection ID length for version";
      return;
    }
  }

  if (should_update_expected_server_connection_id_length_) {
    expected_server_connection_id_length_ =
        packet_info.destination_connection_id.length();
  }

  if (MaybeDispatchPacket(packet_info)) {
    // Packet has been dropped or successfully dispatched, stop processing.
    return;
  }
  ProcessHeader(&packet_info);
}

QuicConnectionId QuicDispatcher::MaybeReplaceServerConnectionId(
    QuicConnectionId server_connection_id,
    ParsedQuicVersion version) {
  if (server_connection_id.length() == expected_server_connection_id_length_) {
    return server_connection_id;
  }
  DCHECK(version.AllowsVariableLengthConnectionIds());

  QuicConnectionId new_connection_id =
      GenerateNewServerConnectionId(version, server_connection_id);
  DCHECK_EQ(expected_server_connection_id_length_, new_connection_id.length());

  // Verify that GenerateNewServerConnectionId is deterministic.
  DCHECK_EQ(new_connection_id,
            GenerateNewServerConnectionId(version, server_connection_id));

  QUIC_DLOG(INFO) << "Replacing incoming connection ID " << server_connection_id
                  << " with " << new_connection_id;
  return new_connection_id;
}

QuicConnectionId QuicDispatcher::GenerateNewServerConnectionId(
    ParsedQuicVersion /*version*/,
    QuicConnectionId connection_id) const {
  return QuicUtils::CreateReplacementConnectionId(connection_id);
}

bool QuicDispatcher::MaybeDispatchPacket(
    const ReceivedPacketInfo& packet_info) {
  // Port zero is only allowed for unidirectional UDP, so is disallowed by QUIC.
  // Given that we can't even send a reply rejecting the packet, just drop the
  // packet.
  if (packet_info.peer_address.port() == 0) {
    return true;
  }

  QuicConnectionId server_connection_id = packet_info.destination_connection_id;

  // The IETF spec requires the client to generate an initial server
  // connection ID that is at least 64 bits long. After that initial
  // connection ID, the dispatcher picks a new one of its expected length.
  // Therefore we should never receive a connection ID that is smaller
  // than 64 bits and smaller than what we expect.
  if (server_connection_id.length() < kQuicMinimumInitialConnectionIdLength &&
      server_connection_id.length() < expected_server_connection_id_length_ &&
      !allow_short_initial_server_connection_ids_) {
    DCHECK(packet_info.version_flag);
    DCHECK(packet_info.version.AllowsVariableLengthConnectionIds());
    QUIC_DLOG(INFO) << "Packet with short destination connection ID "
                    << server_connection_id << " expected "
                    << static_cast<int>(expected_server_connection_id_length_);
    // Drop the packet silently.
    QUIC_CODE_COUNT(quic_dropped_invalid_small_initial_connection_id);
    return true;
  }

  // Packets with connection IDs for active connections are processed
  // immediately.
  auto it = session_map_.find(server_connection_id);
  if (it != session_map_.end()) {
    DCHECK(!buffered_packets_.HasBufferedPackets(server_connection_id));
    it->second->ProcessUdpPacket(packet_info.self_address,
                                 packet_info.peer_address, packet_info.packet);
    return true;
  } else if (packet_info.version.transport_version !=
             QUIC_VERSION_UNSUPPORTED) {
    // We did not find the connection ID, check if we've replaced it.
    // This is only performed for supported versions because packets with
    // unsupported versions can flow through this function in order to send
    // a version negotiation packet, but we know that their connection ID
    // did not get replaced since that is performed on connection creation,
    // and that only happens for known verions.
    QuicConnectionId replaced_connection_id = MaybeReplaceServerConnectionId(
        server_connection_id, packet_info.version);
    if (replaced_connection_id != server_connection_id) {
      // Search for the replacement.
      auto it2 = session_map_.find(replaced_connection_id);
      if (it2 != session_map_.end()) {
        DCHECK(!buffered_packets_.HasBufferedPackets(replaced_connection_id));
        it2->second->ProcessUdpPacket(packet_info.self_address,
                                      packet_info.peer_address,
                                      packet_info.packet);
        return true;
      }
    }
  }

  if (buffered_packets_.HasChloForConnection(server_connection_id)) {
    BufferEarlyPacket(packet_info);
    return true;
  }

  if (OnFailedToDispatchPacket(packet_info)) {
    return true;
  }

  if (time_wait_list_manager_->IsConnectionIdInTimeWait(server_connection_id)) {
    // This connection ID is already in time-wait state.
    time_wait_list_manager_->ProcessPacket(
        packet_info.self_address, packet_info.peer_address,
        packet_info.destination_connection_id, packet_info.form,
        GetPerPacketContext());
    return true;
  }

  // The packet has an unknown connection ID.
  if (!accept_new_connections_ && packet_info.version_flag) {
    // If not accepting new connections, reject packets with version which can
    // potentially result in new connection creation. But if the packet doesn't
    // have version flag, leave it to ValidityChecks() to reset it.
    // By adding the connection to time wait list, following packets on this
    // connection will not reach ShouldAcceptNewConnections().
    StatelesslyTerminateConnection(
        packet_info.destination_connection_id, packet_info.form,
        packet_info.version_flag, packet_info.use_length_prefix,
        packet_info.version, QUIC_HANDSHAKE_FAILED,
        "Stop accepting new connections",
        quic::QuicTimeWaitListManager::SEND_STATELESS_RESET);
    // Time wait list will reject the packet correspondingly..
    time_wait_list_manager()->ProcessPacket(
        packet_info.self_address, packet_info.peer_address,
        packet_info.destination_connection_id, packet_info.form,
        GetPerPacketContext());
    OnNewConnectionRejected();
    return true;
  }

  // Unless the packet provides a version, assume that we can continue
  // processing using our preferred version.
  if (packet_info.version_flag) {
    if (!IsSupportedVersion(packet_info.version)) {
      if (ShouldCreateSessionForUnknownVersion(packet_info.version_label)) {
        return false;
      }
      if (!crypto_config()->validate_chlo_size() ||
          packet_info.packet.length() >= kMinPacketSizeForVersionNegotiation) {
        // Since the version is not supported, send a version negotiation
        // packet and stop processing the current packet.
        QuicConnectionId client_connection_id =
            packet_info.source_connection_id;
        time_wait_list_manager()->SendVersionNegotiationPacket(
            server_connection_id, client_connection_id,
            packet_info.form != GOOGLE_QUIC_PACKET,
            packet_info.use_length_prefix, GetSupportedVersions(),
            packet_info.self_address, packet_info.peer_address,
            GetPerPacketContext());
      }
      return true;
    }

    if (crypto_config()->validate_chlo_size() &&
        packet_info.form == IETF_QUIC_LONG_HEADER_PACKET &&
        packet_info.long_packet_type == INITIAL &&
        packet_info.packet.length() < kMinClientInitialPacketLength) {
      QUIC_DVLOG(1) << "Dropping initial packet which is too short, length: "
                    << packet_info.packet.length();
      QUIC_CODE_COUNT(quic_drop_small_initial_packets);
      return true;
    }
  }

  return false;
}

void QuicDispatcher::ProcessHeader(ReceivedPacketInfo* packet_info) {
  QuicConnectionId server_connection_id =
      packet_info->destination_connection_id;
  // Packet's connection ID is unknown.  Apply the validity checks.
  // TODO(wub): Determine the fate completely in ValidityChecks, then call
  // ProcessUnauthenticatedHeaderFate in one place.
  QuicPacketFate fate = ValidityChecks(*packet_info);
  ChloAlpnExtractor alpn_extractor;
  switch (fate) {
    case kFateProcess: {
      if (packet_info->version.handshake_protocol == PROTOCOL_TLS1_3) {
        // TODO(nharper): Support buffering non-ClientHello packets when using
        // TLS.
        ProcessChlo(/*alpn=*/"", packet_info);
        break;
      }
      if (GetQuicFlag(FLAGS_quic_allow_chlo_buffering) &&
          !ChloExtractor::Extract(packet_info->packet, packet_info->version,
                                  config_->create_session_tag_indicators(),
                                  &alpn_extractor,
                                  server_connection_id.length())) {
        // Buffer non-CHLO packets.
        BufferEarlyPacket(*packet_info);
        break;
      }
      ProcessChlo(alpn_extractor.ConsumeAlpn(), packet_info);
    } break;
    case kFateTimeWait:
      // Add this connection_id to the time-wait state, to safely reject
      // future packets.
      QUIC_DLOG(INFO) << "Adding connection ID " << server_connection_id
                      << " to time-wait list.";
      QUIC_CODE_COUNT(quic_reject_fate_time_wait);
      StatelesslyTerminateConnection(
          server_connection_id, packet_info->form, packet_info->version_flag,
          packet_info->use_length_prefix, packet_info->version,
          QUIC_HANDSHAKE_FAILED, "Reject connection",
          quic::QuicTimeWaitListManager::SEND_STATELESS_RESET);

      DCHECK(time_wait_list_manager_->IsConnectionIdInTimeWait(
          server_connection_id));
      time_wait_list_manager_->ProcessPacket(
          packet_info->self_address, packet_info->peer_address,
          server_connection_id, packet_info->form, GetPerPacketContext());

      buffered_packets_.DiscardPackets(server_connection_id);
      break;
    case kFateDrop:
      break;
  }
}

QuicDispatcher::QuicPacketFate QuicDispatcher::ValidityChecks(
    const ReceivedPacketInfo& packet_info) {
  if (!packet_info.version_flag) {
      // The Android network conformance test contains a UDP test that sends a
      // 12-byte packet with the following format:
      //  - 0x0c (public flags: 8-byte connection ID, 1-byte packet number)
      //  - randomized 8-byte connection ID
      //  - 0x01 (1-byte packet number)
      //  - 0x00 (private flags)
      //  - 0x07 (PING frame).
      // That packet is invalid and we would normally drop it but in order to
      // unblock this conformance testing we have the following workaround that
      // will be removed once the fixed test is deployed.
      // TODO(b/139691956) Remove this workaround once fixed test is deployed.
      if (packet_info.packet.length() == 12 &&
          packet_info.packet.data()[0] == 0x0c &&
          packet_info.packet.data()[9] == 0x01 &&
          packet_info.packet.data()[10] == 0x00 &&
          packet_info.packet.data()[11] == 0x07) {
        QUIC_DLOG(INFO) << "Received Android UDP network conformance test "
                           "packet with connection ID "
                        << packet_info.destination_connection_id;
        // Respond with a public reset that the test will know how to parse
        // then return kFateDrop to stop processing of this packet.
        time_wait_list_manager()->SendPublicReset(
            packet_info.self_address, packet_info.peer_address,
            packet_info.destination_connection_id,
            /*ietf_quic=*/false, GetPerPacketContext());
        return kFateDrop;
      }

    QUIC_DLOG(INFO)
        << "Packet without version arrived for unknown connection ID "
        << packet_info.destination_connection_id;
    MaybeResetPacketsWithNoVersion(packet_info);
    return kFateDrop;
  }

  // Let the connection parse and validate packet number.
  return kFateProcess;
}

void QuicDispatcher::CleanUpSession(SessionMap::iterator it,
                                    QuicConnection* connection,
                                    ConnectionCloseSource /*source*/) {
  write_blocked_list_.erase(connection);
  QuicTimeWaitListManager::TimeWaitAction action =
      QuicTimeWaitListManager::SEND_STATELESS_RESET;
  if (connection->termination_packets() != nullptr &&
      !connection->termination_packets()->empty()) {
    action = QuicTimeWaitListManager::SEND_TERMINATION_PACKETS;
  } else {
    if (!connection->IsHandshakeComplete()) {
      if (!VersionHasIetfInvariantHeader(connection->transport_version())) {
        QUIC_CODE_COUNT(gquic_add_to_time_wait_list_with_handshake_failed);
      } else {
        QUIC_CODE_COUNT(quic_v44_add_to_time_wait_list_with_handshake_failed);
      }
      action = QuicTimeWaitListManager::SEND_TERMINATION_PACKETS;
      // This serializes a connection close termination packet with error code
      // QUIC_HANDSHAKE_FAILED and adds the connection to the time wait list.
      StatelesslyTerminateConnection(
          connection->connection_id(),
          VersionHasIetfInvariantHeader(connection->transport_version())
              ? IETF_QUIC_LONG_HEADER_PACKET
              : GOOGLE_QUIC_PACKET,
          /*version_flag=*/true,
          connection->version().HasLengthPrefixedConnectionIds(),
          connection->version(), QUIC_HANDSHAKE_FAILED,
          "Connection is closed by server before handshake confirmed",
          // Although it is our intention to send termination packets, the
          // |action| argument is not used by this call to
          // StatelesslyTerminateConnection().
          action);
      session_map_.erase(it);
      return;
    }
    QUIC_CODE_COUNT(quic_v44_add_to_time_wait_list_with_stateless_reset);
  }
  time_wait_list_manager_->AddConnectionIdToTimeWait(
      it->first, VersionHasIetfInvariantHeader(connection->transport_version()),
      action, connection->encryption_level(),
      connection->termination_packets());
  session_map_.erase(it);
}

void QuicDispatcher::StartAcceptingNewConnections() {
  accept_new_connections_ = true;
}

void QuicDispatcher::StopAcceptingNewConnections() {
  accept_new_connections_ = false;
  // No more CHLO will arrive and buffered CHLOs shouldn't be able to create
  // connections.
  buffered_packets_.DiscardAllPackets();
}

std::unique_ptr<QuicPerPacketContext> QuicDispatcher::GetPerPacketContext()
    const {
  return nullptr;
}

void QuicDispatcher::DeleteSessions() {
  if (!write_blocked_list_.empty()) {
    for (const std::unique_ptr<QuicSession>& session : closed_session_list_) {
      if (write_blocked_list_.erase(session->connection()) != 0) {
        QUIC_BUG << "QuicConnection was in WriteBlockedList before destruction "
                 << session->connection()->connection_id();
      }
    }
  }
  closed_session_list_.clear();
}

void QuicDispatcher::OnCanWrite() {
  // The socket is now writable.
  writer_->SetWritable();

  // Move every blocked writer in |write_blocked_list_| to a temporary list.
  const size_t num_blocked_writers_before = write_blocked_list_.size();
  WriteBlockedList temp_list;
  temp_list.swap(write_blocked_list_);
  DCHECK(write_blocked_list_.empty());

  // Give each blocked writer a chance to write what they indended to write.
  // If they are blocked again, they will call |OnWriteBlocked| to add
  // themselves back into |write_blocked_list_|.
  while (!temp_list.empty()) {
    QuicBlockedWriterInterface* blocked_writer = temp_list.begin()->first;
    temp_list.erase(temp_list.begin());
    blocked_writer->OnBlockedWriterCanWrite();
  }
  const size_t num_blocked_writers_after = write_blocked_list_.size();
  if (num_blocked_writers_after != 0) {
    if (num_blocked_writers_before == num_blocked_writers_after) {
      QUIC_CODE_COUNT(quic_zero_progress_on_can_write);
    } else {
      QUIC_CODE_COUNT(quic_blocked_again_on_can_write);
    }
  }
}

bool QuicDispatcher::HasPendingWrites() const {
  return !write_blocked_list_.empty();
}

void QuicDispatcher::Shutdown() {
  while (!session_map_.empty()) {
    QuicSession* session = session_map_.begin()->second.get();
    session->connection()->CloseConnection(
        QUIC_PEER_GOING_AWAY, "Server shutdown imminent",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    // Validate that the session removes itself from the session map on close.
    DCHECK(session_map_.empty() ||
           session_map_.begin()->second.get() != session);
  }
  DeleteSessions();
}

void QuicDispatcher::OnConnectionClosed(QuicConnectionId server_connection_id,
                                        QuicErrorCode error,
                                        const std::string& error_details,
                                        ConnectionCloseSource source) {
  auto it = session_map_.find(server_connection_id);
  if (it == session_map_.end()) {
    QUIC_BUG << "ConnectionId " << server_connection_id
             << " does not exist in the session map.  Error: "
             << QuicErrorCodeToString(error);
    QUIC_BUG << QuicStackTrace();
    return;
  }

  QUIC_DLOG_IF(INFO, error != QUIC_NO_ERROR)
      << "Closing connection (" << server_connection_id
      << ") due to error: " << QuicErrorCodeToString(error)
      << ", with details: " << error_details;

  QuicConnection* connection = it->second->connection();
  if (ShouldDestroySessionAsynchronously()) {
    // Set up alarm to fire immediately to bring destruction of this session
    // out of current call stack.
    if (closed_session_list_.empty()) {
      delete_sessions_alarm_->Update(helper()->GetClock()->ApproximateNow(),
                                     QuicTime::Delta::Zero());
    }
    closed_session_list_.push_back(std::move(it->second));
  }
  CleanUpSession(it, connection, source);
}

void QuicDispatcher::OnWriteBlocked(
    QuicBlockedWriterInterface* blocked_writer) {
  if (!blocked_writer->IsWriterBlocked()) {
    // It is a programming error if this ever happens. When we are sure it is
    // not happening, replace it with a DCHECK.
    QUIC_BUG
        << "Tried to add writer into blocked list when it shouldn't be added";
    // Return without adding the connection to the blocked list, to avoid
    // infinite loops in OnCanWrite.
    return;
  }

  write_blocked_list_.insert(std::make_pair(blocked_writer, true));
}

void QuicDispatcher::OnRstStreamReceived(const QuicRstStreamFrame& /*frame*/) {}

void QuicDispatcher::OnStopSendingReceived(
    const QuicStopSendingFrame& /*frame*/) {}

void QuicDispatcher::OnConnectionAddedToTimeWaitList(
    QuicConnectionId server_connection_id) {
  QUIC_DLOG(INFO) << "Connection " << server_connection_id
                  << " added to time wait list.";
}

void QuicDispatcher::StatelesslyTerminateConnection(
    QuicConnectionId server_connection_id,
    PacketHeaderFormat format,
    bool version_flag,
    bool use_length_prefix,
    ParsedQuicVersion version,
    QuicErrorCode error_code,
    const std::string& error_details,
    QuicTimeWaitListManager::TimeWaitAction action) {
  if (format != IETF_QUIC_LONG_HEADER_PACKET && !version_flag) {
    QUIC_DVLOG(1) << "Statelessly terminating " << server_connection_id
                  << " based on a non-ietf-long packet, action:" << action
                  << ", error_code:" << error_code
                  << ", error_details:" << error_details;
    time_wait_list_manager_->AddConnectionIdToTimeWait(
        server_connection_id, format != GOOGLE_QUIC_PACKET, action,
        ENCRYPTION_INITIAL, nullptr);
    return;
  }

  // If the version is known and supported by framer, send a connection close.
  if (IsSupportedVersion(version)) {
    QUIC_DVLOG(1)
        << "Statelessly terminating " << server_connection_id
        << " based on an ietf-long packet, which has a supported version:"
        << version << ", error_code:" << error_code
        << ", error_details:" << error_details;

    StatelessConnectionTerminator terminator(server_connection_id, version,
                                             helper_.get(),
                                             time_wait_list_manager_.get());
    // This also adds the connection to time wait list.
    terminator.CloseConnection(error_code, error_details,
                               format != GOOGLE_QUIC_PACKET);
    return;
  }

  QUIC_DVLOG(1)
      << "Statelessly terminating " << server_connection_id
      << " based on an ietf-long packet, which has an unsupported version:"
      << version << ", error_code:" << error_code
      << ", error_details:" << error_details;
  // Version is unknown or unsupported by framer, send a version negotiation
  // with an empty version list, which can be understood by the client.
  std::vector<std::unique_ptr<QuicEncryptedPacket>> termination_packets;
  termination_packets.push_back(QuicFramer::BuildVersionNegotiationPacket(
      server_connection_id, EmptyQuicConnectionId(),
      /*ietf_quic=*/format != GOOGLE_QUIC_PACKET, use_length_prefix,
      /*versions=*/{}));
  time_wait_list_manager()->AddConnectionIdToTimeWait(
      server_connection_id, /*ietf_quic=*/format != GOOGLE_QUIC_PACKET,
      QuicTimeWaitListManager::SEND_TERMINATION_PACKETS, ENCRYPTION_INITIAL,
      &termination_packets);
}

bool QuicDispatcher::ShouldCreateSessionForUnknownVersion(
    QuicVersionLabel /*version_label*/) {
  return false;
}

void QuicDispatcher::OnExpiredPackets(
    QuicConnectionId server_connection_id,
    BufferedPacketList early_arrived_packets) {
  QUIC_CODE_COUNT(quic_reject_buffered_packets_expired);
  StatelesslyTerminateConnection(
      server_connection_id,
      early_arrived_packets.ietf_quic ? IETF_QUIC_LONG_HEADER_PACKET
                                      : GOOGLE_QUIC_PACKET,
      /*version_flag=*/true,
      early_arrived_packets.version.HasLengthPrefixedConnectionIds(),
      early_arrived_packets.version, QUIC_HANDSHAKE_FAILED,
      "Packets buffered for too long",
      quic::QuicTimeWaitListManager::SEND_STATELESS_RESET);
}

void QuicDispatcher::ProcessBufferedChlos(size_t max_connections_to_create) {
  // Reset the counter before starting creating connections.
  new_sessions_allowed_per_event_loop_ = max_connections_to_create;
  for (; new_sessions_allowed_per_event_loop_ > 0;
       --new_sessions_allowed_per_event_loop_) {
    QuicConnectionId server_connection_id;
    BufferedPacketList packet_list =
        buffered_packets_.DeliverPacketsForNextConnection(
            &server_connection_id);
    const std::list<BufferedPacket>& packets = packet_list.buffered_packets;
    if (packets.empty()) {
      return;
    }
    QuicConnectionId original_connection_id = server_connection_id;
    server_connection_id = MaybeReplaceServerConnectionId(server_connection_id,
                                                          packet_list.version);
    std::unique_ptr<QuicSession> session =
        CreateQuicSession(server_connection_id, packets.front().peer_address,
                          packet_list.alpn, packet_list.version);
    if (original_connection_id != server_connection_id) {
      session->connection()->AddIncomingConnectionId(original_connection_id);
      session->connection()->InstallInitialCrypters(original_connection_id);
    }
    QUIC_DLOG(INFO) << "Created new session for " << server_connection_id;

    auto insertion_result = session_map_.insert(
        std::make_pair(server_connection_id, std::move(session)));
    QUIC_BUG_IF(!insertion_result.second)
        << "Tried to add a session to session_map with existing connection id: "
        << server_connection_id;
    DeliverPacketsToSession(packets, insertion_result.first->second.get());
  }
}

bool QuicDispatcher::HasChlosBuffered() const {
  return buffered_packets_.HasChlosBuffered();
}

bool QuicDispatcher::ShouldCreateOrBufferPacketForConnection(
    const ReceivedPacketInfo& packet_info) {
  QUIC_VLOG(1) << "Received packet from new connection "
               << packet_info.destination_connection_id;
  return true;
}

// Return true if there is any packet buffered in the store.
bool QuicDispatcher::HasBufferedPackets(QuicConnectionId server_connection_id) {
  return buffered_packets_.HasBufferedPackets(server_connection_id);
}

void QuicDispatcher::OnBufferPacketFailure(
    EnqueuePacketResult result,
    QuicConnectionId server_connection_id) {
  QUIC_DLOG(INFO) << "Fail to buffer packet on connection "
                  << server_connection_id << " because of " << result;
}

QuicTimeWaitListManager* QuicDispatcher::CreateQuicTimeWaitListManager() {
  return new QuicTimeWaitListManager(writer_.get(), this, helper_->GetClock(),
                                     alarm_factory_.get());
}

void QuicDispatcher::BufferEarlyPacket(const ReceivedPacketInfo& packet_info) {
  bool is_new_connection = !buffered_packets_.HasBufferedPackets(
      packet_info.destination_connection_id);
  if (is_new_connection &&
      !ShouldCreateOrBufferPacketForConnection(packet_info)) {
    return;
  }

  EnqueuePacketResult rs = buffered_packets_.EnqueuePacket(
      packet_info.destination_connection_id,
      packet_info.form != GOOGLE_QUIC_PACKET, packet_info.packet,
      packet_info.self_address, packet_info.peer_address, /*is_chlo=*/false,
      /*alpn=*/"", packet_info.version);
  if (rs != EnqueuePacketResult::SUCCESS) {
    OnBufferPacketFailure(rs, packet_info.destination_connection_id);
  }
}

void QuicDispatcher::ProcessChlo(const std::string& alpn,
                                 ReceivedPacketInfo* packet_info) {
  if (!buffered_packets_.HasBufferedPackets(
          packet_info->destination_connection_id) &&
      !ShouldCreateOrBufferPacketForConnection(*packet_info)) {
    return;
  }
  if (GetQuicFlag(FLAGS_quic_allow_chlo_buffering) &&
      new_sessions_allowed_per_event_loop_ <= 0) {
    // Can't create new session any more. Wait till next event loop.
    QUIC_BUG_IF(buffered_packets_.HasChloForConnection(
        packet_info->destination_connection_id));
    EnqueuePacketResult rs = buffered_packets_.EnqueuePacket(
        packet_info->destination_connection_id,
        packet_info->form != GOOGLE_QUIC_PACKET, packet_info->packet,
        packet_info->self_address, packet_info->peer_address,
        /*is_chlo=*/true, alpn, packet_info->version);
    if (rs != EnqueuePacketResult::SUCCESS) {
      OnBufferPacketFailure(rs, packet_info->destination_connection_id);
    }
    return;
  }

  QuicConnectionId original_connection_id =
      packet_info->destination_connection_id;
  packet_info->destination_connection_id = MaybeReplaceServerConnectionId(
      original_connection_id, packet_info->version);
  // Creates a new session and process all buffered packets for this connection.
  std::unique_ptr<QuicSession> session =
      CreateQuicSession(packet_info->destination_connection_id,
                        packet_info->peer_address, alpn, packet_info->version);
  DCHECK(session);
  if (original_connection_id != packet_info->destination_connection_id) {
    session->connection()->AddIncomingConnectionId(original_connection_id);
    session->connection()->InstallInitialCrypters(original_connection_id);
  }
  QUIC_DLOG(INFO) << "Created new session for "
                  << packet_info->destination_connection_id;

  auto insertion_result = session_map_.insert(std::make_pair(
      packet_info->destination_connection_id, std::move(session)));
  QUIC_BUG_IF(!insertion_result.second)
      << "Tried to add a session to session_map with existing connection id: "
      << packet_info->destination_connection_id;
  QuicSession* session_ptr = insertion_result.first->second.get();
  std::list<BufferedPacket> packets =
      buffered_packets_.DeliverPackets(packet_info->destination_connection_id)
          .buffered_packets;
  // Process CHLO at first.
  session_ptr->ProcessUdpPacket(packet_info->self_address,
                                packet_info->peer_address, packet_info->packet);
  // Deliver queued-up packets in the same order as they arrived.
  // Do this even when flag is off because there might be still some packets
  // buffered in the store before flag is turned off.
  DeliverPacketsToSession(packets, session_ptr);
  --new_sessions_allowed_per_event_loop_;
}

bool QuicDispatcher::ShouldDestroySessionAsynchronously() {
  return true;
}

void QuicDispatcher::SetLastError(QuicErrorCode error) {
  last_error_ = error;
}

bool QuicDispatcher::OnFailedToDispatchPacket(
    const ReceivedPacketInfo& /*packet_info*/) {
  return false;
}

const QuicTransportVersionVector&
QuicDispatcher::GetSupportedTransportVersions() {
  return version_manager_->GetSupportedTransportVersions();
}

const ParsedQuicVersionVector& QuicDispatcher::GetSupportedVersions() {
  return version_manager_->GetSupportedVersions();
}

void QuicDispatcher::DeliverPacketsToSession(
    const std::list<BufferedPacket>& packets,
    QuicSession* session) {
  for (const BufferedPacket& packet : packets) {
    session->ProcessUdpPacket(packet.self_address, packet.peer_address,
                              *(packet.packet));
  }
}

bool QuicDispatcher::IsSupportedVersion(const ParsedQuicVersion version) {
  for (const ParsedQuicVersion& supported_version :
       version_manager_->GetSupportedVersions()) {
    if (version == supported_version) {
      return true;
    }
  }
  return false;
}

void QuicDispatcher::MaybeResetPacketsWithNoVersion(
    const ReceivedPacketInfo& packet_info) {
  DCHECK(!packet_info.version_flag);
  const size_t MinValidPacketLength =
      kPacketHeaderTypeSize + expected_server_connection_id_length_ +
      PACKET_1BYTE_PACKET_NUMBER + /*payload size=*/1 + /*tag size=*/12;
  if (packet_info.packet.length() < MinValidPacketLength) {
    // The packet size is too small.
    QUIC_CODE_COUNT(drop_too_small_packets);
    return;
  }
  // TODO(fayang): Consider rate limiting reset packets if reset packet size >
  // packet_length.

  time_wait_list_manager()->SendPublicReset(
      packet_info.self_address, packet_info.peer_address,
      packet_info.destination_connection_id,
      packet_info.form != GOOGLE_QUIC_PACKET, GetPerPacketContext());
}

}  // namespace quic

// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/qbone_session_base.h"

#include <netinet/icmp6.h>
#include <netinet/ip6.h>

#include <limits>
#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_exported_stats.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_testvalue.h"
#include "quiche/quic/qbone/platform/icmp_packet.h"
#include "quiche/quic/qbone/qbone_constants.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/quiche_buffer_allocator.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, qbone_close_ephemeral_frames, true,
    "If true, we'll call CloseStream even when we receive ephemeral frames.");

namespace quic {

#define ENDPOINT \
  (perspective() == Perspective::IS_SERVER ? "Server: " : "Client: ")

QboneSessionBase::QboneSessionBase(
    QuicConnection* connection, Visitor* owner, const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    QbonePacketWriter* writer)
    : QuicSession(connection, owner, config, supported_versions,
                  /*num_expected_unidirectional_static_streams = */ 0) {
  set_writer(writer);
  const uint32_t max_streams =
      (std::numeric_limits<uint32_t>::max() / kMaxAvailableStreamsMultiplier) -
      1;
  this->config()->SetMaxBidirectionalStreamsToSend(max_streams);
  if (VersionHasIetfQuicFrames(transport_version())) {
    this->config()->SetMaxUnidirectionalStreamsToSend(max_streams);
  }
}

QboneSessionBase::~QboneSessionBase() {}

void QboneSessionBase::Initialize() {
  crypto_stream_ = CreateCryptoStream();
  QuicSession::Initialize();
}

const QuicCryptoStream* QboneSessionBase::GetCryptoStream() const {
  return crypto_stream_.get();
}

QuicCryptoStream* QboneSessionBase::GetMutableCryptoStream() {
  return crypto_stream_.get();
}

QuicStream* QboneSessionBase::CreateOutgoingStream() {
  return ActivateDataStream(
      CreateDataStream(GetNextOutgoingUnidirectionalStreamId()));
}

void QboneSessionBase::OnStreamFrame(const QuicStreamFrame& frame) {
  if (frame.offset == 0 && frame.fin && frame.data_length > 0) {
    ++num_ephemeral_packets_;
    ProcessPacketFromPeer(
        absl::string_view(frame.data_buffer, frame.data_length));
    flow_controller()->AddBytesConsumed(frame.data_length);
    // TODO(b/147817422): Add a counter for how many streams were actually
    // closed here.
    if (quiche::GetQuicheCommandLineFlag(FLAGS_qbone_close_ephemeral_frames)) {
      ResetStream(frame.stream_id, QUIC_STREAM_CANCELLED);
    }
    return;
  }
  QuicSession::OnStreamFrame(frame);
}

void QboneSessionBase::OnMessageReceived(absl::string_view message) {
  ++num_message_packets_;
  ProcessPacketFromPeer(message);
}

QuicStream* QboneSessionBase::CreateIncomingStream(QuicStreamId id) {
  return ActivateDataStream(CreateDataStream(id));
}

QuicStream* QboneSessionBase::CreateIncomingStream(PendingStream* /*pending*/) {
  QUICHE_NOTREACHED();
  return nullptr;
}

bool QboneSessionBase::ShouldKeepConnectionAlive() const {
  // QBONE connections stay alive until they're explicitly closed.
  return true;
}

std::unique_ptr<QuicStream> QboneSessionBase::CreateDataStream(
    QuicStreamId id) {
  if (!IsEncryptionEstablished()) {
    // Encryption not active so no stream created
    return nullptr;
  }

  if (IsIncomingStream(id)) {
    ++num_streamed_packets_;
    return std::make_unique<QboneReadOnlyStream>(id, this);
  }

  return std::make_unique<QboneWriteOnlyStream>(id, this);
}

QuicStream* QboneSessionBase::ActivateDataStream(
    std::unique_ptr<QuicStream> stream) {
  // Transfer ownership of the data stream to the session via ActivateStream().
  QuicStream* raw = stream.get();
  if (stream) {
    // Make QuicSession take ownership of the stream.
    ActivateStream(std::move(stream));
  }
  return raw;
}

void QboneSessionBase::SendPacketToPeer(absl::string_view packet) {
  if (crypto_stream_ == nullptr) {
    QUIC_BUG(quic_bug_10987_1)
        << "Attempting to send packet before encryption established";
    return;
  }

  if (send_packets_as_messages_) {
    quiche::QuicheMemSlice slice(quiche::QuicheBuffer::Copy(
        connection()->helper()->GetStreamSendBufferAllocator(), packet));
    switch (SendMessage(absl::MakeSpan(&slice, 1), /*flush=*/true).status) {
      case MESSAGE_STATUS_SUCCESS:
        break;
      case MESSAGE_STATUS_TOO_LARGE: {
        if (packet.size() < sizeof(ip6_hdr)) {
          QUIC_BUG(quic_bug_10987_2)
              << "Dropped malformed packet: IPv6 header too short";
          break;
        }
        auto* header = reinterpret_cast<const ip6_hdr*>(packet.begin());
        icmp6_hdr icmp_header{};
        icmp_header.icmp6_type = ICMP6_PACKET_TOO_BIG;
        icmp_header.icmp6_mtu =
            connection()->GetGuaranteedLargestMessagePayload();

        CreateIcmpPacket(header->ip6_dst, header->ip6_src, icmp_header, packet,
                         [this](absl::string_view icmp_packet) {
                           writer_->WritePacketToNetwork(icmp_packet.data(),
                                                         icmp_packet.size());
                         });
        break;
      }
      case MESSAGE_STATUS_ENCRYPTION_NOT_ESTABLISHED:
        QUIC_BUG(quic_bug_10987_3)
            << "MESSAGE_STATUS_ENCRYPTION_NOT_ESTABLISHED";
        break;
      case MESSAGE_STATUS_UNSUPPORTED:
        QUIC_BUG(quic_bug_10987_4) << "MESSAGE_STATUS_UNSUPPORTED";
        break;
      case MESSAGE_STATUS_BLOCKED:
        QUIC_BUG(quic_bug_10987_5) << "MESSAGE_STATUS_BLOCKED";
        break;
      case MESSAGE_STATUS_INTERNAL_ERROR:
        QUIC_BUG(quic_bug_10987_6) << "MESSAGE_STATUS_INTERNAL_ERROR";
        break;
    }
    return;
  }

  // QBONE streams are ephemeral.
  QuicStream* stream = CreateOutgoingStream();
  if (!stream) {
    QUIC_BUG(quic_bug_10987_7) << "Failed to create an outgoing QBONE stream.";
    return;
  }

  QboneWriteOnlyStream* qbone_stream =
      static_cast<QboneWriteOnlyStream*>(stream);
  qbone_stream->WritePacketToQuicStream(packet);
}

uint64_t QboneSessionBase::GetNumEphemeralPackets() const {
  return num_ephemeral_packets_;
}

uint64_t QboneSessionBase::GetNumStreamedPackets() const {
  return num_streamed_packets_;
}

uint64_t QboneSessionBase::GetNumMessagePackets() const {
  return num_message_packets_;
}

uint64_t QboneSessionBase::GetNumFallbackToStream() const {
  return num_fallback_to_stream_;
}

void QboneSessionBase::set_writer(QbonePacketWriter* writer) {
  writer_ = writer;
  quic::AdjustTestValue("quic_QbonePacketWriter", &writer_);
}

}  // namespace quic

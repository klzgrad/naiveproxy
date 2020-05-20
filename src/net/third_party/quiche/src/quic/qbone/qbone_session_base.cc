// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/qbone_session_base.h"

#include <netinet/icmp6.h>
#include <netinet/ip6.h>

#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_data_reader.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_exported_stats.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/qbone/platform/icmp_packet.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_constants.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

#define ENDPOINT \
  (perspective() == Perspective::IS_SERVER ? "Server: " : "Client: ")

QboneSessionBase::QboneSessionBase(
    QuicConnection* connection,
    Visitor* owner,
    const QuicConfig& config,
    const ParsedQuicVersionVector& supported_versions,
    QbonePacketWriter* writer)
    : QuicSession(connection,
                  owner,
                  config,
                  supported_versions,
                  /*num_expected_unidirectional_static_streams = */ 0) {
  set_writer(writer);
  const uint32_t max_streams =
      (std::numeric_limits<uint32_t>::max() / kMaxAvailableStreamsMultiplier) -
      1;
  this->config()->SetMaxBidirectionalStreamsToSend(max_streams);
  if (VersionHasIetfQuicFrames(transport_version())) {
    ConfigureMaxDynamicStreamsToSend(max_streams);
  }
}

QboneSessionBase::~QboneSessionBase() {
  // Clear out the streams before leaving this destructor to avoid calling
  // QuicSession::UnregisterStreamPriority
  stream_map().clear();
  closed_streams()->clear();
}

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

void QboneSessionBase::CloseStream(QuicStreamId stream_id) {
  if (IsClosedStream(stream_id)) {
    // When CloseStream has been called recursively (via
    // QuicStream::OnClose), the stream is already closed so return.
    return;
  }
  QuicSession::CloseStream(stream_id);
}

void QboneSessionBase::OnStreamFrame(const QuicStreamFrame& frame) {
  if (frame.offset == 0 && frame.fin && frame.data_length > 0) {
    ++num_ephemeral_packets_;
    ProcessPacketFromPeer(
        quiche::QuicheStringPiece(frame.data_buffer, frame.data_length));
    flow_controller()->AddBytesConsumed(frame.data_length);
    return;
  }
  QuicSession::OnStreamFrame(frame);
}

void QboneSessionBase::OnMessageReceived(quiche::QuicheStringPiece message) {
  ++num_message_packets_;
  ProcessPacketFromPeer(message);
}

QuicStream* QboneSessionBase::CreateIncomingStream(QuicStreamId id) {
  return ActivateDataStream(CreateDataStream(id));
}

QuicStream* QboneSessionBase::CreateIncomingStream(PendingStream* /*pending*/) {
  QUIC_NOTREACHED();
  return nullptr;
}

bool QboneSessionBase::ShouldKeepConnectionAlive() const {
  // QBONE connections stay alive until they're explicitly closed.
  return true;
}

std::unique_ptr<QuicStream> QboneSessionBase::CreateDataStream(
    QuicStreamId id) {
  if (crypto_stream_ == nullptr || !crypto_stream_->encryption_established()) {
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

void QboneSessionBase::SendPacketToPeer(quiche::QuicheStringPiece packet) {
  if (crypto_stream_ == nullptr) {
    QUIC_BUG << "Attempting to send packet before encryption established";
    return;
  }

  if (send_packets_as_messages_) {
    QuicUniqueBufferPtr buffer = MakeUniqueBuffer(
        connection()->helper()->GetStreamSendBufferAllocator(), packet.size());
    memcpy(buffer.get(), packet.data(), packet.size());
    QuicMemSlice slice(std::move(buffer), packet.size());
    switch (SendMessage(QuicMemSliceSpan(&slice), /*flush=*/true).status) {
      case MESSAGE_STATUS_SUCCESS:
        break;
      case MESSAGE_STATUS_TOO_LARGE: {
        if (packet.size() < sizeof(ip6_hdr)) {
          QUIC_BUG << "Dropped malformed packet: IPv6 header too short";
          break;
        }
        auto* header = reinterpret_cast<const ip6_hdr*>(packet.begin());
        icmp6_hdr icmp_header{};
        icmp_header.icmp6_type = ICMP6_PACKET_TOO_BIG;
        icmp_header.icmp6_mtu =
            connection()->GetGuaranteedLargestMessagePayload();

        CreateIcmpPacket(header->ip6_dst, header->ip6_src, icmp_header, packet,
                         [this](quiche::QuicheStringPiece icmp_packet) {
                           writer_->WritePacketToNetwork(icmp_packet.data(),
                                                         icmp_packet.size());
                         });
        break;
      }
      case MESSAGE_STATUS_ENCRYPTION_NOT_ESTABLISHED:
        QUIC_BUG << "MESSAGE_STATUS_ENCRYPTION_NOT_ESTABLISHED";
        break;
      case MESSAGE_STATUS_UNSUPPORTED:
        QUIC_BUG << "MESSAGE_STATUS_UNSUPPORTED";
        break;
      case MESSAGE_STATUS_BLOCKED:
        QUIC_BUG << "MESSAGE_STATUS_BLOCKED";
        break;
      case MESSAGE_STATUS_INTERNAL_ERROR:
        QUIC_BUG << "MESSAGE_STATUS_INTERNAL_ERROR";
        break;
    }
    return;
  }

  // QBONE streams are ephemeral.
  QuicStream* stream = CreateOutgoingStream();
  if (!stream) {
    QUIC_BUG << "Failed to create an outgoing QBONE stream.";
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
  testing::testvalue::Adjust("quic_QbonePacketWriter", &writer_);
}

}  // namespace quic

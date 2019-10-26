// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/qbone_session_base.h"

#include "net/third_party/quiche/src/quic/core/quic_data_reader.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_exported_stats.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_constants.h"

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
  this->config()->SetMaxIncomingBidirectionalStreamsToSend(max_streams);
  if (VersionHasIetfQuicFrames(transport_version())) {
    ConfigureMaxIncomingDynamicStreamsToSend(max_streams);
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
        QuicStringPiece(frame.data_buffer, frame.data_length));
    flow_controller()->AddBytesConsumed(frame.data_length);
    return;
  }
  QuicSession::OnStreamFrame(frame);
}

QuicStream* QboneSessionBase::CreateIncomingStream(QuicStreamId id) {
  return ActivateDataStream(CreateDataStream(id));
}

QuicStream* QboneSessionBase::CreateIncomingStream(PendingStream* /*pending*/) {
  QUIC_NOTREACHED();
  return nullptr;
}

bool QboneSessionBase::ShouldKeepConnectionAlive() const {
  // Qbone connections stay alive until they're explicitly closed.
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
    return QuicMakeUnique<QboneReadOnlyStream>(id, this);
  }

  return QuicMakeUnique<QboneWriteOnlyStream>(id, this);
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

void QboneSessionBase::SendPacketToPeer(QuicStringPiece packet) {
  // Qbone streams are ephemeral.
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

void QboneSessionBase::set_writer(QbonePacketWriter* writer) {
  writer_ = writer;
  testing::testvalue::Adjust("quic_QbonePacketWriter", &writer_);
}

}  // namespace quic

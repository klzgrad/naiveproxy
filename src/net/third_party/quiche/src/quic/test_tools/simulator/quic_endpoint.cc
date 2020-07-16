// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/simulator/quic_endpoint.h"

#include <memory>
#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake_message.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test_output.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_connection_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"

namespace quic {
namespace simulator {

const QuicStreamId kDataStream = 3;
const QuicByteCount kWriteChunkSize = 128 * 1024;
const char kStreamDataContents = 'Q';

QuicEndpoint::QuicEndpoint(Simulator* simulator,
                           std::string name,
                           std::string peer_name,
                           Perspective perspective,
                           QuicConnectionId connection_id)
    : QuicEndpointBase(simulator, name, peer_name),
      bytes_to_transfer_(0),
      bytes_transferred_(0),
      wrong_data_received_(false),
      notifier_(nullptr) {
  connection_ = std::make_unique<QuicConnection>(
      connection_id, GetAddressFromName(peer_name), simulator,
      simulator->GetAlarmFactory(), &writer_, false, perspective,
      ParsedVersionOfIndex(CurrentSupportedVersions(), 0));
  connection_->SetSelfAddress(GetAddressFromName(name));
  connection_->set_visitor(this);
  connection_->SetEncrypter(ENCRYPTION_FORWARD_SECURE,
                            std::make_unique<NullEncrypter>(perspective));
  connection_->SetEncrypter(ENCRYPTION_INITIAL, nullptr);
  if (connection_->version().KnowsWhichDecrypterToUse()) {
    connection_->InstallDecrypter(ENCRYPTION_FORWARD_SECURE,
                                  std::make_unique<NullDecrypter>(perspective));
    connection_->RemoveDecrypter(ENCRYPTION_INITIAL);
  } else {
    connection_->SetDecrypter(ENCRYPTION_FORWARD_SECURE,
                              std::make_unique<NullDecrypter>(perspective));
  }
  connection_->SetDefaultEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
  connection_->OnHandshakeComplete();
  if (perspective == Perspective::IS_SERVER) {
    // Skip version negotiation.
    test::QuicConnectionPeer::SetNegotiatedVersion(connection_.get());
  }
  connection_->SetDataProducer(&producer_);
  connection_->SetSessionNotifier(this);
  notifier_ = std::make_unique<test::SimpleSessionNotifier>(connection_.get());

  // Configure the connection as if it received a handshake.  This is important
  // primarily because
  //  - this enables pacing, and
  //  - this sets the non-handshake timeouts.
  std::string error;
  CryptoHandshakeMessage peer_hello;
  peer_hello.SetValue(kICSL,
                      static_cast<uint32_t>(kMaximumIdleTimeoutSecs - 1));
  peer_hello.SetValue(kMIBS,
                      static_cast<uint32_t>(kDefaultMaxStreamsPerConnection));
  QuicConfig config;
  QuicErrorCode error_code = config.ProcessPeerHello(
      peer_hello, perspective == Perspective::IS_CLIENT ? SERVER : CLIENT,
      &error);
  DCHECK_EQ(error_code, QUIC_NO_ERROR) << "Configuration failed: " << error;
  connection_->SetFromConfig(config);
}

QuicByteCount QuicEndpoint::bytes_received() const {
  QuicByteCount total = 0;
  for (auto& interval : offsets_received_) {
    total += interval.max() - interval.min();
  }
  return total;
}

QuicByteCount QuicEndpoint::bytes_to_transfer() const {
  if (notifier_ != nullptr) {
    return notifier_->StreamBytesToSend();
  }
  return bytes_to_transfer_;
}

QuicByteCount QuicEndpoint::bytes_transferred() const {
  if (notifier_ != nullptr) {
    return notifier_->StreamBytesSent();
  }
  return bytes_transferred_;
}

void QuicEndpoint::AddBytesToTransfer(QuicByteCount bytes) {
  if (notifier_ != nullptr) {
    if (notifier_->HasBufferedStreamData()) {
      Schedule(clock_->Now());
    }
    notifier_->WriteOrBufferData(kDataStream, bytes, NO_FIN);
    return;
  }

  if (bytes_to_transfer_ > 0) {
    Schedule(clock_->Now());
  }

  bytes_to_transfer_ += bytes;
  WriteStreamData();
}

void QuicEndpoint::OnStreamFrame(const QuicStreamFrame& frame) {
  // Verify that the data received always matches the expected.
  DCHECK(frame.stream_id == kDataStream);
  for (size_t i = 0; i < frame.data_length; i++) {
    if (frame.data_buffer[i] != kStreamDataContents) {
      wrong_data_received_ = true;
    }
  }
  offsets_received_.Add(frame.offset, frame.offset + frame.data_length);
  // Sanity check against very pathological connections.
  DCHECK_LE(offsets_received_.Size(), 1000u);
}

void QuicEndpoint::OnCryptoFrame(const QuicCryptoFrame& /*frame*/) {}

void QuicEndpoint::OnCanWrite() {
  if (notifier_ != nullptr) {
    notifier_->OnCanWrite();
    return;
  }
  WriteStreamData();
}

bool QuicEndpoint::SendProbingData() {
  if (connection()->sent_packet_manager().MaybeRetransmitOldestPacket(
          PROBING_RETRANSMISSION)) {
    return true;
  }
  return false;
}

bool QuicEndpoint::WillingAndAbleToWrite() const {
  if (notifier_ != nullptr) {
    return notifier_->WillingToWrite();
  }
  return bytes_to_transfer_ != 0;
}
bool QuicEndpoint::HasPendingHandshake() const {
  return false;
}
bool QuicEndpoint::ShouldKeepConnectionAlive() const {
  return true;
}

bool QuicEndpoint::AllowSelfAddressChange() const {
  return false;
}

bool QuicEndpoint::OnFrameAcked(const QuicFrame& frame,
                                QuicTime::Delta ack_delay_time,
                                QuicTime receive_timestamp) {
  if (notifier_ != nullptr) {
    return notifier_->OnFrameAcked(frame, ack_delay_time, receive_timestamp);
  }
  return false;
}

void QuicEndpoint::OnFrameLost(const QuicFrame& frame) {
  DCHECK(notifier_);
  notifier_->OnFrameLost(frame);
}

void QuicEndpoint::RetransmitFrames(const QuicFrames& frames,
                                    TransmissionType type) {
  DCHECK(notifier_);
  notifier_->RetransmitFrames(frames, type);
}

bool QuicEndpoint::IsFrameOutstanding(const QuicFrame& frame) const {
  DCHECK(notifier_);
  return notifier_->IsFrameOutstanding(frame);
}

bool QuicEndpoint::HasUnackedCryptoData() const {
  return false;
}

bool QuicEndpoint::HasUnackedStreamData() const {
  if (notifier_ != nullptr) {
    return notifier_->HasUnackedStreamData();
  }
  return false;
}

HandshakeState QuicEndpoint::GetHandshakeState() const {
  return HANDSHAKE_COMPLETE;
}

WriteStreamDataResult QuicEndpoint::DataProducer::WriteStreamData(
    QuicStreamId /*id*/,
    QuicStreamOffset /*offset*/,
    QuicByteCount data_length,
    QuicDataWriter* writer) {
  writer->WriteRepeatedByte(kStreamDataContents, data_length);
  return WRITE_SUCCESS;
}

bool QuicEndpoint::DataProducer::WriteCryptoData(EncryptionLevel /*level*/,
                                                 QuicStreamOffset /*offset*/,
                                                 QuicByteCount /*data_length*/,
                                                 QuicDataWriter* /*writer*/) {
  QUIC_BUG << "QuicEndpoint::DataProducer::WriteCryptoData is unimplemented";
  return false;
}

void QuicEndpoint::WriteStreamData() {
  // Instantiate a flusher which would normally be here due to QuicSession.
  QuicConnection::ScopedPacketFlusher flusher(connection_.get());

  while (bytes_to_transfer_ > 0) {
    // Transfer data in chunks of size at most |kWriteChunkSize|.
    const size_t transmission_size =
        std::min(kWriteChunkSize, bytes_to_transfer_);

    QuicConsumedData consumed_data = connection_->SendStreamData(
        kDataStream, transmission_size, bytes_transferred_, NO_FIN);

    DCHECK(consumed_data.bytes_consumed <= transmission_size);
    bytes_transferred_ += consumed_data.bytes_consumed;
    bytes_to_transfer_ -= consumed_data.bytes_consumed;
    if (consumed_data.bytes_consumed != transmission_size) {
      return;
    }
  }
}

}  // namespace simulator
}  // namespace quic

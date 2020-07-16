// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/quartc_multiplexer.h"

#include <cstdint>
#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice_span.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

QuartcSendChannel::QuartcSendChannel(QuartcMultiplexer* multiplexer,
                                     uint64_t id,
                                     QuicBufferAllocator* allocator,
                                     Delegate* delegate)
    : multiplexer_(multiplexer),
      id_(id),
      encoded_length_(QuicDataWriter::GetVarInt62Len(id_)),
      allocator_(allocator),
      delegate_(delegate) {}

QuartcStream* QuartcSendChannel::CreateOutgoingBidirectionalStream() {
  if (!session_) {
    QUIC_LOG(DFATAL) << "Session is not ready to write yet; channel_id=" << id_;
    return nullptr;
  }
  QuicMemSlice id_slice = EncodeChannelId();

  QuartcStream* stream = session_->CreateOutgoingBidirectionalStream();
  QuicConsumedData consumed =
      stream->WriteMemSlices(QuicMemSliceSpan(&id_slice), /*fin=*/false);
  DCHECK_EQ(consumed.bytes_consumed, encoded_length_);
  return stream;
}

bool QuartcSendChannel::SendOrQueueMessage(QuicMemSliceSpan message,
                                           int64_t datagram_id) {
  if (!session_) {
    QUIC_LOG(DFATAL) << "Session is not ready to write yet; channel_id=" << id_
                     << "datagram size=" << message.total_length();
    return false;
  }
  QuicMemSliceStorage storage(nullptr, 0, nullptr, 0);  // Empty storage.
  storage.Append(EncodeChannelId());

  message.ConsumeAll(
      [&storage](QuicMemSlice slice) { storage.Append(std::move(slice)); });

  // Allocate a unique datagram id so that notifications can be routed back to
  // the right send channel.
  int64_t unique_datagram_id = multiplexer_->AllocateDatagramId(this);
  multiplexer_to_user_datagram_ids_[unique_datagram_id] = datagram_id;

  return session_->SendOrQueueMessage(storage.ToSpan(), unique_datagram_id);
}

void QuartcSendChannel::OnMessageSent(int64_t datagram_id) {
  // Map back to the caller-chosen |datagram_id|.
  datagram_id = multiplexer_to_user_datagram_ids_[datagram_id];
  delegate_->OnMessageSent(datagram_id);
}

void QuartcSendChannel::OnMessageAcked(int64_t datagram_id,
                                       QuicTime receive_timestamp) {
  // Map back to the caller-chosen |datagram_id|.
  auto it = multiplexer_to_user_datagram_ids_.find(datagram_id);
  if (it == multiplexer_to_user_datagram_ids_.end()) {
    QUIC_LOG(DFATAL) << "Datagram acked/lost multiple times; datagram_id="
                     << datagram_id;
    return;
  }
  delegate_->OnMessageAcked(it->second, receive_timestamp);
  multiplexer_to_user_datagram_ids_.erase(it);
}

void QuartcSendChannel::OnMessageLost(int64_t datagram_id) {
  // Map back to the caller-chosen |datagram_id|.
  auto it = multiplexer_to_user_datagram_ids_.find(datagram_id);
  if (it == multiplexer_to_user_datagram_ids_.end()) {
    QUIC_LOG(DFATAL) << "Datagram acked/lost multiple times; datagram_id="
                     << datagram_id;
    return;
  }
  delegate_->OnMessageLost(it->second);
  multiplexer_to_user_datagram_ids_.erase(it);
}

void QuartcSendChannel::OnSessionCreated(QuartcSession* session) {
  session_ = session;
}

QuicMemSlice QuartcSendChannel::EncodeChannelId() {
  QuicUniqueBufferPtr buffer = MakeUniqueBuffer(allocator_, encoded_length_);
  QuicDataWriter writer(encoded_length_, buffer.get());
  writer.WriteVarInt62(id_);
  return QuicMemSlice(std::move(buffer), encoded_length_);
}

QuartcMultiplexer::QuartcMultiplexer(
    QuicBufferAllocator* allocator,
    QuartcSessionEventDelegate* session_delegate,
    QuartcReceiveChannel* default_receive_channel)
    : allocator_(allocator),
      session_delegate_(session_delegate),
      default_receive_channel_(default_receive_channel) {
  CHECK_NE(session_delegate_, nullptr);
  CHECK_NE(default_receive_channel_, nullptr);
}

QuartcSendChannel* QuartcMultiplexer::CreateSendChannel(
    uint64_t channel_id,
    QuartcSendChannel::Delegate* delegate) {
  send_channels_.push_back(std::make_unique<QuartcSendChannel>(
      this, channel_id, allocator_, delegate));
  if (session_) {
    send_channels_.back()->OnSessionCreated(session_);
  }
  return send_channels_.back().get();
}

void QuartcMultiplexer::RegisterReceiveChannel(uint64_t channel_id,
                                               QuartcReceiveChannel* channel) {
  if (channel == nullptr) {
    receive_channels_.erase(channel_id);
    return;
  }
  auto& registered_channel = receive_channels_[channel_id];
  if (registered_channel) {
    QUIC_LOG(DFATAL) << "Attempted to overwrite existing channel_id="
                     << channel_id;
    return;
  }
  registered_channel = channel;
}

int64_t QuartcMultiplexer::AllocateDatagramId(QuartcSendChannel* channel) {
  send_channels_by_datagram_id_[next_datagram_id_] = channel;
  return next_datagram_id_++;
}

void QuartcMultiplexer::OnSessionCreated(QuartcSession* session) {
  for (auto& channel : send_channels_) {
    channel->OnSessionCreated(session);
  }
  session_ = session;
  session_delegate_->OnSessionCreated(session);
}

void QuartcMultiplexer::OnCryptoHandshakeComplete() {
  session_delegate_->OnCryptoHandshakeComplete();
}

void QuartcMultiplexer::OnConnectionWritable() {
  session_delegate_->OnConnectionWritable();
}

void QuartcMultiplexer::OnIncomingStream(QuartcStream* stream) {
  stream->SetDelegate(this);
}

void QuartcMultiplexer::OnCongestionControlChange(
    QuicBandwidth bandwidth_estimate,
    QuicBandwidth pacing_rate,
    QuicTime::Delta latest_rtt) {
  session_delegate_->OnCongestionControlChange(bandwidth_estimate, pacing_rate,
                                               latest_rtt);
}

void QuartcMultiplexer::OnConnectionClosed(
    const QuicConnectionCloseFrame& frame,
    ConnectionCloseSource source) {
  session_delegate_->OnConnectionClosed(frame, source);
}

void QuartcMultiplexer::OnMessageReceived(quiche::QuicheStringPiece message) {
  QuicDataReader reader(message);
  QuicVariableLengthIntegerLength channel_id_length =
      reader.PeekVarInt62Length();

  uint64_t channel_id;
  if (!reader.ReadVarInt62(&channel_id)) {
    QUIC_LOG(DFATAL) << "Received message without properly encoded channel id";
    return;
  }

  QuartcReceiveChannel* channel = default_receive_channel_;
  auto it = receive_channels_.find(channel_id);
  if (it != receive_channels_.end()) {
    channel = it->second;
  }

  channel->OnMessageReceived(channel_id, message.substr(channel_id_length));
}

void QuartcMultiplexer::OnMessageSent(int64_t datagram_id) {
  auto it = send_channels_by_datagram_id_.find(datagram_id);
  if (it == send_channels_by_datagram_id_.end()) {
    return;
  }
  it->second->OnMessageSent(datagram_id);
}

void QuartcMultiplexer::OnMessageAcked(int64_t datagram_id,
                                       QuicTime receive_timestamp) {
  auto it = send_channels_by_datagram_id_.find(datagram_id);
  if (it == send_channels_by_datagram_id_.end()) {
    return;
  }
  it->second->OnMessageAcked(datagram_id, receive_timestamp);
  send_channels_by_datagram_id_.erase(it);
}

void QuartcMultiplexer::OnMessageLost(int64_t datagram_id) {
  auto it = send_channels_by_datagram_id_.find(datagram_id);
  if (it == send_channels_by_datagram_id_.end()) {
    return;
  }
  it->second->OnMessageLost(datagram_id);
  send_channels_by_datagram_id_.erase(it);
}

size_t QuartcMultiplexer::OnReceived(QuartcStream* stream,
                                     iovec* iov,
                                     size_t iov_length,
                                     bool /*fin*/) {
  if (iov == nullptr || iov_length <= 0) {
    return 0;
  }

  QuicDataReader reader(static_cast<char*>(iov[0].iov_base), iov[0].iov_len);
  QuicVariableLengthIntegerLength channel_id_length =
      reader.PeekVarInt62Length();

  uint64_t channel_id;
  if (reader.BytesRemaining() >= channel_id_length) {
    // Fast path, have enough data to read immediately.
    if (!reader.ReadVarInt62(&channel_id)) {
      return 0;
    }
  } else {
    // Slow path, need to coalesce multiple iovecs.
    std::string data;
    for (size_t i = 0; i < iov_length; ++i) {
      data += std::string(static_cast<char*>(iov[i].iov_base), iov[i].iov_len);
    }
    QuicDataReader combined_reader(data);
    if (!combined_reader.ReadVarInt62(&channel_id)) {
      return 0;
    }
  }

  QuartcReceiveChannel* channel = default_receive_channel_;
  auto it = receive_channels_.find(channel_id);
  if (it != receive_channels_.end()) {
    channel = it->second;
  }
  channel->OnIncomingStream(channel_id, stream);
  return channel_id_length;
}

void QuartcMultiplexer::OnClose(QuartcStream* /*stream*/) {}

void QuartcMultiplexer::OnBufferChanged(QuartcStream* /*stream*/) {}

}  // namespace quic

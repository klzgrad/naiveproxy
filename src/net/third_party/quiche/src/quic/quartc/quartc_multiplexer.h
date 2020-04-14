// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUARTC_QUARTC_MULTIPLEXER_H_
#define QUICHE_QUIC_QUARTC_QUARTC_MULTIPLEXER_H_

#include <cstdint>

#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice_span.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_endpoint.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_session.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_stream.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class QuartcMultiplexer;

// A single, multiplexed send channel within a Quartc session.  A send channel
// wraps send-side operations with an outgoing multiplex id.
class QuartcSendChannel {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when a message with |datagram_id| is sent by this channel.
    virtual void OnMessageSent(int64_t datagram_id) = 0;

    // Called when a message sent on this channel with |datagram_id| is acked.
    // |receive_timestamp| indicates when the peer received this message,
    // according to the peer's clock.
    virtual void OnMessageAcked(int64_t datagram_id,
                                QuicTime receive_timestamp) = 0;

    // Called when a message sent on this channel with |datagram_id| is lost.
    virtual void OnMessageLost(int64_t datagram_id) = 0;
  };

  QuartcSendChannel(QuartcMultiplexer* multiplexer,
                    uint64_t id,
                    QuicBufferAllocator* allocator,
                    Delegate* delegate);
  virtual ~QuartcSendChannel() = default;

  // Creates a new, outgoing stream on this channel.
  //
  // Automatically writes the channel id to the start of the stream.  The caller
  // SHOULD create a |ScopedPacketFlusher| before calling this function to
  // prevent the channel id from being sent by itself.
  QuartcStream* CreateOutgoingBidirectionalStream();

  // Writes |message| to the session.  Prepends the channel's send id before any
  // following message data.
  bool SendOrQueueMessage(QuicMemSliceSpan message, int64_t datagram_id);

  // Gets the current largest message payload for this channel.  Returns the
  // largest payload size supported by the session minus overhead required to
  // encode this channel's send id.
  QuicPacketLength GetCurrentLargestMessagePayload() const;

  // The following are called by the multiplexer to deliver message
  // notifications.  The |datagram_id| passed to these is unique per-message,
  // and must be translated back to the sender's chosen datagram_id.
  void OnMessageSent(int64_t datagram_id);
  void OnMessageAcked(int64_t datagram_id, QuicTime receive_timestamp);
  void OnMessageLost(int64_t datagram_id);
  void OnSessionCreated(QuartcSession* session);

 private:
  // Creates a mem slice containing a varint-62 encoded channel id.
  QuicMemSlice EncodeChannelId();

  QuartcMultiplexer* const multiplexer_;
  const uint64_t id_;
  const QuicVariableLengthIntegerLength encoded_length_;
  QuicBufferAllocator* const allocator_;
  Delegate* const delegate_;

  QuartcSession* session_;

  // Map of multiplexer-chosen to user/caller-specified datagram ids.  The user
  // may specify any number as a datagram's id.  This number does not have to be
  // unique across channels (nor even within a single channel).  In order
  // to demux sent, acked, and lost messages, the multiplexer assigns a globally
  // unique id to each message.  This map is used to restore the original caller
  // datagram id before issuing callbacks.
  QuicUnorderedMap<int64_t, int64_t> multiplexer_to_user_datagram_ids_;
};

// A single, multiplexed receive channel within a Quartc session.  A receive
// channel is a delegate which accepts incoming streams and datagrams on one (or
// more) channel ids.
class QuartcReceiveChannel {
 public:
  virtual ~QuartcReceiveChannel() = default;

  // Called when a new incoming stream arrives on this channel.
  virtual void OnIncomingStream(uint64_t channel_id, QuartcStream* stream) = 0;

  // Called when a message is recieved by this channel.
  virtual void OnMessageReceived(uint64_t channel_id,
                                 quiche::QuicheStringPiece message) = 0;
};

// Delegate for session-wide events.
class QuartcSessionEventDelegate {
 public:
  virtual ~QuartcSessionEventDelegate() = default;

  virtual void OnSessionCreated(QuartcSession* session) = 0;
  virtual void OnCryptoHandshakeComplete() = 0;
  virtual void OnConnectionWritable() = 0;
  virtual void OnCongestionControlChange(QuicBandwidth bandwidth_estimate,
                                         QuicBandwidth pacing_rate,
                                         QuicTime::Delta latest_rtt) = 0;
  virtual void OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                                  ConnectionCloseSource source) = 0;
};

// A multiplexer capable of sending and receiving data on multiple channels.
class QuartcMultiplexer : public QuartcEndpoint::Delegate,
                          public QuartcStream::Delegate {
 public:
  // Creates a new multiplexer.  |session_delegate| handles all session-wide
  // events, while |default_receive_channel| handles incoming data on unknown
  // or unregistered channel ids.  Neither |session_delegate| nor
  // |default_receive_channel| may be nullptr, and both must outlive the
  // multiplexer.
  QuartcMultiplexer(QuicBufferAllocator* allocator,
                    QuartcSessionEventDelegate* session_delegate,
                    QuartcReceiveChannel* default_receive_channel);

  // Creates a new send channel.  The channel is owned by the multiplexer, and
  // references to it must not outlive the multiplexer.
  QuartcSendChannel* CreateSendChannel(uint64_t channel_id,
                                       QuartcSendChannel::Delegate* delegate);

  // Registers a receiver for incoming data on |channel_id|.
  void RegisterReceiveChannel(uint64_t channel_id,
                              QuartcReceiveChannel* channel);

  // Allocates a datagram id to |channel|.
  int64_t AllocateDatagramId(QuartcSendChannel* channel);

  // QuartcEndpoint::Delegate overrides.
  void OnSessionCreated(QuartcSession* session) override;

  // QuartcSession::Delegate overrides.
  void OnCryptoHandshakeComplete() override;
  void OnConnectionWritable() override;
  void OnIncomingStream(QuartcStream* stream) override;
  void OnCongestionControlChange(QuicBandwidth bandwidth_estimate,
                                 QuicBandwidth pacing_rate,
                                 QuicTime::Delta latest_rtt) override;
  void OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                          ConnectionCloseSource source) override;
  void OnMessageReceived(quiche::QuicheStringPiece message) override;
  void OnMessageSent(int64_t datagram_id) override;
  void OnMessageAcked(int64_t datagram_id, QuicTime receive_timestamp) override;
  void OnMessageLost(int64_t datagram_id) override;

  // QuartcStream::Delegate overrides.
  size_t OnReceived(QuartcStream* stream,
                    iovec* iov,
                    size_t iov_length,
                    bool fin) override;
  void OnClose(QuartcStream* stream) override;
  void OnBufferChanged(QuartcStream* stream) override;

 private:
  QuicBufferAllocator* const allocator_;
  QuartcSessionEventDelegate* const session_delegate_;

  QuartcSession* session_ = nullptr;
  std::vector<std::unique_ptr<QuartcSendChannel>> send_channels_;
  QuicUnorderedMap<uint64_t, QuartcReceiveChannel*> receive_channels_;
  QuartcReceiveChannel* default_receive_channel_ = nullptr;

  int64_t next_datagram_id_ = 1;
  QuicUnorderedMap<int64_t, QuartcSendChannel*> send_channels_by_datagram_id_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QUARTC_QUARTC_MULTIPLEXER_H_

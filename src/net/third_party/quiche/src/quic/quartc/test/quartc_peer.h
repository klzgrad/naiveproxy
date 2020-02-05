// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUARTC_TEST_QUARTC_PEER_H_
#define QUICHE_QUIC_QUARTC_TEST_QUARTC_PEER_H_

#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_alarm_factory.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_endpoint.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_session.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_stream.h"
#include "net/third_party/quiche/src/quic/quartc/test/quartc_data_source.h"

namespace quic {
namespace test {

// Map of source id to sequence number.
using IdToSequenceNumberMap = std::map<int32_t, int64_t>;

// ParsedQuartcDataFrame with a receive_time.
struct ReceivedMessage {
  ParsedQuartcDataFrame frame;
  QuicTime receive_time = QuicTime::Zero();
};

// Test utility that adapts QuartcDataSources to a QuartcSession.
// The utility creates and manages a set of QuartcDataSources.  It sends the
// data produced by those sources as QUIC datagram frames.  It reconfigures the
// maximum frame size of each source in order to fit test frames into QUIC
// datagram frames.  It also adjusts the bitrate of each source to fit within
// the bandwidth available to the session.
class QuartcPeer : public QuartcEndpoint::Delegate,
                   public QuartcDataSource::Delegate {
 public:
  // Creates a QuartcPeer that sends data from a set of sources described by
  // |configs|.  Note that the max frame size of each config may be adjusted in
  // order to fit within the constraints of the QUIC session.
  QuartcPeer(const QuicClock* clock,
             QuicAlarmFactory* alarm_factory,
             QuicRandom* random,
             QuicBufferAllocator* buffer_allocator,
             const std::vector<QuartcDataSource::Config>& configs);
  QuartcPeer(QuartcPeer&) = delete;
  QuartcPeer& operator=(QuartcPeer&) = delete;

  ~QuartcPeer();

  // Enable or disable this peer.  Disabling a peer causes it to stop sending
  // messages (which may be useful for flushing data during tests).
  // A peer begins disabled.  It automatically enables itself as soon as its
  // session becomes writable, and disables itself when its session closes.
  bool Enabled() const { return enabled_; }
  void SetEnabled(bool value);

  // Messages received from the peer, in the order they were received.
  const std::vector<ReceivedMessage>& received_messages() const {
    return received_messages_;
  }

  // Returns a map of source id to the sequence number of the last frame
  // produced by that source.
  IdToSequenceNumberMap GetLastSequenceNumbers() const;

  QuicBandwidth last_available_bandwidth() { return last_available_; }

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
  void OnMessageReceived(QuicStringPiece message) override;
  void OnMessageSent(int64_t /*datagram_id*/) override {}
  void OnMessageAcked(int64_t /*datagram_id*/,
                      QuicTime /*receive_timestamp*/) override {}
  void OnMessageLost(int64_t /*datagram_id*/) override {}

  // QuartcDataSource::Delegate overrides.
  void OnDataProduced(const char* data, size_t length) override;

 private:
  const QuicClock* clock_;
  QuicAlarmFactory* alarm_factory_;
  QuicRandom* random_;
  QuicBufferAllocator* buffer_allocator_;

  // Whether the peer is currently sending.
  bool enabled_;

  // Session used for sending and receiving data.  Not owned.  Created by an
  // external QuartcEndpoint and set in the |OnSessionCreated| callback.
  QuartcSession* session_;

  // Saved copy of the configs for data sources.  These configs may be modified
  // before |data_sources_| are initialized (for example, to set appropriate
  // max frame sizes).
  std::vector<QuartcDataSource::Config> configs_;

  // Data sources are initialized once the session is created and enabled once
  // the session is able to send.
  std::vector<std::unique_ptr<QuartcDataSource>> data_sources_;

  // Messages received by this peer from the remote peer.  Stored in the order
  // they are received.
  std::vector<ReceivedMessage> received_messages_;

  // Last available bandwidth.
  QuicBandwidth last_available_;
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_QUARTC_FAKE_QUARTC_PEER_H_

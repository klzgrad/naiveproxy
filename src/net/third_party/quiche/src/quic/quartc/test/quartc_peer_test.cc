// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/test/quartc_peer.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_endpoint.h"
#include "net/third_party/quiche/src/quic/quartc/simulated_packet_transport.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/link.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"

namespace quic {
namespace test {
namespace {

class QuartcPeerTest : public QuicTest {
 protected:
  QuartcPeerTest()
      : client_transport_(&simulator_,
                          "client_transport",
                          "server_transport",
                          10 * kDefaultMaxPacketSize),
        server_transport_(&simulator_,
                          "server_transport",
                          "client_transport",
                          10 * kDefaultMaxPacketSize),
        client_server_link_(&client_transport_,
                            &server_transport_,
                            QuicBandwidth::FromKBitsPerSecond(512),
                            QuicTime::Delta::FromMilliseconds(100)) {
    SetQuicReloadableFlag(quic_default_to_bbr, true);
    simulator_.set_random_generator(&rng_);
  }

  void CreatePeers(const std::vector<QuartcDataSource::Config>& configs) {
    client_peer_ = std::make_unique<QuartcPeer>(
        simulator_.GetClock(), simulator_.GetAlarmFactory(),
        simulator_.GetRandomGenerator(),
        simulator_.GetStreamSendBufferAllocator(), configs);
    server_peer_ = std::make_unique<QuartcPeer>(
        simulator_.GetClock(), simulator_.GetAlarmFactory(),
        simulator_.GetRandomGenerator(),
        simulator_.GetStreamSendBufferAllocator(), configs);
  }

  void Connect() {
    DCHECK(client_peer_);
    DCHECK(server_peer_);

    server_endpoint_ = std::make_unique<QuartcServerEndpoint>(
        simulator_.GetAlarmFactory(), simulator_.GetClock(),
        simulator_.GetRandomGenerator(), server_peer_.get(),
        QuartcSessionConfig());
    client_endpoint_ = std::make_unique<QuartcClientEndpoint>(
        simulator_.GetAlarmFactory(), simulator_.GetClock(),
        simulator_.GetRandomGenerator(), client_peer_.get(),
        QuartcSessionConfig(), server_endpoint_->server_crypto_config());

    server_endpoint_->Connect(&server_transport_);
    client_endpoint_->Connect(&client_transport_);
  }

  void RampUpBandwidth() {
    // Run long enough for the bandwidth estimate to ramp up.
    simulator_.RunUntilOrTimeout(
        [this] {
          return client_peer_->last_available_bandwidth() ==
                     client_server_link_.bandwidth() &&
                 server_peer_->last_available_bandwidth() ==
                     client_server_link_.bandwidth();
        },
        QuicTime::Delta::FromSeconds(60));
  }

  SimpleRandom rng_;
  simulator::Simulator simulator_;
  simulator::SimulatedQuartcPacketTransport client_transport_;
  simulator::SimulatedQuartcPacketTransport server_transport_;
  simulator::SymmetricLink client_server_link_;

  std::unique_ptr<QuartcClientEndpoint> client_endpoint_;
  std::unique_ptr<QuartcPeer> client_peer_;

  std::unique_ptr<QuartcServerEndpoint> server_endpoint_;
  std::unique_ptr<QuartcPeer> server_peer_;
};

const ReceivedMessage& FindLastMessageFromSource(
    const std::vector<ReceivedMessage>& messages,
    int32_t source_id) {
  const auto& it = std::find_if(messages.rbegin(), messages.rend(),
                                [source_id](const ReceivedMessage& r) {
                                  return r.frame.source_id == source_id;
                                });
  return *it;
}

TEST_F(QuartcPeerTest, SendReceiveMessages) {
  QuicTime start_time = simulator_.GetClock()->Now();

  QuartcDataSource::Config config;
  config.id = 1;

  CreatePeers({config});
  Connect();

  ASSERT_TRUE(simulator_.RunUntil([this] {
    return !client_peer_->received_messages().empty() &&
           !server_peer_->received_messages().empty();
  }));

  QuicTime end_time = simulator_.GetClock()->Now();

  // Sanity checks on messages.
  const ReceivedMessage& client_message = client_peer_->received_messages()[0];
  EXPECT_EQ(client_message.frame.source_id, 1);
  EXPECT_EQ(client_message.frame.sequence_number, 0);
  EXPECT_GE(client_message.frame.send_time, start_time);
  EXPECT_LE(client_message.receive_time, end_time);

  const ReceivedMessage& server_message = server_peer_->received_messages()[0];
  EXPECT_EQ(server_message.frame.source_id, 1);
  EXPECT_EQ(server_message.frame.sequence_number, 0);
  EXPECT_GE(server_message.frame.send_time, start_time);
  EXPECT_LE(server_message.receive_time, end_time);
}

TEST_F(QuartcPeerTest, MaxFrameSizeUnset) {
  // Configure the source with no max frame size, and a framerate and max
  // bandwidth that allows very large frames (larger than will fit in a packet).
  QuartcDataSource::Config config;
  config.id = 1;
  config.frame_interval = QuicTime::Delta::FromMilliseconds(20);
  config.max_bandwidth = QuicBandwidth::FromBytesAndTimeDelta(
      2 * kDefaultMaxPacketSize, config.frame_interval);

  CreatePeers({config});
  Connect();
  RampUpBandwidth();

  // The peers generate frames that fit in one packet.
  EXPECT_LT(client_peer_->received_messages().back().frame.size,
            kDefaultMaxPacketSize);
  EXPECT_LT(server_peer_->received_messages().back().frame.size,
            kDefaultMaxPacketSize);
}

TEST_F(QuartcPeerTest, MaxFrameSizeLargerThanPacketSize) {
  // Configure the source with a max frame size larger than the packet size.
  QuartcDataSource::Config config;
  config.id = 1;
  config.max_frame_size = 2 * kDefaultMaxPacketSize;

  CreatePeers({config});
  Connect();
  RampUpBandwidth();

  // The peers generate frames that fit in one packet.
  EXPECT_LT(client_peer_->received_messages().back().frame.size,
            kDefaultMaxPacketSize);
  EXPECT_LT(server_peer_->received_messages().back().frame.size,
            kDefaultMaxPacketSize);
}

TEST_F(QuartcPeerTest, MaxFrameSizeSmallerThanPacketSize) {
  QuartcDataSource::Config config;
  config.id = 1;
  config.max_frame_size = 100;
  // Note that a long frame interval helps to ensure that the test produces
  // enough bytes per frame to reach max_frame_size.
  config.frame_interval = QuicTime::Delta::FromMilliseconds(100);

  CreatePeers({config});
  Connect();
  RampUpBandwidth();

  EXPECT_EQ(client_peer_->received_messages().back().frame.size, 100u);
  EXPECT_EQ(server_peer_->received_messages().back().frame.size, 100u);
}

TEST_F(QuartcPeerTest, MaxFrameSizeSmallerThanFrameHeader) {
  QuartcDataSource::Config config;
  config.id = 1;
  config.max_frame_size = kDataFrameHeaderSize - 1;

  CreatePeers({config});
  Connect();
  RampUpBandwidth();

  // Max frame sizes smaller than the header are ignored, and the frame size is
  // limited by packet size.
  EXPECT_LT(client_peer_->received_messages().back().frame.size,
            kDefaultMaxPacketSize);
  EXPECT_LT(server_peer_->received_messages().back().frame.size,
            kDefaultMaxPacketSize);
}

TEST_F(QuartcPeerTest, SendReceiveMultipleSources) {
  QuicTime start_time = simulator_.GetClock()->Now();

  // Note: use of really long frame intervals means that each source should send
  // one frame during this test.  This simplifies expectations for received
  // data.
  QuartcDataSource::Config config_1;
  config_1.id = 1;
  config_1.max_bandwidth = QuicBandwidth::FromKBitsPerSecond(32);
  config_1.frame_interval = QuicTime::Delta::FromSeconds(10);

  QuartcDataSource::Config config_2;
  config_2.id = 2;
  config_2.max_bandwidth = QuicBandwidth::FromKBitsPerSecond(64);
  config_2.frame_interval = QuicTime::Delta::FromSeconds(10);

  QuartcDataSource::Config config_3;
  config_3.id = 3;
  config_3.max_bandwidth = QuicBandwidth::FromKBitsPerSecond(128);
  config_3.frame_interval = QuicTime::Delta::FromSeconds(10);

  CreatePeers({config_1, config_2, config_3});
  Connect();

  ASSERT_TRUE(simulator_.RunUntil([this] {
    return client_peer_->received_messages().size() == 3 &&
           server_peer_->received_messages().size() == 3;
  }));

  QuicTime end_time = simulator_.GetClock()->Now();

  // Sanity checks on messages.
  const auto& order = [](const ReceivedMessage& lhs,
                         const ReceivedMessage& rhs) {
    return lhs.frame.source_id < rhs.frame.source_id;
  };

  std::vector<ReceivedMessage> client_messages =
      client_peer_->received_messages();
  std::sort(client_messages.begin(), client_messages.end(), order);
  for (size_t i = 0; i < client_messages.size(); ++i) {
    EXPECT_EQ(client_messages[i].frame.source_id, static_cast<int32_t>(i + 1));
    EXPECT_EQ(client_messages[i].frame.sequence_number, 0);
    EXPECT_GE(client_messages[i].frame.send_time, start_time);
    EXPECT_LE(client_messages[i].receive_time, end_time);
  }

  std::vector<ReceivedMessage> server_messages =
      server_peer_->received_messages();
  std::sort(server_messages.begin(), server_messages.end(), order);
  for (size_t i = 0; i < server_messages.size(); ++i) {
    EXPECT_EQ(server_messages[i].frame.source_id, static_cast<int32_t>(i + 1u));
    EXPECT_EQ(server_messages[i].frame.sequence_number, 0);
    EXPECT_GE(server_messages[i].frame.send_time, start_time);
    EXPECT_LE(server_messages[i].receive_time, end_time);
  }
}

TEST_F(QuartcPeerTest, BandwidthAllocationWithEnoughAvailable) {
  QuartcDataSource::Config config_1;
  config_1.id = 1;
  config_1.max_bandwidth = QuicBandwidth::FromKBitsPerSecond(32);
  config_1.frame_interval = QuicTime::Delta::FromMilliseconds(100);

  QuartcDataSource::Config config_2;
  config_2.id = 2;
  config_2.max_bandwidth = QuicBandwidth::FromKBitsPerSecond(64);
  config_2.frame_interval = QuicTime::Delta::FromMilliseconds(25);

  QuartcDataSource::Config config_3;
  config_3.id = 3;
  config_3.max_bandwidth = QuicBandwidth::FromKBitsPerSecond(128);
  config_3.frame_interval = QuicTime::Delta::FromMilliseconds(10);

  CreatePeers({config_1, config_2, config_3});
  Connect();
  RampUpBandwidth();

  // The last message from each source should be the size allowed by that
  // source's maximum bandwidth and frame interval.
  QuicByteCount source_1_size =
      config_1.max_bandwidth.ToBytesPerPeriod(config_1.frame_interval);
  QuicByteCount source_2_size =
      config_2.max_bandwidth.ToBytesPerPeriod(config_2.frame_interval);
  QuicByteCount source_3_size =
      config_3.max_bandwidth.ToBytesPerPeriod(config_3.frame_interval);

  const std::vector<ReceivedMessage>& client_messages =
      client_peer_->received_messages();
  EXPECT_EQ(FindLastMessageFromSource(client_messages, 1).frame.size,
            source_1_size);
  EXPECT_EQ(FindLastMessageFromSource(client_messages, 2).frame.size,
            source_2_size);
  EXPECT_EQ(FindLastMessageFromSource(client_messages, 3).frame.size,
            source_3_size);

  const std::vector<ReceivedMessage>& server_messages =
      server_peer_->received_messages();
  EXPECT_EQ(FindLastMessageFromSource(server_messages, 1).frame.size,
            source_1_size);
  EXPECT_EQ(FindLastMessageFromSource(server_messages, 2).frame.size,
            source_2_size);
  EXPECT_EQ(FindLastMessageFromSource(server_messages, 3).frame.size,
            source_3_size);
}

TEST_F(QuartcPeerTest, BandwidthAllocationWithoutEnoughAvailable) {
  QuartcDataSource::Config config_1;
  config_1.id = 1;
  config_1.max_bandwidth = client_server_link_.bandwidth() * 0.5;
  config_1.frame_interval = QuicTime::Delta::FromMilliseconds(10);

  QuartcDataSource::Config config_2;
  config_2.id = 2;
  config_2.min_bandwidth = QuicBandwidth::FromKBitsPerSecond(32);
  config_2.max_bandwidth = client_server_link_.bandwidth();
  config_2.frame_interval = QuicTime::Delta::FromMilliseconds(5);

  QuartcDataSource::Config config_3;
  config_3.id = 3;
  config_3.min_bandwidth = QuicBandwidth::FromKBitsPerSecond(32);
  config_3.max_bandwidth = client_server_link_.bandwidth() * 2;
  config_3.frame_interval = QuicTime::Delta::FromMilliseconds(20);

  CreatePeers({config_1, config_2, config_3});
  Connect();
  RampUpBandwidth();

  const std::vector<ReceivedMessage>& client_messages =
      client_peer_->received_messages();
  const std::vector<ReceivedMessage>& server_messages =
      server_peer_->received_messages();

  // Source 1 eventually ramps up to full bandwidth.
  const QuicByteCount source_1_size =
      config_1.max_bandwidth.ToBytesPerPeriod(config_1.frame_interval);
  EXPECT_EQ(FindLastMessageFromSource(client_messages, 1).frame.size,
            source_1_size);
  EXPECT_EQ(FindLastMessageFromSource(server_messages, 1).frame.size,
            source_1_size);

  // Source 2 takes the remainder of available bandwidth.  However, the exact
  // value depends on the bandwidth estimate.
  const QuicByteCount source_2_min =
      config_2.min_bandwidth.ToBytesPerPeriod(config_2.frame_interval);
  const QuicByteCount source_2_max =
      config_2.max_bandwidth.ToBytesPerPeriod(config_2.frame_interval);
  EXPECT_GT(FindLastMessageFromSource(client_messages, 2).frame.size,
            source_2_min);
  EXPECT_LT(FindLastMessageFromSource(client_messages, 2).frame.size,
            source_2_max);
  EXPECT_GT(FindLastMessageFromSource(server_messages, 2).frame.size,
            source_2_min);
  EXPECT_LT(FindLastMessageFromSource(server_messages, 2).frame.size,
            source_2_max);

  // Source 3 gets only its minimum bandwidth.
  const QuicByteCount source_3_size =
      config_3.min_bandwidth.ToBytesPerPeriod(config_3.frame_interval);
  EXPECT_EQ(FindLastMessageFromSource(client_messages, 3).frame.size,
            source_3_size);
  EXPECT_EQ(FindLastMessageFromSource(server_messages, 3).frame.size,
            source_3_size);
}

TEST_F(QuartcPeerTest, DisableAndDrainMessages) {
  QuartcDataSource::Config config;
  config.id = 1;
  config.max_bandwidth = client_server_link_.bandwidth() * 0.5;
  config.frame_interval = QuicTime::Delta::FromMilliseconds(10);

  CreatePeers({config});
  Connect();

  // Note: this time is completely arbitrary, to allow messages to be sent.
  simulator_.RunFor(QuicTime::Delta::FromSeconds(10));

  // After these calls, we should observe no new messages.
  server_peer_->SetEnabled(false);
  client_peer_->SetEnabled(false);

  std::map<int32_t, int64_t> last_sent_by_client =
      client_peer_->GetLastSequenceNumbers();
  std::map<int32_t, int64_t> last_sent_by_server =
      server_peer_->GetLastSequenceNumbers();

  // Note: this time is completely arbitrary, to allow time for the peers to
  // generate new messages after being disabled.  The point of the test is that
  // they should not do that.
  simulator_.RunFor(QuicTime::Delta::FromSeconds(10));

  // Messages sent prior to disabling the peers are eventually received.
  EXPECT_TRUE(simulator_.RunUntilOrTimeout(
      [this, last_sent_by_client, last_sent_by_server]() mutable -> bool {
        return !client_peer_->received_messages().empty() &&
               client_peer_->received_messages().back().frame.sequence_number ==
                   last_sent_by_server[1] &&
               !server_peer_->received_messages().empty() &&
               server_peer_->received_messages().back().frame.sequence_number ==
                   last_sent_by_client[1];
      },
      QuicTime::Delta::FromSeconds(60)));
}

}  // namespace
}  // namespace test
}  // namespace quic

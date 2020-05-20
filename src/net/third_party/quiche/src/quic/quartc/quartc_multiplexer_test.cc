// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/quartc_multiplexer.h"

#include <memory>
#include <utility>

#include "net/third_party/quiche/src/quic/core/frames/quic_connection_close_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test_mem_slice_vector.h"
#include "net/third_party/quiche/src/quic/quartc/counting_packet_filter.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_endpoint.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_fakes.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_session.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_stream.h"
#include "net/third_party/quiche/src/quic/quartc/simulated_packet_transport.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/link.h"
#include "net/third_party/quiche/src/quic/test_tools/simulator/simulator.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace {

using ::testing::ElementsAreArray;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::Pair;

constexpr QuicTime::Delta kPropagationDelay =
    QuicTime::Delta::FromMilliseconds(10);

class FakeSessionEventDelegate : public QuartcSessionEventDelegate {
 public:
  void OnSessionCreated(QuartcSession* session) override {
    session->StartCryptoHandshake();
    session_ = session;
  }

  void OnConnectionWritable() override { ++writable_count_; }

  void OnCryptoHandshakeComplete() override { ++handshake_count_; }

  void OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                          ConnectionCloseSource source) override {
    error_ = frame.quic_error_code;
    close_source_ = source;
  }

  void OnCongestionControlChange(QuicBandwidth bandwidth_estimate,
                                 QuicBandwidth pacing_rate,
                                 QuicTime::Delta latest_rtt) override {
    latest_bandwidth_estimate_ = bandwidth_estimate;
    latest_pacing_rate_ = pacing_rate;
    latest_rtt_ = latest_rtt;
  }

  QuartcSession* session() { return session_; }
  int writable_count() const { return writable_count_; }
  int handshake_count() const { return handshake_count_; }
  QuicErrorCode error() const { return error_; }
  ConnectionCloseSource close_source() const { return close_source_; }
  QuicBandwidth latest_bandwidth_estimate() const {
    return latest_bandwidth_estimate_;
  }
  QuicBandwidth latest_pacing_rate() const { return latest_pacing_rate_; }
  QuicTime::Delta latest_rtt() const { return latest_rtt_; }

 private:
  QuartcSession* session_ = nullptr;
  int writable_count_ = 0;
  int handshake_count_ = 0;
  QuicErrorCode error_ = QUIC_NO_ERROR;
  ConnectionCloseSource close_source_;
  QuicBandwidth latest_bandwidth_estimate_ = QuicBandwidth::Zero();
  QuicBandwidth latest_pacing_rate_ = QuicBandwidth::Zero();
  QuicTime::Delta latest_rtt_ = QuicTime::Delta::Zero();
};

class FakeSendDelegate : public QuartcSendChannel::Delegate {
 public:
  void OnMessageSent(int64_t datagram_id) override {
    datagrams_sent_.push_back(datagram_id);
  }

  void OnMessageAcked(int64_t datagram_id,
                      QuicTime receive_timestamp) override {
    datagrams_acked_.push_back({datagram_id, receive_timestamp});
  }

  void OnMessageLost(int64_t datagram_id) override {
    datagrams_lost_.push_back(datagram_id);
  }

  const std::vector<int64_t>& datagrams_sent() const { return datagrams_sent_; }
  const std::vector<std::pair<int64_t, QuicTime>>& datagrams_acked() const {
    return datagrams_acked_;
  }
  const std::vector<int64_t>& datagrams_lost() const { return datagrams_lost_; }

 private:
  std::vector<int64_t> datagrams_sent_;
  std::vector<std::pair<int64_t, QuicTime>> datagrams_acked_;
  std::vector<int64_t> datagrams_lost_;
};

class FakeReceiveDelegate : public QuartcReceiveChannel,
                            public QuartcStream::Delegate {
 public:
  const std::vector<std::pair<uint64_t, std::string>> messages_received()
      const {
    return messages_received_;
  }

  void OnIncomingStream(uint64_t channel_id, QuartcStream* stream) override {
    stream->SetDelegate(this);
    stream_to_channel_id_[stream] = channel_id;
  }

  void OnMessageReceived(uint64_t channel_id,
                         quiche::QuicheStringPiece message) override {
    messages_received_.emplace_back(channel_id, message);
  }

  // Stream delegate overrides.
  size_t OnReceived(QuartcStream* stream,
                    iovec* iov,
                    size_t iov_length,
                    bool fin) override {
    if (!fin) {
      return 0;
    }

    size_t bytes = 0;
    std::string message;
    for (size_t i = 0; i < iov_length; ++i) {
      message +=
          std::string(static_cast<char*>(iov[i].iov_base), iov[i].iov_len);
      bytes += iov[i].iov_len;
    }
    QUIC_LOG(INFO) << "Received " << bytes << " byte message on channel "
                   << stream_to_channel_id_[stream];
    messages_received_.emplace_back(stream_to_channel_id_[stream], message);
    return bytes;
  }

  void OnClose(QuartcStream* stream) override {
    stream_to_channel_id_.erase(stream);
  }

  void OnBufferChanged(QuartcStream* /*stream*/) override {}

 private:
  std::vector<std::pair<uint64_t, std::string>> messages_received_;
  QuicUnorderedMap<QuartcStream*, uint64_t> stream_to_channel_id_;
};

class QuartcMultiplexerTest : public QuicTest {
 public:
  QuartcMultiplexerTest()
      : simulator_(),
        client_transport_(&simulator_,
                          "client_transport",
                          "server_transport",
                          10 * kDefaultMaxPacketSize),
        server_transport_(&simulator_,
                          "server_transport",
                          "client_transport",
                          10 * kDefaultMaxPacketSize),
        client_filter_(&simulator_, "client_filter", &client_transport_),
        client_server_link_(&client_filter_,
                            &server_transport_,
                            QuicBandwidth::FromKBitsPerSecond(10 * 1000),
                            kPropagationDelay),
        client_multiplexer_(simulator_.GetStreamSendBufferAllocator(),
                            &client_session_delegate_,
                            &client_default_receiver_),
        server_multiplexer_(simulator_.GetStreamSendBufferAllocator(),
                            &server_session_delegate_,
                            &server_default_receiver_),
        client_endpoint_(std::make_unique<QuartcClientEndpoint>(
            simulator_.GetAlarmFactory(),
            simulator_.GetClock(),
            simulator_.GetRandomGenerator(),
            &client_multiplexer_,
            quic::QuartcSessionConfig(),
            /*serialized_server_config=*/"")),
        server_endpoint_(std::make_unique<QuartcServerEndpoint>(
            simulator_.GetAlarmFactory(),
            simulator_.GetClock(),
            simulator_.GetRandomGenerator(),
            &server_multiplexer_,
            quic::QuartcSessionConfig())) {
    // TODO(b/150224094): Re-enable TLS handshake.
    // TODO(b/150236522): Parametrize by QUIC version.
    SetQuicReloadableFlag(quic_enable_version_draft_27, false);
    SetQuicReloadableFlag(quic_enable_version_draft_25_v3, false);
    SetQuicReloadableFlag(quic_enable_version_t050, false);
  }

  void Connect() {
    client_endpoint_->Connect(&client_transport_);
    server_endpoint_->Connect(&server_transport_);
    ASSERT_TRUE(simulator_.RunUntil([this]() {
      return client_session_delegate_.writable_count() > 0 &&
             server_session_delegate_.writable_count() > 0;
    }));
  }

  void Disconnect() {
    client_session_delegate_.session()->CloseConnection("test");
    server_session_delegate_.session()->CloseConnection("test");
  }

 protected:
  QuartcMultiplexer* client_multiplexer() { return &client_multiplexer_; }

  QuartcMultiplexer* server_multiplexer() { return &server_multiplexer_; }

  simulator::Simulator simulator_;

  simulator::SimulatedQuartcPacketTransport client_transport_;
  simulator::SimulatedQuartcPacketTransport server_transport_;
  simulator::CountingPacketFilter client_filter_;
  simulator::SymmetricLink client_server_link_;

  FakeSessionEventDelegate client_session_delegate_;
  FakeSessionEventDelegate server_session_delegate_;

  FakeReceiveDelegate client_default_receiver_;
  FakeReceiveDelegate server_default_receiver_;

  QuartcMultiplexer client_multiplexer_;
  QuartcMultiplexer server_multiplexer_;

  std::unique_ptr<QuartcClientEndpoint> client_endpoint_;
  std::unique_ptr<QuartcServerEndpoint> server_endpoint_;
};

TEST_F(QuartcMultiplexerTest, MultiplexMessages) {
  Connect();

  FakeSendDelegate send_delegate_1;
  QuartcSendChannel* send_channel_1 =
      client_multiplexer()->CreateSendChannel(1, &send_delegate_1);
  FakeSendDelegate send_delegate_2;
  QuartcSendChannel* send_channel_2 =
      client_multiplexer()->CreateSendChannel(2, &send_delegate_2);

  FakeReceiveDelegate receive_delegate_1;
  server_multiplexer()->RegisterReceiveChannel(1, &receive_delegate_1);

  int num_messages = 10;
  std::vector<std::pair<uint64_t, std::string>> messages_1;
  messages_1.reserve(num_messages);
  std::vector<std::pair<uint64_t, std::string>> messages_2;
  messages_2.reserve(num_messages);
  std::vector<int64_t> messages_sent_1;
  std::vector<int64_t> messages_sent_2;
  std::vector<testing::Matcher<std::pair<int64_t, QuicTime>>> ack_matchers_1;
  std::vector<testing::Matcher<std::pair<int64_t, QuicTime>>> ack_matchers_2;
  for (int i = 0; i < num_messages; ++i) {
    messages_1.emplace_back(1, quiche::QuicheStrCat("message for 1: ", i));
    test::QuicTestMemSliceVector slice_1(
        {std::make_pair(const_cast<char*>(messages_1.back().second.data()),
                        messages_1.back().second.size())});
    send_channel_1->SendOrQueueMessage(slice_1.span(), i);
    messages_sent_1.push_back(i);
    ack_matchers_1.push_back(Pair(i, Gt(QuicTime::Zero())));

    messages_2.emplace_back(2, quiche::QuicheStrCat("message for 2: ", i));
    test::QuicTestMemSliceVector slice_2(
        {std::make_pair(const_cast<char*>(messages_2.back().second.data()),
                        messages_2.back().second.size())});
    // Use i + 5 as the datagram id for channel 2, so that some of the ids
    // overlap and some are disjoint.
    send_channel_2->SendOrQueueMessage(slice_2.span(), i + 5);
    messages_sent_2.push_back(i + 5);
    ack_matchers_2.push_back(Pair(i + 5, Gt(QuicTime::Zero())));
  }

  EXPECT_TRUE(simulator_.RunUntil([&send_delegate_1, &send_delegate_2]() {
    return send_delegate_1.datagrams_acked().size() == 10 &&
           send_delegate_2.datagrams_acked().size() == 10;
  }));

  EXPECT_EQ(send_delegate_1.datagrams_sent(), messages_sent_1);
  EXPECT_EQ(send_delegate_2.datagrams_sent(), messages_sent_2);

  EXPECT_EQ(receive_delegate_1.messages_received(), messages_1);
  EXPECT_EQ(server_default_receiver_.messages_received(), messages_2);

  EXPECT_THAT(send_delegate_1.datagrams_acked(),
              ElementsAreArray(ack_matchers_1));
  EXPECT_THAT(send_delegate_2.datagrams_acked(),
              ElementsAreArray(ack_matchers_2));
}

TEST_F(QuartcMultiplexerTest, MultiplexStreams) {
  FakeSendDelegate send_delegate_1;
  QuartcSendChannel* send_channel_1 =
      client_multiplexer()->CreateSendChannel(1, &send_delegate_1);
  FakeSendDelegate send_delegate_2;
  QuartcSendChannel* send_channel_2 =
      client_multiplexer()->CreateSendChannel(2, &send_delegate_2);

  FakeQuartcStreamDelegate fake_send_stream_delegate;

  FakeReceiveDelegate receive_delegate_1;
  server_multiplexer()->RegisterReceiveChannel(1, &receive_delegate_1);

  Connect();

  int num_messages = 10;
  std::vector<std::pair<uint64_t, std::string>> messages_1;
  messages_1.reserve(num_messages);
  std::vector<std::pair<uint64_t, std::string>> messages_2;
  messages_2.reserve(num_messages);
  for (int i = 0; i < num_messages; ++i) {
    messages_1.emplace_back(1, quiche::QuicheStrCat("message for 1: ", i));
    test::QuicTestMemSliceVector slice_1(
        {std::make_pair(const_cast<char*>(messages_1.back().second.data()),
                        messages_1.back().second.size())});
    QuartcStream* stream_1 =
        send_channel_1->CreateOutgoingBidirectionalStream();
    stream_1->SetDelegate(&fake_send_stream_delegate);
    stream_1->WriteMemSlices(slice_1.span(), /*fin=*/true);

    messages_2.emplace_back(2, quiche::QuicheStrCat("message for 2: ", i));
    test::QuicTestMemSliceVector slice_2(
        {std::make_pair(const_cast<char*>(messages_2.back().second.data()),
                        messages_2.back().second.size())});
    QuartcStream* stream_2 =
        send_channel_2->CreateOutgoingBidirectionalStream();
    stream_2->SetDelegate(&fake_send_stream_delegate);
    stream_2->WriteMemSlices(slice_2.span(), /*fin=*/true);
  }

  EXPECT_TRUE(simulator_.RunUntilOrTimeout(
      [this, &receive_delegate_1]() {
        return receive_delegate_1.messages_received().size() == 10 &&
               server_default_receiver_.messages_received().size() == 10;
      },
      QuicTime::Delta::FromSeconds(5)));

  EXPECT_EQ(receive_delegate_1.messages_received(), messages_1);
  EXPECT_EQ(server_default_receiver_.messages_received(), messages_2);
}

// Tests that datagram-lost callbacks are invoked on the right send channel
// delegate, and that they work with overlapping datagram ids.
TEST_F(QuartcMultiplexerTest, MultiplexLostDatagrams) {
  Connect();
  ASSERT_TRUE(simulator_.RunUntil([this]() {
    return client_session_delegate_.handshake_count() > 0 &&
           server_session_delegate_.handshake_count() > 0;
  }));

  // Just drop everything we try to send.
  client_filter_.set_packets_to_drop(30);

  FakeSendDelegate send_delegate_1;
  QuartcSendChannel* send_channel_1 =
      client_multiplexer()->CreateSendChannel(1, &send_delegate_1);
  FakeSendDelegate send_delegate_2;
  QuartcSendChannel* send_channel_2 =
      client_multiplexer()->CreateSendChannel(2, &send_delegate_2);

  FakeQuartcStreamDelegate fake_send_stream_delegate;

  FakeReceiveDelegate receive_delegate_1;
  server_multiplexer()->RegisterReceiveChannel(1, &receive_delegate_1);

  int num_messages = 10;
  std::vector<std::pair<uint64_t, std::string>> messages_1;
  messages_1.reserve(num_messages);
  std::vector<std::pair<uint64_t, std::string>> messages_2;
  messages_2.reserve(num_messages);
  std::vector<int64_t> messages_sent_1;
  std::vector<int64_t> messages_sent_2;
  for (int i = 0; i < num_messages; ++i) {
    messages_1.emplace_back(1, quiche::QuicheStrCat("message for 1: ", i));
    test::QuicTestMemSliceVector slice_1(
        {std::make_pair(const_cast<char*>(messages_1.back().second.data()),
                        messages_1.back().second.size())});
    send_channel_1->SendOrQueueMessage(slice_1.span(), i);
    messages_sent_1.push_back(i);

    messages_2.emplace_back(2, quiche::QuicheStrCat("message for 2: ", i));
    test::QuicTestMemSliceVector slice_2(
        {std::make_pair(const_cast<char*>(messages_2.back().second.data()),
                        messages_2.back().second.size())});
    // Use i + 5 as the datagram id for channel 2, so that some of the ids
    // overlap and some are disjoint.
    send_channel_2->SendOrQueueMessage(slice_2.span(), i + 5);
    messages_sent_2.push_back(i + 5);
  }

  // Now send something retransmittable to prompt loss detection.
  // If we never send anything retransmittable, we will never get acks, and
  // never detect losses.
  messages_1.emplace_back(
      1, quiche::QuicheStrCat("message for 1: ", num_messages));
  test::QuicTestMemSliceVector slice(
      {std::make_pair(const_cast<char*>(messages_1.back().second.data()),
                      messages_1.back().second.size())});
  QuartcStream* stream_1 = send_channel_1->CreateOutgoingBidirectionalStream();
  stream_1->SetDelegate(&fake_send_stream_delegate);
  stream_1->WriteMemSlices(slice.span(), /*fin=*/true);

  EXPECT_TRUE(simulator_.RunUntilOrTimeout(
      [&send_delegate_1, &send_delegate_2]() {
        return send_delegate_1.datagrams_lost().size() == 10 &&
               send_delegate_2.datagrams_lost().size() == 10;
      },
      QuicTime::Delta::FromSeconds(60)));

  EXPECT_EQ(send_delegate_1.datagrams_lost(), messages_sent_1);
  EXPECT_EQ(send_delegate_2.datagrams_lost(), messages_sent_2);

  EXPECT_THAT(send_delegate_1.datagrams_acked(), IsEmpty());
  EXPECT_THAT(send_delegate_2.datagrams_acked(), IsEmpty());

  EXPECT_THAT(receive_delegate_1.messages_received(), IsEmpty());
  EXPECT_THAT(server_default_receiver_.messages_received(), IsEmpty());
}

TEST_F(QuartcMultiplexerTest, UnregisterReceiveChannel) {
  Connect();

  FakeSendDelegate send_delegate;
  QuartcSendChannel* send_channel =
      client_multiplexer()->CreateSendChannel(1, &send_delegate);
  FakeQuartcStreamDelegate fake_send_stream_delegate;

  FakeReceiveDelegate receive_delegate;
  server_multiplexer()->RegisterReceiveChannel(1, &receive_delegate);
  server_multiplexer()->RegisterReceiveChannel(1, nullptr);

  int num_messages = 10;
  std::vector<std::pair<uint64_t, std::string>> messages;
  messages.reserve(num_messages);
  std::vector<int64_t> messages_sent;
  std::vector<testing::Matcher<std::pair<int64_t, QuicTime>>> ack_matchers;
  for (int i = 0; i < num_messages; ++i) {
    messages.emplace_back(1, quiche::QuicheStrCat("message for 1: ", i));
    test::QuicTestMemSliceVector slice(
        {std::make_pair(const_cast<char*>(messages.back().second.data()),
                        messages.back().second.size())});
    send_channel->SendOrQueueMessage(slice.span(), i);
    messages_sent.push_back(i);
    ack_matchers.push_back(Pair(i, Gt(QuicTime::Zero())));
  }

  EXPECT_TRUE(simulator_.RunUntil([&send_delegate]() {
    return send_delegate.datagrams_acked().size() == 10;
  }));

  EXPECT_EQ(send_delegate.datagrams_sent(), messages_sent);
  EXPECT_EQ(server_default_receiver_.messages_received(), messages);
  EXPECT_THAT(send_delegate.datagrams_acked(), ElementsAreArray(ack_matchers));
}

TEST_F(QuartcMultiplexerTest, CloseEvent) {
  Connect();
  Disconnect();

  EXPECT_THAT(client_session_delegate_.error(),
              test::IsError(QUIC_CONNECTION_CANCELLED));
  EXPECT_THAT(server_session_delegate_.error(),
              test::IsError(QUIC_CONNECTION_CANCELLED));
}

TEST_F(QuartcMultiplexerTest, CongestionEvent) {
  Connect();
  ASSERT_TRUE(simulator_.RunUntil([this]() {
    return client_session_delegate_.handshake_count() > 0 &&
           server_session_delegate_.handshake_count() > 0;
  }));

  EXPECT_GT(client_session_delegate_.latest_bandwidth_estimate(),
            QuicBandwidth::Zero());
  EXPECT_GT(client_session_delegate_.latest_pacing_rate(),
            QuicBandwidth::Zero());
  EXPECT_GT(client_session_delegate_.latest_rtt(), QuicTime::Delta::Zero());
}

}  // namespace
}  // namespace quic

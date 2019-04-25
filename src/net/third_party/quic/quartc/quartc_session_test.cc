// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/quartc/quartc_session.h"

#include "net/third_party/quic/core/quic_simple_buffer_allocator.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/core/tls_client_handshaker.h"
#include "net/third_party/quic/core/tls_server_handshaker.h"
#include "net/third_party/quic/platform/api/quic_clock.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quic/platform/api/quic_string_utils.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/api/quic_test_mem_slice_vector.h"
#include "net/third_party/quic/quartc/counting_packet_filter.h"
#include "net/third_party/quic/quartc/quartc_endpoint.h"
#include "net/third_party/quic/quartc/quartc_fakes.h"
#include "net/third_party/quic/quartc/quartc_packet_writer.h"
#include "net/third_party/quic/quartc/simulated_packet_transport.h"
#include "net/third_party/quic/test_tools/mock_clock.h"
#include "net/third_party/quic/test_tools/simulator/packet_filter.h"
#include "net/third_party/quic/test_tools/simulator/simulator.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace quic {

namespace {

constexpr QuicTime::Delta kPropagationDelay =
    QuicTime::Delta::FromMilliseconds(10);
// Propagation delay and a bit, but no more than full RTT.
constexpr QuicTime::Delta kPropagationDelayAndABit =
    QuicTime::Delta::FromMilliseconds(12);

static QuicByteCount kDefaultMaxPacketSize = 1200;

class QuartcSessionTest : public QuicTest {
 public:
  ~QuartcSessionTest() override {}

  void Init(bool create_client_endpoint = true) {
    client_transport_ =
        QuicMakeUnique<simulator::SimulatedQuartcPacketTransport>(
            &simulator_, "client_transport", "server_transport",
            10 * kDefaultMaxPacketSize);
    server_transport_ =
        QuicMakeUnique<simulator::SimulatedQuartcPacketTransport>(
            &simulator_, "server_transport", "client_transport",
            10 * kDefaultMaxPacketSize);

    client_filter_ = QuicMakeUnique<simulator::CountingPacketFilter>(
        &simulator_, "client_filter", client_transport_.get());

    client_server_link_ = QuicMakeUnique<simulator::SymmetricLink>(
        client_filter_.get(), server_transport_.get(),
        QuicBandwidth::FromKBitsPerSecond(10 * 1000), kPropagationDelay);

    client_stream_delegate_ = QuicMakeUnique<FakeQuartcStreamDelegate>();
    client_session_delegate_ = QuicMakeUnique<FakeQuartcSessionDelegate>(
        client_stream_delegate_.get(), simulator_.GetClock());
    client_endpoint_delegate_ = QuicMakeUnique<FakeQuartcEndpointDelegate>(
        client_session_delegate_.get());

    server_stream_delegate_ = QuicMakeUnique<FakeQuartcStreamDelegate>();
    server_session_delegate_ = QuicMakeUnique<FakeQuartcSessionDelegate>(
        server_stream_delegate_.get(), simulator_.GetClock());
    server_endpoint_delegate_ = QuicMakeUnique<FakeQuartcEndpointDelegate>(
        server_session_delegate_.get());

    // No 0-rtt setup, because server config is empty.
    // CannotCreateDataStreamBeforeHandshake depends on 1-rtt setup.
    if (create_client_endpoint) {
      client_endpoint_ = QuicMakeUnique<QuartcClientEndpoint>(
          simulator_.GetAlarmFactory(), simulator_.GetClock(),
          client_endpoint_delegate_.get(), quic::QuartcSessionConfig(),
          /*serialized_server_config=*/"");
    }
    server_endpoint_ = QuicMakeUnique<QuartcServerEndpoint>(
        simulator_.GetAlarmFactory(), simulator_.GetClock(),
        server_endpoint_delegate_.get(), quic::QuartcSessionConfig());
  }

  // Note that input session config will apply to both server and client.
  // Perspective and packet_transport will be overwritten.
  void CreateClientAndServerSessions(const QuartcSessionConfig& session_config,
                                     bool init = true) {
    if (init) {
      Init();
    }

    server_endpoint_->Connect(server_transport_.get());
    client_endpoint_->Connect(client_transport_.get());

    CHECK(simulator_.RunUntil([this] {
      return client_endpoint_delegate_->session() != nullptr &&
             server_endpoint_delegate_->session() != nullptr;
    }));

    client_peer_ = client_endpoint_delegate_->session();
    server_peer_ = server_endpoint_delegate_->session();
  }

  // Runs all tasks scheduled in the next 200 ms.
  void RunTasks() { simulator_.RunFor(QuicTime::Delta::FromMilliseconds(200)); }

  void AwaitHandshake() {
    simulator_.RunUntil([this] {
      return client_peer_->IsCryptoHandshakeConfirmed() &&
             server_peer_->IsCryptoHandshakeConfirmed();
    });
  }

  // Test handshake establishment and sending/receiving of data for two
  // directions.
  void TestSendReceiveStreams() {
    ASSERT_TRUE(server_peer_->IsCryptoHandshakeConfirmed());
    ASSERT_TRUE(client_peer_->IsCryptoHandshakeConfirmed());
    ASSERT_TRUE(server_peer_->IsEncryptionEstablished());
    ASSERT_TRUE(client_peer_->IsEncryptionEstablished());

    // Now we can establish encrypted outgoing stream.
    QuartcStream* outgoing_stream =
        server_peer_->CreateOutgoingBidirectionalStream();
    QuicStreamId stream_id = outgoing_stream->id();
    ASSERT_NE(nullptr, outgoing_stream);
    EXPECT_TRUE(server_peer_->ShouldKeepConnectionAlive());

    outgoing_stream->SetDelegate(server_stream_delegate_.get());

    // Send a test message from peer 1 to peer 2.
    char kTestMessage[] = "Hello";
    test::QuicTestMemSliceVector data(
        {std::make_pair(kTestMessage, strlen(kTestMessage))});
    outgoing_stream->WriteMemSlices(data.span(), /*fin=*/false);
    RunTasks();

    // Wait for peer 2 to receive messages.
    ASSERT_TRUE(client_stream_delegate_->has_data());

    QuartcStream* incoming = client_session_delegate_->last_incoming_stream();
    ASSERT_TRUE(incoming);
    EXPECT_EQ(incoming->id(), stream_id);
    EXPECT_TRUE(client_peer_->ShouldKeepConnectionAlive());

    EXPECT_EQ(client_stream_delegate_->data()[stream_id], kTestMessage);
    // Send a test message from peer 2 to peer 1.
    char kTestResponse[] = "Response";
    test::QuicTestMemSliceVector response(
        {std::make_pair(kTestResponse, strlen(kTestResponse))});
    incoming->WriteMemSlices(response.span(), /*fin=*/false);
    RunTasks();
    // Wait for peer 1 to receive messages.
    ASSERT_TRUE(server_stream_delegate_->has_data());

    EXPECT_EQ(server_stream_delegate_->data()[stream_id], kTestResponse);
  }

  // Test sending/receiving of messages for two directions.
  void TestSendReceiveMessage() {
    ASSERT_TRUE(server_peer_->CanSendMessage());
    ASSERT_TRUE(client_peer_->CanSendMessage());

    // Send message from peer 1 to peer 2.
    ASSERT_TRUE(server_peer_->SendOrQueueMessage("Message from server"));

    // First message in each direction should not be queued.
    EXPECT_EQ(server_peer_->send_message_queue_size(), 0u);

    // Wait for peer 2 to receive message.
    RunTasks();

    EXPECT_THAT(client_session_delegate_->incoming_messages(),
                testing::ElementsAre("Message from server"));

    // Send message from peer 2 to peer 1.
    ASSERT_TRUE(client_peer_->SendOrQueueMessage("Message from client"));

    // First message in each direction should not be queued.
    EXPECT_EQ(client_peer_->send_message_queue_size(), 0u);

    // Wait for peer 1 to receive message.
    RunTasks();

    EXPECT_THAT(server_session_delegate_->incoming_messages(),
                testing::ElementsAre("Message from client"));
  }

  // Test for sending multiple messages that also result in queueing.
  // This is one-way test, which is run in given direction.
  void TestSendReceiveQueuedMessages(bool direction_from_server) {
    // Send until queue_size number of messages are queued.
    constexpr size_t queue_size = 10;

    ASSERT_TRUE(server_peer_->CanSendMessage());
    ASSERT_TRUE(client_peer_->CanSendMessage());

    QuartcSession* const peer_sending =
        direction_from_server ? server_peer_ : client_peer_;

    FakeQuartcSessionDelegate* const delegate_receiving =
        direction_from_server ? client_session_delegate_.get()
                              : server_session_delegate_.get();

    // There should be no messages in the queue before we start sending.
    EXPECT_EQ(peer_sending->send_message_queue_size(), 0u);

    // Send messages from peer 1 to peer 2 until required number of messages
    // are queued in unsent message queue.
    std::vector<QuicString> sent_messages;
    while (peer_sending->send_message_queue_size() < queue_size) {
      sent_messages.push_back(
          QuicStrCat("Sending message, index=", sent_messages.size()));
      ASSERT_TRUE(peer_sending->SendOrQueueMessage(sent_messages.back()));
    }

    // Wait for peer 2 to receive all messages.
    RunTasks();

    EXPECT_EQ(delegate_receiving->incoming_messages(), sent_messages);
  }

  // Test sending long messages:
  // - message of maximum allowed length should succeed
  // - message of > maximum allowed length should fail.
  void TestSendLongMessage() {
    ASSERT_TRUE(server_peer_->CanSendMessage());
    ASSERT_TRUE(client_peer_->CanSendMessage());

    // Send message of maximum allowed length.
    QuicString message_max_long =
        QuicString(server_peer_->GetLargestMessagePayload(), 'A');
    ASSERT_TRUE(server_peer_->SendOrQueueMessage(message_max_long));

    // Send long message which should fail.
    QuicString message_too_long =
        QuicString(server_peer_->GetLargestMessagePayload() + 1, 'B');
    ASSERT_FALSE(server_peer_->SendOrQueueMessage(message_too_long));

    // Wait for peer 2 to receive message.
    RunTasks();

    // Client should only receive one message of allowed length.
    EXPECT_THAT(client_session_delegate_->incoming_messages(),
                testing::ElementsAre(message_max_long));
  }

  // Test that client and server are not connected after handshake failure.
  void TestDisconnectAfterFailedHandshake() {
    EXPECT_TRUE(!client_session_delegate_->connected());
    EXPECT_TRUE(!server_session_delegate_->connected());

    EXPECT_FALSE(client_peer_->IsEncryptionEstablished());
    EXPECT_FALSE(client_peer_->IsCryptoHandshakeConfirmed());

    EXPECT_FALSE(server_peer_->IsEncryptionEstablished());
    EXPECT_FALSE(server_peer_->IsCryptoHandshakeConfirmed());
  }

 protected:
  simulator::Simulator simulator_;

  std::unique_ptr<simulator::SimulatedQuartcPacketTransport> client_transport_;
  std::unique_ptr<simulator::SimulatedQuartcPacketTransport> server_transport_;
  std::unique_ptr<simulator::CountingPacketFilter> client_filter_;
  std::unique_ptr<simulator::SymmetricLink> client_server_link_;

  std::unique_ptr<FakeQuartcStreamDelegate> client_stream_delegate_;
  std::unique_ptr<FakeQuartcSessionDelegate> client_session_delegate_;
  std::unique_ptr<FakeQuartcEndpointDelegate> client_endpoint_delegate_;
  std::unique_ptr<FakeQuartcStreamDelegate> server_stream_delegate_;
  std::unique_ptr<FakeQuartcSessionDelegate> server_session_delegate_;
  std::unique_ptr<FakeQuartcEndpointDelegate> server_endpoint_delegate_;

  std::unique_ptr<QuartcClientEndpoint> client_endpoint_;
  std::unique_ptr<QuartcServerEndpoint> server_endpoint_;

  QuartcSession* client_peer_ = nullptr;
  QuartcSession* server_peer_ = nullptr;
};

TEST_F(QuartcSessionTest, SendReceiveStreams) {
  CreateClientAndServerSessions(QuartcSessionConfig());
  AwaitHandshake();
  TestSendReceiveStreams();
}

TEST_F(QuartcSessionTest, SendReceiveMessages) {
  CreateClientAndServerSessions(QuartcSessionConfig());
  AwaitHandshake();
  TestSendReceiveMessage();
}

TEST_F(QuartcSessionTest, SendReceiveQueuedMessages) {
  CreateClientAndServerSessions(QuartcSessionConfig());
  AwaitHandshake();
  TestSendReceiveQueuedMessages(/*direction_from_server=*/true);
  TestSendReceiveQueuedMessages(/*direction_from_server=*/false);
}

TEST_F(QuartcSessionTest, SendMessageFails) {
  CreateClientAndServerSessions(QuartcSessionConfig());
  AwaitHandshake();
  TestSendLongMessage();
}

TEST_F(QuartcSessionTest, TestCryptoHandshakeCanWriteTriggers) {
  CreateClientAndServerSessions(QuartcSessionConfig());

  AwaitHandshake();

  RunTasks();

  ASSERT_TRUE(client_session_delegate_->writable_time().IsInitialized());
  ASSERT_TRUE(
      client_session_delegate_->crypto_handshake_time().IsInitialized());
  // On client, we are writable 1-rtt before crypto handshake is complete.
  ASSERT_LT(client_session_delegate_->writable_time(),
            client_session_delegate_->crypto_handshake_time());

  ASSERT_TRUE(server_session_delegate_->writable_time().IsInitialized());
  ASSERT_TRUE(
      server_session_delegate_->crypto_handshake_time().IsInitialized());
  // On server, the writable time and crypto handshake are the same. (when SHLO
  // is sent).
  ASSERT_EQ(server_session_delegate_->writable_time(),
            server_session_delegate_->crypto_handshake_time());
}

TEST_F(QuartcSessionTest, PreSharedKeyHandshake) {
  QuartcSessionConfig config;
  config.pre_shared_key = "foo";
  CreateClientAndServerSessions(config);
  AwaitHandshake();
  TestSendReceiveStreams();
  TestSendReceiveMessage();
}

// Test that data streams are not created before handshake.
TEST_F(QuartcSessionTest, CannotCreateDataStreamBeforeHandshake) {
  CreateClientAndServerSessions(QuartcSessionConfig());
  EXPECT_EQ(nullptr, server_peer_->CreateOutgoingBidirectionalStream());
  EXPECT_EQ(nullptr, client_peer_->CreateOutgoingBidirectionalStream());
}

TEST_F(QuartcSessionTest, CancelQuartcStream) {
  CreateClientAndServerSessions(QuartcSessionConfig());
  AwaitHandshake();
  ASSERT_TRUE(client_peer_->IsCryptoHandshakeConfirmed());
  ASSERT_TRUE(server_peer_->IsCryptoHandshakeConfirmed());

  QuartcStream* stream = client_peer_->CreateOutgoingBidirectionalStream();
  ASSERT_NE(nullptr, stream);

  uint32_t id = stream->id();
  EXPECT_FALSE(client_peer_->IsClosedStream(id));
  stream->SetDelegate(client_stream_delegate_.get());
  client_peer_->CancelStream(id);
  EXPECT_EQ(stream->stream_error(),
            QuicRstStreamErrorCode::QUIC_STREAM_CANCELLED);
  EXPECT_TRUE(client_peer_->IsClosedStream(id));
}

// TODO(b/112561077):  This is the wrong layer for this test.  We should write a
// test specifically for QuartcPacketWriter with a stubbed-out
// QuartcPacketTransport and remove
// SimulatedQuartcPacketTransport::last_packet_number().
TEST_F(QuartcSessionTest, WriterGivesPacketNumberToTransport) {
  CreateClientAndServerSessions(QuartcSessionConfig());
  AwaitHandshake();
  ASSERT_TRUE(client_peer_->IsCryptoHandshakeConfirmed());
  ASSERT_TRUE(server_peer_->IsCryptoHandshakeConfirmed());

  QuartcStream* stream = client_peer_->CreateOutgoingBidirectionalStream();
  stream->SetDelegate(client_stream_delegate_.get());

  char kClientMessage[] = "Hello";
  test::QuicTestMemSliceVector stream_data(
      {std::make_pair(kClientMessage, strlen(kClientMessage))});
  stream->WriteMemSlices(stream_data.span(), /*fin=*/false);
  RunTasks();

  // The transport should see the latest packet number sent by QUIC.
  EXPECT_EQ(
      client_transport_->last_packet_number(),
      client_peer_->connection()->sent_packet_manager().GetLargestSentPacket());
}

TEST_F(QuartcSessionTest, CloseConnection) {
  CreateClientAndServerSessions(QuartcSessionConfig());
  AwaitHandshake();
  ASSERT_TRUE(client_peer_->IsCryptoHandshakeConfirmed());
  ASSERT_TRUE(server_peer_->IsCryptoHandshakeConfirmed());

  client_peer_->CloseConnection("Connection closed by client");
  EXPECT_FALSE(client_session_delegate_->connected());
  RunTasks();
  EXPECT_FALSE(server_session_delegate_->connected());
}

TEST_F(QuartcSessionTest, StreamRetransmissionEnabled) {
  CreateClientAndServerSessions(QuartcSessionConfig());
  AwaitHandshake();
  ASSERT_TRUE(client_peer_->IsCryptoHandshakeConfirmed());
  ASSERT_TRUE(server_peer_->IsCryptoHandshakeConfirmed());

  QuartcStream* stream = client_peer_->CreateOutgoingBidirectionalStream();
  QuicStreamId stream_id = stream->id();
  stream->SetDelegate(client_stream_delegate_.get());
  stream->set_cancel_on_loss(false);

  client_filter_->set_packets_to_drop(1);

  char kClientMessage[] = "Hello";
  test::QuicTestMemSliceVector stream_data(
      {std::make_pair(kClientMessage, strlen(kClientMessage))});
  stream->WriteMemSlices(stream_data.span(), /*fin=*/false);
  RunTasks();

  // Stream data should make it despite packet loss.
  ASSERT_TRUE(server_stream_delegate_->has_data());
  EXPECT_EQ(server_stream_delegate_->data()[stream_id], kClientMessage);
}

TEST_F(QuartcSessionTest, StreamRetransmissionDisabled) {
  // Disable tail loss probe, otherwise test maybe flaky because dropped
  // message will be retransmitted to detect tail loss.
  QuartcSessionConfig session_config;
  session_config.enable_tail_loss_probe = false;
  CreateClientAndServerSessions(session_config);

  // Disable probing retransmissions, otherwise test maybe flaky because dropped
  // message will be retransmitted to to probe for more bandwidth.
  client_peer_->connection()->set_fill_up_link_during_probing(false);

  AwaitHandshake();
  ASSERT_TRUE(client_peer_->IsCryptoHandshakeConfirmed());
  ASSERT_TRUE(server_peer_->IsCryptoHandshakeConfirmed());

  // The client sends an ACK for the crypto handshake next.  This must be
  // flushed before we set the filter to drop the next packet, in order to
  // ensure that the filter drops a data-bearing packet instead of just an ack.
  RunTasks();

  QuartcStream* stream = client_peer_->CreateOutgoingBidirectionalStream();
  QuicStreamId stream_id = stream->id();
  stream->SetDelegate(client_stream_delegate_.get());
  stream->set_cancel_on_loss(true);

  client_filter_->set_packets_to_drop(1);

  char kMessage[] = "Hello";
  test::QuicTestMemSliceVector stream_data(
      {std::make_pair(kMessage, strlen(kMessage))});
  stream->WriteMemSlices(stream_data.span(), /*fin=*/false);
  simulator_.RunFor(QuicTime::Delta::FromMilliseconds(1));

  // Send another packet to trigger loss detection.
  QuartcStream* stream_1 = client_peer_->CreateOutgoingBidirectionalStream();
  stream_1->SetDelegate(client_stream_delegate_.get());

  char kMessage1[] = "Second message";
  test::QuicTestMemSliceVector stream_data_1(
      {std::make_pair(kMessage1, strlen(kMessage1))});
  stream_1->WriteMemSlices(stream_data_1.span(), /*fin=*/false);
  RunTasks();

  // QUIC should try to retransmit the first stream by loss detection.  Instead,
  // it will cancel itself.
  EXPECT_THAT(server_stream_delegate_->data()[stream_id], testing::IsEmpty());

  EXPECT_TRUE(client_peer_->IsClosedStream(stream_id));
  EXPECT_TRUE(server_peer_->IsClosedStream(stream_id));
  EXPECT_EQ(client_stream_delegate_->stream_error(stream_id),
            QUIC_STREAM_CANCELLED);
  EXPECT_EQ(server_stream_delegate_->stream_error(stream_id),
            QUIC_STREAM_CANCELLED);
}

TEST_F(QuartcSessionTest, ServerRegistersAsWriteBlocked) {
  // Initialize client and server session, but with the server write-blocked.
  Init();
  server_transport_->SetWritable(false);
  CreateClientAndServerSessions(QuartcSessionConfig(), /*init=*/false);

  // Let the client send a few copies of the CHLO.  The server can't respond, as
  // it's still write-blocked.
  RunTasks();

  // Making the server's transport writable should trigger a callback that
  // reaches the server session, allowing it to write packets.
  server_transport_->SetWritable(true);

  // Now the server should respond with the SHLO, encryption should be
  // established, and data should flow normally.
  // Note that if the server is *not* correctly registered as write-blocked,
  // it will crash here (see b/124527328 for details).
  AwaitHandshake();
  TestSendReceiveStreams();
}

TEST_F(QuartcSessionTest, PreSharedKeyHandshakeIs0RTT) {
  QuartcSessionConfig session_config;
  session_config.pre_shared_key = "foo";

  // Client endpoint is created below. Destructing client endpoint
  // causes issues with the simulator.
  Init(/*create_client_endpoint=*/false);

  server_endpoint_->Connect(server_transport_.get());

  client_endpoint_ = QuicMakeUnique<QuartcClientEndpoint>(
      simulator_.GetAlarmFactory(), simulator_.GetClock(),
      client_endpoint_delegate_.get(), QuartcSessionConfig(),
      // This is the key line here. It passes through the server config
      // from the server to the client.
      server_endpoint_->server_crypto_config());

  client_endpoint_->Connect(client_transport_.get());

  // Running for 1ms. This is shorter than the RTT, so the
  // client session should be created, but server won't be created yet.
  simulator_.RunFor(QuicTime::Delta::FromMilliseconds(1));

  client_peer_ = client_endpoint_delegate_->session();
  server_peer_ = server_endpoint_delegate_->session();

  ASSERT_NE(client_peer_, nullptr);
  ASSERT_EQ(server_peer_, nullptr);

  // Write data to the client before running tasks.  This should be sent by the
  // client and received by the server if the handshake is 0RTT.
  // If this test fails, add 'RunTasks()' above, and see what error is sent
  // by the server in the rejection message.
  QuartcStream* stream = client_peer_->CreateOutgoingBidirectionalStream();
  ASSERT_NE(stream, nullptr);
  QuicStreamId stream_id = stream->id();
  stream->SetDelegate(client_stream_delegate_.get());

  char message[] = "Hello in 0RTTs!";
  test::QuicTestMemSliceVector data({std::make_pair(message, strlen(message))});
  stream->WriteMemSlices(data.span(), /*fin=*/false);

  // This will now run the rest of the connection. But the
  // Server peer will receive the CHLO and message after 1 delay.
  simulator_.RunFor(kPropagationDelayAndABit);

  // If we can decrypt the data, it means that 0 rtt was successful.
  // This is because we waited only a propagation delay. So if the decryption
  // failed, we would send sREJ instead of SHLO, but it wouldn't be delivered to
  // the client yet.
  ASSERT_TRUE(server_stream_delegate_->has_data());
  EXPECT_EQ(server_stream_delegate_->data()[stream_id], message);
}

}  // namespace

}  // namespace quic

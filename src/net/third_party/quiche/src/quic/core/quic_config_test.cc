// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_config.h"

#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake_message.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_uint128.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {
namespace {

class QuicConfigTest : public QuicTestWithParam<ParsedQuicVersion> {
 public:
  QuicConfigTest() : version_(GetParam()) {}

 protected:
  ParsedQuicVersion version_;
  QuicConfig config_;
};

// Run all tests with all versions of QUIC.
INSTANTIATE_TEST_SUITE_P(QuicConfigTests,
                         QuicConfigTest,
                         ::testing::ValuesIn(AllSupportedVersions()),
                         ::testing::PrintToStringParamName());

TEST_P(QuicConfigTest, SetDefaults) {
  EXPECT_EQ(kMinimumFlowControlSendWindow,
            config_.GetInitialStreamFlowControlWindowToSend());
  EXPECT_EQ(kMinimumFlowControlSendWindow,
            config_.GetInitialMaxStreamDataBytesIncomingBidirectionalToSend());
  EXPECT_EQ(kMinimumFlowControlSendWindow,
            config_.GetInitialMaxStreamDataBytesOutgoingBidirectionalToSend());
  EXPECT_EQ(kMinimumFlowControlSendWindow,
            config_.GetInitialMaxStreamDataBytesUnidirectionalToSend());
  EXPECT_FALSE(config_.HasReceivedInitialStreamFlowControlWindowBytes());
  EXPECT_FALSE(
      config_.HasReceivedInitialMaxStreamDataBytesIncomingBidirectional());
  EXPECT_FALSE(
      config_.HasReceivedInitialMaxStreamDataBytesOutgoingBidirectional());
  EXPECT_FALSE(config_.HasReceivedInitialMaxStreamDataBytesUnidirectional());
  EXPECT_EQ(kMaxIncomingPacketSize, config_.GetMaxPacketSizeToSend());
  EXPECT_FALSE(config_.HasReceivedMaxPacketSize());
}

TEST_P(QuicConfigTest, AutoSetIetfFlowControl) {
  EXPECT_EQ(kMinimumFlowControlSendWindow,
            config_.GetInitialStreamFlowControlWindowToSend());
  EXPECT_EQ(kMinimumFlowControlSendWindow,
            config_.GetInitialMaxStreamDataBytesIncomingBidirectionalToSend());
  EXPECT_EQ(kMinimumFlowControlSendWindow,
            config_.GetInitialMaxStreamDataBytesOutgoingBidirectionalToSend());
  EXPECT_EQ(kMinimumFlowControlSendWindow,
            config_.GetInitialMaxStreamDataBytesUnidirectionalToSend());
  static const uint32_t kTestWindowSize = 1234567;
  config_.SetInitialStreamFlowControlWindowToSend(kTestWindowSize);
  EXPECT_EQ(kTestWindowSize, config_.GetInitialStreamFlowControlWindowToSend());
  EXPECT_EQ(kTestWindowSize,
            config_.GetInitialMaxStreamDataBytesIncomingBidirectionalToSend());
  EXPECT_EQ(kTestWindowSize,
            config_.GetInitialMaxStreamDataBytesOutgoingBidirectionalToSend());
  EXPECT_EQ(kTestWindowSize,
            config_.GetInitialMaxStreamDataBytesUnidirectionalToSend());
  static const uint32_t kTestWindowSizeTwo = 2345678;
  config_.SetInitialMaxStreamDataBytesIncomingBidirectionalToSend(
      kTestWindowSizeTwo);
  EXPECT_EQ(kTestWindowSize, config_.GetInitialStreamFlowControlWindowToSend());
  EXPECT_EQ(kTestWindowSizeTwo,
            config_.GetInitialMaxStreamDataBytesIncomingBidirectionalToSend());
  EXPECT_EQ(kTestWindowSize,
            config_.GetInitialMaxStreamDataBytesOutgoingBidirectionalToSend());
  EXPECT_EQ(kTestWindowSize,
            config_.GetInitialMaxStreamDataBytesUnidirectionalToSend());
}

TEST_P(QuicConfigTest, ToHandshakeMessage) {
  if (version_.UsesTls()) {
    // CryptoHandshakeMessage is only used for QUIC_CRYPTO.
    return;
  }
  config_.SetInitialStreamFlowControlWindowToSend(
      kInitialStreamFlowControlWindowForTest);
  config_.SetInitialSessionFlowControlWindowToSend(
      kInitialSessionFlowControlWindowForTest);
  config_.SetIdleNetworkTimeout(QuicTime::Delta::FromSeconds(5));
  CryptoHandshakeMessage msg;
  config_.ToHandshakeMessage(&msg, version_.transport_version);

  uint32_t value;
  QuicErrorCode error = msg.GetUint32(kICSL, &value);
  EXPECT_THAT(error, IsQuicNoError());
  EXPECT_EQ(5u, value);

  error = msg.GetUint32(kSFCW, &value);
  EXPECT_THAT(error, IsQuicNoError());
  EXPECT_EQ(kInitialStreamFlowControlWindowForTest, value);

  error = msg.GetUint32(kCFCW, &value);
  EXPECT_THAT(error, IsQuicNoError());
  EXPECT_EQ(kInitialSessionFlowControlWindowForTest, value);
}

TEST_P(QuicConfigTest, ProcessClientHello) {
  if (version_.UsesTls()) {
    // CryptoHandshakeMessage is only used for QUIC_CRYPTO.
    return;
  }
  const uint32_t kTestMaxAckDelayMs =
      static_cast<uint32_t>(kDefaultDelayedAckTimeMs + 1);
  QuicConfig client_config;
  QuicTagVector cgst;
  cgst.push_back(kQBIC);
  client_config.SetIdleNetworkTimeout(
      QuicTime::Delta::FromSeconds(2 * kMaximumIdleTimeoutSecs));
  client_config.SetInitialRoundTripTimeUsToSend(10 * kNumMicrosPerMilli);
  client_config.SetInitialStreamFlowControlWindowToSend(
      2 * kInitialStreamFlowControlWindowForTest);
  client_config.SetInitialSessionFlowControlWindowToSend(
      2 * kInitialSessionFlowControlWindowForTest);
  QuicTagVector copt;
  copt.push_back(kTBBR);
  client_config.SetConnectionOptionsToSend(copt);
  client_config.SetMaxAckDelayToSendMs(kTestMaxAckDelayMs);
  CryptoHandshakeMessage msg;
  client_config.ToHandshakeMessage(&msg, version_.transport_version);

  std::string error_details;
  QuicTagVector initial_received_options;
  initial_received_options.push_back(kIW50);
  EXPECT_TRUE(
      config_.SetInitialReceivedConnectionOptions(initial_received_options));
  EXPECT_FALSE(
      config_.SetInitialReceivedConnectionOptions(initial_received_options))
      << "You can only set initial options once.";
  const QuicErrorCode error =
      config_.ProcessPeerHello(msg, CLIENT, &error_details);
  EXPECT_FALSE(
      config_.SetInitialReceivedConnectionOptions(initial_received_options))
      << "You cannot set initial options after the hello.";
  EXPECT_THAT(error, IsQuicNoError());
  EXPECT_TRUE(config_.negotiated());
  EXPECT_EQ(QuicTime::Delta::FromSeconds(kMaximumIdleTimeoutSecs),
            config_.IdleNetworkTimeout());
  EXPECT_EQ(10 * kNumMicrosPerMilli, config_.ReceivedInitialRoundTripTimeUs());
  EXPECT_TRUE(config_.HasReceivedConnectionOptions());
  EXPECT_EQ(2u, config_.ReceivedConnectionOptions().size());
  EXPECT_EQ(config_.ReceivedConnectionOptions()[0], kIW50);
  EXPECT_EQ(config_.ReceivedConnectionOptions()[1], kTBBR);
  EXPECT_EQ(config_.ReceivedInitialStreamFlowControlWindowBytes(),
            2 * kInitialStreamFlowControlWindowForTest);
  EXPECT_EQ(config_.ReceivedInitialSessionFlowControlWindowBytes(),
            2 * kInitialSessionFlowControlWindowForTest);
  EXPECT_TRUE(config_.HasReceivedMaxAckDelayMs());
  EXPECT_EQ(kTestMaxAckDelayMs, config_.ReceivedMaxAckDelayMs());

  // IETF QUIC stream limits should not be received in QUIC crypto messages.
  EXPECT_FALSE(
      config_.HasReceivedInitialMaxStreamDataBytesIncomingBidirectional());
  EXPECT_FALSE(
      config_.HasReceivedInitialMaxStreamDataBytesOutgoingBidirectional());
  EXPECT_FALSE(config_.HasReceivedInitialMaxStreamDataBytesUnidirectional());
}

TEST_P(QuicConfigTest, ProcessServerHello) {
  if (version_.UsesTls()) {
    // CryptoHandshakeMessage is only used for QUIC_CRYPTO.
    return;
  }
  QuicIpAddress host;
  host.FromString("127.0.3.1");
  const QuicSocketAddress kTestServerAddress = QuicSocketAddress(host, 1234);
  const QuicUint128 kTestResetToken = MakeQuicUint128(0, 10111100001);
  const uint32_t kTestMaxAckDelayMs =
      static_cast<uint32_t>(kDefaultDelayedAckTimeMs + 1);
  QuicConfig server_config;
  QuicTagVector cgst;
  cgst.push_back(kQBIC);
  server_config.SetIdleNetworkTimeout(
      QuicTime::Delta::FromSeconds(kMaximumIdleTimeoutSecs / 2));
  server_config.SetInitialRoundTripTimeUsToSend(10 * kNumMicrosPerMilli);
  server_config.SetInitialStreamFlowControlWindowToSend(
      2 * kInitialStreamFlowControlWindowForTest);
  server_config.SetInitialSessionFlowControlWindowToSend(
      2 * kInitialSessionFlowControlWindowForTest);
  server_config.SetIPv4AlternateServerAddressToSend(kTestServerAddress);
  server_config.SetStatelessResetTokenToSend(kTestResetToken);
  server_config.SetMaxAckDelayToSendMs(kTestMaxAckDelayMs);
  CryptoHandshakeMessage msg;
  server_config.ToHandshakeMessage(&msg, version_.transport_version);
  std::string error_details;
  const QuicErrorCode error =
      config_.ProcessPeerHello(msg, SERVER, &error_details);
  EXPECT_THAT(error, IsQuicNoError());
  EXPECT_TRUE(config_.negotiated());
  EXPECT_EQ(QuicTime::Delta::FromSeconds(kMaximumIdleTimeoutSecs / 2),
            config_.IdleNetworkTimeout());
  EXPECT_EQ(10 * kNumMicrosPerMilli, config_.ReceivedInitialRoundTripTimeUs());
  EXPECT_EQ(config_.ReceivedInitialStreamFlowControlWindowBytes(),
            2 * kInitialStreamFlowControlWindowForTest);
  EXPECT_EQ(config_.ReceivedInitialSessionFlowControlWindowBytes(),
            2 * kInitialSessionFlowControlWindowForTest);
  EXPECT_TRUE(config_.HasReceivedIPv4AlternateServerAddress());
  EXPECT_EQ(kTestServerAddress, config_.ReceivedIPv4AlternateServerAddress());
  EXPECT_FALSE(config_.HasReceivedIPv6AlternateServerAddress());
  EXPECT_TRUE(config_.HasReceivedStatelessResetToken());
  EXPECT_EQ(kTestResetToken, config_.ReceivedStatelessResetToken());
  EXPECT_TRUE(config_.HasReceivedMaxAckDelayMs());
  EXPECT_EQ(kTestMaxAckDelayMs, config_.ReceivedMaxAckDelayMs());

  // IETF QUIC stream limits should not be received in QUIC crypto messages.
  EXPECT_FALSE(
      config_.HasReceivedInitialMaxStreamDataBytesIncomingBidirectional());
  EXPECT_FALSE(
      config_.HasReceivedInitialMaxStreamDataBytesOutgoingBidirectional());
  EXPECT_FALSE(config_.HasReceivedInitialMaxStreamDataBytesUnidirectional());
}

TEST_P(QuicConfigTest, MissingOptionalValuesInCHLO) {
  if (version_.UsesTls()) {
    // CryptoHandshakeMessage is only used for QUIC_CRYPTO.
    return;
  }
  CryptoHandshakeMessage msg;
  msg.SetValue(kICSL, 1);

  // Set all REQUIRED tags.
  msg.SetValue(kICSL, 1);
  msg.SetValue(kMIBS, 1);

  // No error, as rest are optional.
  std::string error_details;
  const QuicErrorCode error =
      config_.ProcessPeerHello(msg, CLIENT, &error_details);
  EXPECT_THAT(error, IsQuicNoError());
  EXPECT_TRUE(config_.negotiated());
}

TEST_P(QuicConfigTest, MissingOptionalValuesInSHLO) {
  if (version_.UsesTls()) {
    // CryptoHandshakeMessage is only used for QUIC_CRYPTO.
    return;
  }
  CryptoHandshakeMessage msg;

  // Set all REQUIRED tags.
  msg.SetValue(kICSL, 1);
  msg.SetValue(kMIBS, 1);

  // No error, as rest are optional.
  std::string error_details;
  const QuicErrorCode error =
      config_.ProcessPeerHello(msg, SERVER, &error_details);
  EXPECT_THAT(error, IsQuicNoError());
  EXPECT_TRUE(config_.negotiated());
}

TEST_P(QuicConfigTest, MissingValueInCHLO) {
  if (version_.UsesTls()) {
    // CryptoHandshakeMessage is only used for QUIC_CRYPTO.
    return;
  }
  // Server receives CHLO with missing kICSL.
  CryptoHandshakeMessage msg;
  std::string error_details;
  const QuicErrorCode error =
      config_.ProcessPeerHello(msg, CLIENT, &error_details);
  EXPECT_THAT(error, IsError(QUIC_CRYPTO_MESSAGE_PARAMETER_NOT_FOUND));
}

TEST_P(QuicConfigTest, MissingValueInSHLO) {
  if (version_.UsesTls()) {
    // CryptoHandshakeMessage is only used for QUIC_CRYPTO.
    return;
  }
  // Client receives SHLO with missing kICSL.
  CryptoHandshakeMessage msg;
  std::string error_details;
  const QuicErrorCode error =
      config_.ProcessPeerHello(msg, SERVER, &error_details);
  EXPECT_THAT(error, IsError(QUIC_CRYPTO_MESSAGE_PARAMETER_NOT_FOUND));
}

TEST_P(QuicConfigTest, OutOfBoundSHLO) {
  if (version_.UsesTls()) {
    // CryptoHandshakeMessage is only used for QUIC_CRYPTO.
    return;
  }
  QuicConfig server_config;
  server_config.SetIdleNetworkTimeout(
      QuicTime::Delta::FromSeconds(2 * kMaximumIdleTimeoutSecs));

  CryptoHandshakeMessage msg;
  server_config.ToHandshakeMessage(&msg, version_.transport_version);
  std::string error_details;
  const QuicErrorCode error =
      config_.ProcessPeerHello(msg, SERVER, &error_details);
  EXPECT_THAT(error, IsError(QUIC_INVALID_NEGOTIATED_VALUE));
}

TEST_P(QuicConfigTest, InvalidFlowControlWindow) {
  // QuicConfig should not accept an invalid flow control window to send to the
  // peer: the receive window must be at least the default of 16 Kb.
  QuicConfig config;
  const uint64_t kInvalidWindow = kMinimumFlowControlSendWindow - 1;
  EXPECT_QUIC_BUG(
      config.SetInitialStreamFlowControlWindowToSend(kInvalidWindow),
      "Initial stream flow control receive window");

  EXPECT_EQ(kMinimumFlowControlSendWindow,
            config.GetInitialStreamFlowControlWindowToSend());
}

TEST_P(QuicConfigTest, HasClientSentConnectionOption) {
  if (version_.UsesTls()) {
    // CryptoHandshakeMessage is only used for QUIC_CRYPTO.
    return;
  }
  QuicConfig client_config;
  QuicTagVector copt;
  copt.push_back(kTBBR);
  client_config.SetConnectionOptionsToSend(copt);
  EXPECT_TRUE(client_config.HasClientSentConnectionOption(
      kTBBR, Perspective::IS_CLIENT));

  CryptoHandshakeMessage msg;
  client_config.ToHandshakeMessage(&msg, version_.transport_version);

  std::string error_details;
  const QuicErrorCode error =
      config_.ProcessPeerHello(msg, CLIENT, &error_details);
  EXPECT_THAT(error, IsQuicNoError());
  EXPECT_TRUE(config_.negotiated());

  EXPECT_TRUE(config_.HasReceivedConnectionOptions());
  EXPECT_EQ(1u, config_.ReceivedConnectionOptions().size());
  EXPECT_TRUE(
      config_.HasClientSentConnectionOption(kTBBR, Perspective::IS_SERVER));
}

TEST_P(QuicConfigTest, DontSendClientConnectionOptions) {
  if (version_.UsesTls()) {
    // CryptoHandshakeMessage is only used for QUIC_CRYPTO.
    return;
  }
  QuicConfig client_config;
  QuicTagVector copt;
  copt.push_back(kTBBR);
  client_config.SetClientConnectionOptions(copt);

  CryptoHandshakeMessage msg;
  client_config.ToHandshakeMessage(&msg, version_.transport_version);

  std::string error_details;
  const QuicErrorCode error =
      config_.ProcessPeerHello(msg, CLIENT, &error_details);
  EXPECT_THAT(error, IsQuicNoError());
  EXPECT_TRUE(config_.negotiated());

  EXPECT_FALSE(config_.HasReceivedConnectionOptions());
}

TEST_P(QuicConfigTest, HasClientRequestedIndependentOption) {
  if (version_.UsesTls()) {
    // CryptoHandshakeMessage is only used for QUIC_CRYPTO.
    return;
  }
  QuicConfig client_config;
  QuicTagVector client_opt;
  client_opt.push_back(kRENO);
  QuicTagVector copt;
  copt.push_back(kTBBR);
  client_config.SetClientConnectionOptions(client_opt);
  client_config.SetConnectionOptionsToSend(copt);
  EXPECT_TRUE(client_config.HasClientSentConnectionOption(
      kTBBR, Perspective::IS_CLIENT));
  EXPECT_TRUE(client_config.HasClientRequestedIndependentOption(
      kRENO, Perspective::IS_CLIENT));
  EXPECT_FALSE(client_config.HasClientRequestedIndependentOption(
      kTBBR, Perspective::IS_CLIENT));

  CryptoHandshakeMessage msg;
  client_config.ToHandshakeMessage(&msg, version_.transport_version);

  std::string error_details;
  const QuicErrorCode error =
      config_.ProcessPeerHello(msg, CLIENT, &error_details);
  EXPECT_THAT(error, IsQuicNoError());
  EXPECT_TRUE(config_.negotiated());

  EXPECT_TRUE(config_.HasReceivedConnectionOptions());
  EXPECT_EQ(1u, config_.ReceivedConnectionOptions().size());
  EXPECT_FALSE(config_.HasClientRequestedIndependentOption(
      kRENO, Perspective::IS_SERVER));
  EXPECT_TRUE(config_.HasClientRequestedIndependentOption(
      kTBBR, Perspective::IS_SERVER));
}

TEST_P(QuicConfigTest, IncomingLargeIdleTimeoutTransportParameter) {
  if (!version_.UsesTls()) {
    // TransportParameters are only used for QUIC+TLS.
    return;
  }
  // Configure our idle timeout to 60s, then receive 120s from peer.
  // Since the received value is above ours, we should then use ours.
  config_.SetIdleNetworkTimeout(quic::QuicTime::Delta::FromSeconds(60));
  TransportParameters params;
  params.max_idle_timeout_ms.set_value(120000);

  std::string error_details = "foobar";
  EXPECT_THAT(config_.ProcessTransportParameters(
                  params, /* is_resumption = */ false, &error_details),
              IsQuicNoError());
  EXPECT_EQ("", error_details);
  EXPECT_EQ(quic::QuicTime::Delta::FromSeconds(60),
            config_.IdleNetworkTimeout());
}

TEST_P(QuicConfigTest, ReceivedInvalidMinAckDelayInTransportParameter) {
  if (!version_.UsesTls()) {
    // TransportParameters are only used for QUIC+TLS.
    return;
  }
  SetQuicReloadableFlag(quic_record_received_min_ack_delay, true);
  TransportParameters params;

  params.max_ack_delay.set_value(25 /*ms*/);
  params.min_ack_delay_us.set_value(25 * kNumMicrosPerMilli + 1);
  std::string error_details = "foobar";
  EXPECT_THAT(config_.ProcessTransportParameters(
                  params, /* is_resumption = */ false, &error_details),
              IsError(IETF_QUIC_PROTOCOL_VIOLATION));
  EXPECT_EQ("MinAckDelay is greater than MaxAckDelay.", error_details);

  params.max_ack_delay.set_value(25 /*ms*/);
  params.min_ack_delay_us.set_value(25 * kNumMicrosPerMilli);
  EXPECT_THAT(config_.ProcessTransportParameters(
                  params, /* is_resumption = */ false, &error_details),
              IsQuicNoError());
  EXPECT_TRUE(error_details.empty());
}

TEST_P(QuicConfigTest, FillTransportParams) {
  if (!version_.UsesTls()) {
    // TransportParameters are only used for QUIC+TLS.
    return;
  }
  config_.SetInitialMaxStreamDataBytesIncomingBidirectionalToSend(
      2 * kMinimumFlowControlSendWindow);
  config_.SetInitialMaxStreamDataBytesOutgoingBidirectionalToSend(
      3 * kMinimumFlowControlSendWindow);
  config_.SetInitialMaxStreamDataBytesUnidirectionalToSend(
      4 * kMinimumFlowControlSendWindow);
  config_.SetMaxPacketSizeToSend(kMaxPacketSizeForTest);
  config_.SetMaxDatagramFrameSizeToSend(kMaxDatagramFrameSizeForTest);
  config_.SetActiveConnectionIdLimitToSend(kActiveConnectionIdLimitForTest);

  config_.SetOriginalConnectionIdToSend(TestConnectionId(0x1111));
  config_.SetInitialSourceConnectionIdToSend(TestConnectionId(0x2222));
  config_.SetRetrySourceConnectionIdToSend(TestConnectionId(0x3333));
  config_.SetMinAckDelayMs(kDefaultMinAckDelayTimeMs);

  TransportParameters params;
  config_.FillTransportParameters(&params);

  EXPECT_EQ(2 * kMinimumFlowControlSendWindow,
            params.initial_max_stream_data_bidi_remote.value());
  EXPECT_EQ(3 * kMinimumFlowControlSendWindow,
            params.initial_max_stream_data_bidi_local.value());
  EXPECT_EQ(4 * kMinimumFlowControlSendWindow,
            params.initial_max_stream_data_uni.value());

  EXPECT_EQ(static_cast<uint64_t>(kMaximumIdleTimeoutSecs * 1000),
            params.max_idle_timeout_ms.value());

  EXPECT_EQ(kMaxPacketSizeForTest, params.max_udp_payload_size.value());
  EXPECT_EQ(kMaxDatagramFrameSizeForTest,
            params.max_datagram_frame_size.value());
  EXPECT_EQ(kActiveConnectionIdLimitForTest,
            params.active_connection_id_limit.value());

  ASSERT_TRUE(params.original_destination_connection_id.has_value());
  EXPECT_EQ(TestConnectionId(0x1111),
            params.original_destination_connection_id.value());
  ASSERT_TRUE(params.initial_source_connection_id.has_value());
  EXPECT_EQ(TestConnectionId(0x2222),
            params.initial_source_connection_id.value());
  ASSERT_TRUE(params.retry_source_connection_id.has_value());
  EXPECT_EQ(TestConnectionId(0x3333),
            params.retry_source_connection_id.value());

  EXPECT_EQ(
      static_cast<uint64_t>(kDefaultMinAckDelayTimeMs) * kNumMicrosPerMilli,
      params.min_ack_delay_us.value());
  EXPECT_EQ(params.key_update_not_yet_supported,
            GetQuicReloadableFlag(quic_send_key_update_not_yet_supported));
}

TEST_P(QuicConfigTest, ProcessTransportParametersServer) {
  if (!version_.UsesTls()) {
    // TransportParameters are only used for QUIC+TLS.
    return;
  }
  TransportParameters params;

  params.initial_max_stream_data_bidi_local.set_value(
      2 * kMinimumFlowControlSendWindow);
  params.initial_max_stream_data_bidi_remote.set_value(
      3 * kMinimumFlowControlSendWindow);
  params.initial_max_stream_data_uni.set_value(4 *
                                               kMinimumFlowControlSendWindow);
  params.max_udp_payload_size.set_value(kMaxPacketSizeForTest);
  params.max_datagram_frame_size.set_value(kMaxDatagramFrameSizeForTest);
  params.initial_max_streams_bidi.set_value(kDefaultMaxStreamsPerConnection);
  params.stateless_reset_token = CreateStatelessResetTokenForTest();
  params.max_ack_delay.set_value(kMaxAckDelayForTest);
  params.min_ack_delay_us.set_value(kMinAckDelayUsForTest);
  params.ack_delay_exponent.set_value(kAckDelayExponentForTest);
  params.active_connection_id_limit.set_value(kActiveConnectionIdLimitForTest);
  params.original_destination_connection_id = TestConnectionId(0x1111);
  params.initial_source_connection_id = TestConnectionId(0x2222);
  params.retry_source_connection_id = TestConnectionId(0x3333);

  std::string error_details;
  EXPECT_THAT(config_.ProcessTransportParameters(
                  params, /* is_resumption = */ true, &error_details),
              IsQuicNoError())
      << error_details;

  EXPECT_FALSE(config_.negotiated());

  ASSERT_TRUE(
      config_.HasReceivedInitialMaxStreamDataBytesIncomingBidirectional());
  EXPECT_EQ(2 * kMinimumFlowControlSendWindow,
            config_.ReceivedInitialMaxStreamDataBytesIncomingBidirectional());

  ASSERT_TRUE(
      config_.HasReceivedInitialMaxStreamDataBytesOutgoingBidirectional());
  EXPECT_EQ(3 * kMinimumFlowControlSendWindow,
            config_.ReceivedInitialMaxStreamDataBytesOutgoingBidirectional());

  ASSERT_TRUE(config_.HasReceivedInitialMaxStreamDataBytesUnidirectional());
  EXPECT_EQ(4 * kMinimumFlowControlSendWindow,
            config_.ReceivedInitialMaxStreamDataBytesUnidirectional());

  ASSERT_TRUE(config_.HasReceivedMaxPacketSize());
  EXPECT_EQ(kMaxPacketSizeForTest, config_.ReceivedMaxPacketSize());

  ASSERT_TRUE(config_.HasReceivedMaxDatagramFrameSize());
  EXPECT_EQ(kMaxDatagramFrameSizeForTest,
            config_.ReceivedMaxDatagramFrameSize());

  ASSERT_TRUE(config_.HasReceivedMaxBidirectionalStreams());
  EXPECT_EQ(kDefaultMaxStreamsPerConnection,
            config_.ReceivedMaxBidirectionalStreams());

  EXPECT_FALSE(config_.DisableConnectionMigration());
  EXPECT_FALSE(config_.PeerSupportsHandshakeDone());

  // The following config shouldn't be processed because of resumption.
  EXPECT_FALSE(config_.HasReceivedStatelessResetToken());
  EXPECT_FALSE(config_.HasReceivedMaxAckDelayMs());
  EXPECT_FALSE(config_.HasReceivedAckDelayExponent());
  EXPECT_FALSE(config_.HasReceivedMinAckDelayMs());
  EXPECT_FALSE(config_.HasReceivedOriginalConnectionId());
  EXPECT_FALSE(config_.HasReceivedInitialSourceConnectionId());
  EXPECT_FALSE(config_.HasReceivedRetrySourceConnectionId());

  // Let the config process another slightly tweaked transport paramters.
  // Note that the values for flow control and stream limit cannot be smaller
  // than before. This rule is enforced in QuicSession::OnConfigNegotiated().
  params.initial_max_stream_data_bidi_local.set_value(
      2 * kMinimumFlowControlSendWindow + 1);
  params.initial_max_stream_data_bidi_remote.set_value(
      4 * kMinimumFlowControlSendWindow);
  params.initial_max_stream_data_uni.set_value(5 *
                                               kMinimumFlowControlSendWindow);
  params.max_udp_payload_size.set_value(2 * kMaxPacketSizeForTest);
  params.max_datagram_frame_size.set_value(2 * kMaxDatagramFrameSizeForTest);
  params.initial_max_streams_bidi.set_value(2 *
                                            kDefaultMaxStreamsPerConnection);
  params.disable_active_migration = true;
  params.support_handshake_done = true;

  EXPECT_THAT(config_.ProcessTransportParameters(
                  params, /* is_resumption = */ false, &error_details),
              IsQuicNoError())
      << error_details;

  EXPECT_TRUE(config_.negotiated());

  ASSERT_TRUE(
      config_.HasReceivedInitialMaxStreamDataBytesIncomingBidirectional());
  EXPECT_EQ(2 * kMinimumFlowControlSendWindow + 1,
            config_.ReceivedInitialMaxStreamDataBytesIncomingBidirectional());

  ASSERT_TRUE(
      config_.HasReceivedInitialMaxStreamDataBytesOutgoingBidirectional());
  EXPECT_EQ(4 * kMinimumFlowControlSendWindow,
            config_.ReceivedInitialMaxStreamDataBytesOutgoingBidirectional());

  ASSERT_TRUE(config_.HasReceivedInitialMaxStreamDataBytesUnidirectional());
  EXPECT_EQ(5 * kMinimumFlowControlSendWindow,
            config_.ReceivedInitialMaxStreamDataBytesUnidirectional());

  ASSERT_TRUE(config_.HasReceivedMaxPacketSize());
  EXPECT_EQ(2 * kMaxPacketSizeForTest, config_.ReceivedMaxPacketSize());

  ASSERT_TRUE(config_.HasReceivedMaxDatagramFrameSize());
  EXPECT_EQ(2 * kMaxDatagramFrameSizeForTest,
            config_.ReceivedMaxDatagramFrameSize());

  ASSERT_TRUE(config_.HasReceivedMaxBidirectionalStreams());
  EXPECT_EQ(2 * kDefaultMaxStreamsPerConnection,
            config_.ReceivedMaxBidirectionalStreams());

  EXPECT_TRUE(config_.DisableConnectionMigration());
  EXPECT_TRUE(config_.PeerSupportsHandshakeDone());

  ASSERT_TRUE(config_.HasReceivedStatelessResetToken());

  ASSERT_TRUE(config_.HasReceivedMaxAckDelayMs());
  EXPECT_EQ(config_.ReceivedMaxAckDelayMs(), kMaxAckDelayForTest);

  if (GetQuicReloadableFlag(quic_record_received_min_ack_delay)) {
    ASSERT_TRUE(config_.HasReceivedMinAckDelayMs());
    EXPECT_EQ(config_.ReceivedMinAckDelayMs(),
              kMinAckDelayUsForTest / kNumMicrosPerMilli);
  } else {
    ASSERT_FALSE(config_.HasReceivedMinAckDelayMs());
  }

  ASSERT_TRUE(config_.HasReceivedAckDelayExponent());
  EXPECT_EQ(config_.ReceivedAckDelayExponent(), kAckDelayExponentForTest);

  ASSERT_TRUE(config_.HasReceivedActiveConnectionIdLimit());
  EXPECT_EQ(config_.ReceivedActiveConnectionIdLimit(),
            kActiveConnectionIdLimitForTest);

  ASSERT_TRUE(config_.HasReceivedOriginalConnectionId());
  EXPECT_EQ(config_.ReceivedOriginalConnectionId(), TestConnectionId(0x1111));
  ASSERT_TRUE(config_.HasReceivedInitialSourceConnectionId());
  EXPECT_EQ(config_.ReceivedInitialSourceConnectionId(),
            TestConnectionId(0x2222));
  ASSERT_TRUE(config_.HasReceivedRetrySourceConnectionId());
  EXPECT_EQ(config_.ReceivedRetrySourceConnectionId(),
            TestConnectionId(0x3333));
}

TEST_P(QuicConfigTest, DisableMigrationTransportParameter) {
  if (!version_.UsesTls()) {
    // TransportParameters are only used for QUIC+TLS.
    return;
  }
  TransportParameters params;
  params.disable_active_migration = true;
  std::string error_details;
  EXPECT_THAT(config_.ProcessTransportParameters(
                  params, /* is_resumption = */ false, &error_details),
              IsQuicNoError());
  EXPECT_TRUE(config_.DisableConnectionMigration());
}

}  // namespace
}  // namespace test
}  // namespace quic

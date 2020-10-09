// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/transport_parameters.h"

#include <cstring>
#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_tag.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_expect_bug.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace test {
namespace {

const QuicVersionLabel kFakeVersionLabel = 0x01234567;
const QuicVersionLabel kFakeVersionLabel2 = 0x89ABCDEF;
const uint64_t kFakeIdleTimeoutMilliseconds = 12012;
const uint64_t kFakeInitialMaxData = 101;
const uint64_t kFakeInitialMaxStreamDataBidiLocal = 2001;
const uint64_t kFakeInitialMaxStreamDataBidiRemote = 2002;
const uint64_t kFakeInitialMaxStreamDataUni = 3000;
const uint64_t kFakeInitialMaxStreamsBidi = 21;
const uint64_t kFakeInitialMaxStreamsUni = 22;
const bool kFakeDisableMigration = true;
const uint64_t kFakeInitialRoundTripTime = 53;
const uint8_t kFakePreferredStatelessResetTokenData[16] = {
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F};
const bool kFakeSupportHandshakeDone = true;
const bool kFakeKeyUpdateNotYetSupported = true;

const auto kCustomParameter1 =
    static_cast<TransportParameters::TransportParameterId>(0xffcd);
const char* kCustomParameter1Value = "foo";
const auto kCustomParameter2 =
    static_cast<TransportParameters::TransportParameterId>(0xff34);
const char* kCustomParameter2Value = "bar";

QuicConnectionId CreateFakeOriginalDestinationConnectionId() {
  return TestConnectionId(0x1337);
}

QuicConnectionId CreateFakeInitialSourceConnectionId() {
  return TestConnectionId(0x2345);
}

QuicConnectionId CreateFakeRetrySourceConnectionId() {
  return TestConnectionId(0x9876);
}

QuicConnectionId CreateFakePreferredConnectionId() {
  return TestConnectionId(0xBEEF);
}

std::vector<uint8_t> CreateFakePreferredStatelessResetToken() {
  return std::vector<uint8_t>(
      kFakePreferredStatelessResetTokenData,
      kFakePreferredStatelessResetTokenData +
          sizeof(kFakePreferredStatelessResetTokenData));
}

QuicSocketAddress CreateFakeV4SocketAddress() {
  QuicIpAddress ipv4_address;
  if (!ipv4_address.FromString("65.66.67.68")) {  // 0x41, 0x42, 0x43, 0x44
    QUIC_LOG(FATAL) << "Failed to create IPv4 address";
    return QuicSocketAddress();
  }
  return QuicSocketAddress(ipv4_address, 0x4884);
}

QuicSocketAddress CreateFakeV6SocketAddress() {
  QuicIpAddress ipv6_address;
  if (!ipv6_address.FromString("6061:6263:6465:6667:6869:6A6B:6C6D:6E6F")) {
    QUIC_LOG(FATAL) << "Failed to create IPv6 address";
    return QuicSocketAddress();
  }
  return QuicSocketAddress(ipv6_address, 0x6336);
}

std::unique_ptr<TransportParameters::PreferredAddress>
CreateFakePreferredAddress() {
  TransportParameters::PreferredAddress preferred_address;
  preferred_address.ipv4_socket_address = CreateFakeV4SocketAddress();
  preferred_address.ipv6_socket_address = CreateFakeV6SocketAddress();
  preferred_address.connection_id = CreateFakePreferredConnectionId();
  preferred_address.stateless_reset_token =
      CreateFakePreferredStatelessResetToken();
  return std::make_unique<TransportParameters::PreferredAddress>(
      preferred_address);
}

QuicTagVector CreateFakeGoogleConnectionOptions() {
  return {kALPN, MakeQuicTag('E', 'F', 'G', 0x00),
          MakeQuicTag('H', 'I', 'J', 0xff)};
}

std::string CreateFakeUserAgentId() {
  return "FakeUAID";
}

void RemoveGreaseParameters(TransportParameters* params) {
  std::vector<TransportParameters::TransportParameterId> grease_params;
  for (const auto& kv : params->custom_parameters) {
    if (kv.first % 31 == 27) {
      grease_params.push_back(kv.first);
    }
  }
  EXPECT_EQ(grease_params.size(), 1u);
  for (TransportParameters::TransportParameterId param_id : grease_params) {
    params->custom_parameters.erase(param_id);
  }
}

}  // namespace

class TransportParametersTest : public QuicTestWithParam<ParsedQuicVersion> {
 protected:
  TransportParametersTest() : version_(GetParam()) {}

  ParsedQuicVersion version_;
};

INSTANTIATE_TEST_SUITE_P(TransportParametersTests,
                         TransportParametersTest,
                         ::testing::ValuesIn(AllSupportedVersionsWithTls()),
                         ::testing::PrintToStringParamName());

TEST_P(TransportParametersTest, Comparator) {
  TransportParameters orig_params;
  TransportParameters new_params;
  // Test comparison on primitive members.
  orig_params.perspective = Perspective::IS_CLIENT;
  new_params.perspective = Perspective::IS_SERVER;
  EXPECT_NE(orig_params, new_params);
  EXPECT_FALSE(orig_params == new_params);
  EXPECT_TRUE(orig_params != new_params);
  new_params.perspective = Perspective::IS_CLIENT;
  orig_params.version = kFakeVersionLabel;
  new_params.version = kFakeVersionLabel;
  orig_params.disable_active_migration = true;
  new_params.disable_active_migration = true;
  EXPECT_EQ(orig_params, new_params);
  EXPECT_TRUE(orig_params == new_params);
  EXPECT_FALSE(orig_params != new_params);

  // Test comparison on vectors.
  orig_params.supported_versions.push_back(kFakeVersionLabel);
  new_params.supported_versions.push_back(kFakeVersionLabel2);
  EXPECT_NE(orig_params, new_params);
  EXPECT_FALSE(orig_params == new_params);
  EXPECT_TRUE(orig_params != new_params);
  new_params.supported_versions.pop_back();
  new_params.supported_versions.push_back(kFakeVersionLabel);
  orig_params.stateless_reset_token = CreateStatelessResetTokenForTest();
  new_params.stateless_reset_token = CreateStatelessResetTokenForTest();
  EXPECT_EQ(orig_params, new_params);
  EXPECT_TRUE(orig_params == new_params);
  EXPECT_FALSE(orig_params != new_params);

  // Test comparison on IntegerParameters.
  orig_params.max_udp_payload_size.set_value(kMaxPacketSizeForTest);
  new_params.max_udp_payload_size.set_value(kMaxPacketSizeForTest + 1);
  EXPECT_NE(orig_params, new_params);
  EXPECT_FALSE(orig_params == new_params);
  EXPECT_TRUE(orig_params != new_params);
  new_params.max_udp_payload_size.set_value(kMaxPacketSizeForTest);
  EXPECT_EQ(orig_params, new_params);
  EXPECT_TRUE(orig_params == new_params);
  EXPECT_FALSE(orig_params != new_params);

  // Test comparison on PreferredAddress
  orig_params.preferred_address = CreateFakePreferredAddress();
  EXPECT_NE(orig_params, new_params);
  EXPECT_FALSE(orig_params == new_params);
  EXPECT_TRUE(orig_params != new_params);
  new_params.preferred_address = CreateFakePreferredAddress();
  EXPECT_EQ(orig_params, new_params);
  EXPECT_TRUE(orig_params == new_params);
  EXPECT_FALSE(orig_params != new_params);

  // Test comparison on CustomMap
  orig_params.custom_parameters[kCustomParameter1] = kCustomParameter1Value;
  orig_params.custom_parameters[kCustomParameter2] = kCustomParameter2Value;

  new_params.custom_parameters[kCustomParameter2] = kCustomParameter2Value;
  new_params.custom_parameters[kCustomParameter1] = kCustomParameter1Value;
  EXPECT_EQ(orig_params, new_params);
  EXPECT_TRUE(orig_params == new_params);
  EXPECT_FALSE(orig_params != new_params);

  // Test comparison on connection IDs.
  orig_params.initial_source_connection_id =
      CreateFakeInitialSourceConnectionId();
  new_params.initial_source_connection_id = QUICHE_NULLOPT;
  EXPECT_NE(orig_params, new_params);
  EXPECT_FALSE(orig_params == new_params);
  EXPECT_TRUE(orig_params != new_params);
  new_params.initial_source_connection_id = TestConnectionId(0xbadbad);
  EXPECT_NE(orig_params, new_params);
  EXPECT_FALSE(orig_params == new_params);
  EXPECT_TRUE(orig_params != new_params);
  new_params.initial_source_connection_id =
      CreateFakeInitialSourceConnectionId();
  EXPECT_EQ(orig_params, new_params);
  EXPECT_TRUE(orig_params == new_params);
  EXPECT_FALSE(orig_params != new_params);
}

TEST_P(TransportParametersTest, CopyConstructor) {
  TransportParameters orig_params;
  orig_params.perspective = Perspective::IS_CLIENT;
  orig_params.version = kFakeVersionLabel;
  orig_params.supported_versions.push_back(kFakeVersionLabel);
  orig_params.supported_versions.push_back(kFakeVersionLabel2);
  orig_params.original_destination_connection_id =
      CreateFakeOriginalDestinationConnectionId();
  orig_params.max_idle_timeout_ms.set_value(kFakeIdleTimeoutMilliseconds);
  orig_params.stateless_reset_token = CreateStatelessResetTokenForTest();
  orig_params.max_udp_payload_size.set_value(kMaxPacketSizeForTest);
  orig_params.initial_max_data.set_value(kFakeInitialMaxData);
  orig_params.initial_max_stream_data_bidi_local.set_value(
      kFakeInitialMaxStreamDataBidiLocal);
  orig_params.initial_max_stream_data_bidi_remote.set_value(
      kFakeInitialMaxStreamDataBidiRemote);
  orig_params.initial_max_stream_data_uni.set_value(
      kFakeInitialMaxStreamDataUni);
  orig_params.initial_max_streams_bidi.set_value(kFakeInitialMaxStreamsBidi);
  orig_params.initial_max_streams_uni.set_value(kFakeInitialMaxStreamsUni);
  orig_params.ack_delay_exponent.set_value(kAckDelayExponentForTest);
  orig_params.max_ack_delay.set_value(kMaxAckDelayForTest);
  orig_params.min_ack_delay_us.set_value(kMinAckDelayUsForTest);
  orig_params.disable_active_migration = kFakeDisableMigration;
  orig_params.preferred_address = CreateFakePreferredAddress();
  orig_params.active_connection_id_limit.set_value(
      kActiveConnectionIdLimitForTest);
  orig_params.initial_source_connection_id =
      CreateFakeInitialSourceConnectionId();
  orig_params.retry_source_connection_id = CreateFakeRetrySourceConnectionId();
  orig_params.initial_round_trip_time_us.set_value(kFakeInitialRoundTripTime);
  orig_params.google_connection_options = CreateFakeGoogleConnectionOptions();
  orig_params.user_agent_id = CreateFakeUserAgentId();
  orig_params.support_handshake_done = kFakeSupportHandshakeDone;
  orig_params.key_update_not_yet_supported = kFakeKeyUpdateNotYetSupported;
  orig_params.custom_parameters[kCustomParameter1] = kCustomParameter1Value;
  orig_params.custom_parameters[kCustomParameter2] = kCustomParameter2Value;

  TransportParameters new_params(orig_params);
  EXPECT_EQ(new_params, orig_params);
}

TEST_P(TransportParametersTest, RoundTripClient) {
  TransportParameters orig_params;
  orig_params.perspective = Perspective::IS_CLIENT;
  orig_params.version = kFakeVersionLabel;
  orig_params.max_idle_timeout_ms.set_value(kFakeIdleTimeoutMilliseconds);
  orig_params.max_udp_payload_size.set_value(kMaxPacketSizeForTest);
  orig_params.initial_max_data.set_value(kFakeInitialMaxData);
  orig_params.initial_max_stream_data_bidi_local.set_value(
      kFakeInitialMaxStreamDataBidiLocal);
  orig_params.initial_max_stream_data_bidi_remote.set_value(
      kFakeInitialMaxStreamDataBidiRemote);
  orig_params.initial_max_stream_data_uni.set_value(
      kFakeInitialMaxStreamDataUni);
  orig_params.initial_max_streams_bidi.set_value(kFakeInitialMaxStreamsBidi);
  orig_params.initial_max_streams_uni.set_value(kFakeInitialMaxStreamsUni);
  orig_params.ack_delay_exponent.set_value(kAckDelayExponentForTest);
  orig_params.max_ack_delay.set_value(kMaxAckDelayForTest);
  orig_params.min_ack_delay_us.set_value(kMinAckDelayUsForTest);
  orig_params.disable_active_migration = kFakeDisableMigration;
  orig_params.active_connection_id_limit.set_value(
      kActiveConnectionIdLimitForTest);
  orig_params.initial_source_connection_id =
      CreateFakeInitialSourceConnectionId();
  orig_params.initial_round_trip_time_us.set_value(kFakeInitialRoundTripTime);
  orig_params.google_connection_options = CreateFakeGoogleConnectionOptions();
  orig_params.user_agent_id = CreateFakeUserAgentId();
  orig_params.support_handshake_done = kFakeSupportHandshakeDone;
  orig_params.key_update_not_yet_supported = kFakeKeyUpdateNotYetSupported;
  orig_params.custom_parameters[kCustomParameter1] = kCustomParameter1Value;
  orig_params.custom_parameters[kCustomParameter2] = kCustomParameter2Value;

  std::vector<uint8_t> serialized;
  ASSERT_TRUE(SerializeTransportParameters(version_, orig_params, &serialized));

  TransportParameters new_params;
  std::string error_details;
  ASSERT_TRUE(ParseTransportParameters(version_, Perspective::IS_CLIENT,
                                       serialized.data(), serialized.size(),
                                       &new_params, &error_details))
      << error_details;
  EXPECT_TRUE(error_details.empty());
  RemoveGreaseParameters(&new_params);
  EXPECT_EQ(new_params, orig_params);
}

TEST_P(TransportParametersTest, RoundTripServer) {
  TransportParameters orig_params;
  orig_params.perspective = Perspective::IS_SERVER;
  orig_params.version = kFakeVersionLabel;
  orig_params.supported_versions.push_back(kFakeVersionLabel);
  orig_params.supported_versions.push_back(kFakeVersionLabel2);
  orig_params.original_destination_connection_id =
      CreateFakeOriginalDestinationConnectionId();
  orig_params.max_idle_timeout_ms.set_value(kFakeIdleTimeoutMilliseconds);
  orig_params.stateless_reset_token = CreateStatelessResetTokenForTest();
  orig_params.max_udp_payload_size.set_value(kMaxPacketSizeForTest);
  orig_params.initial_max_data.set_value(kFakeInitialMaxData);
  orig_params.initial_max_stream_data_bidi_local.set_value(
      kFakeInitialMaxStreamDataBidiLocal);
  orig_params.initial_max_stream_data_bidi_remote.set_value(
      kFakeInitialMaxStreamDataBidiRemote);
  orig_params.initial_max_stream_data_uni.set_value(
      kFakeInitialMaxStreamDataUni);
  orig_params.initial_max_streams_bidi.set_value(kFakeInitialMaxStreamsBidi);
  orig_params.initial_max_streams_uni.set_value(kFakeInitialMaxStreamsUni);
  orig_params.ack_delay_exponent.set_value(kAckDelayExponentForTest);
  orig_params.max_ack_delay.set_value(kMaxAckDelayForTest);
  orig_params.min_ack_delay_us.set_value(kMinAckDelayUsForTest);
  orig_params.disable_active_migration = kFakeDisableMigration;
  orig_params.preferred_address = CreateFakePreferredAddress();
  orig_params.active_connection_id_limit.set_value(
      kActiveConnectionIdLimitForTest);
  orig_params.initial_source_connection_id =
      CreateFakeInitialSourceConnectionId();
  orig_params.retry_source_connection_id = CreateFakeRetrySourceConnectionId();
  orig_params.google_connection_options = CreateFakeGoogleConnectionOptions();

  std::vector<uint8_t> serialized;
  ASSERT_TRUE(SerializeTransportParameters(version_, orig_params, &serialized));

  TransportParameters new_params;
  std::string error_details;
  ASSERT_TRUE(ParseTransportParameters(version_, Perspective::IS_SERVER,
                                       serialized.data(), serialized.size(),
                                       &new_params, &error_details))
      << error_details;
  EXPECT_TRUE(error_details.empty());
  RemoveGreaseParameters(&new_params);
  EXPECT_EQ(new_params, orig_params);
}

TEST_P(TransportParametersTest, AreValid) {
  {
    TransportParameters params;
    std::string error_details;
    params.perspective = Perspective::IS_CLIENT;
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
  }
  {
    TransportParameters params;
    std::string error_details;
    params.perspective = Perspective::IS_CLIENT;
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.max_idle_timeout_ms.set_value(kFakeIdleTimeoutMilliseconds);
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.max_idle_timeout_ms.set_value(601000);
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
  }
  {
    TransportParameters params;
    std::string error_details;
    params.perspective = Perspective::IS_CLIENT;
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.max_udp_payload_size.set_value(1200);
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.max_udp_payload_size.set_value(65535);
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.max_udp_payload_size.set_value(9999999);
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.max_udp_payload_size.set_value(0);
    error_details = "";
    EXPECT_FALSE(params.AreValid(&error_details));
    EXPECT_EQ(error_details,
              "Invalid transport parameters [Client max_udp_payload_size 0 "
              "(Invalid)]");
    params.max_udp_payload_size.set_value(1199);
    error_details = "";
    EXPECT_FALSE(params.AreValid(&error_details));
    EXPECT_EQ(error_details,
              "Invalid transport parameters [Client max_udp_payload_size 1199 "
              "(Invalid)]");
  }
  {
    TransportParameters params;
    std::string error_details;
    params.perspective = Perspective::IS_CLIENT;
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.ack_delay_exponent.set_value(0);
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.ack_delay_exponent.set_value(20);
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.ack_delay_exponent.set_value(21);
    EXPECT_FALSE(params.AreValid(&error_details));
    EXPECT_EQ(error_details,
              "Invalid transport parameters [Client ack_delay_exponent 21 "
              "(Invalid)]");
  }
  {
    TransportParameters params;
    std::string error_details;
    params.perspective = Perspective::IS_CLIENT;
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.active_connection_id_limit.set_value(2);
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.active_connection_id_limit.set_value(999999);
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.active_connection_id_limit.set_value(1);
    EXPECT_FALSE(params.AreValid(&error_details));
    EXPECT_EQ(error_details,
              "Invalid transport parameters [Client active_connection_id_limit"
              " 1 (Invalid)]");
    params.active_connection_id_limit.set_value(0);
    EXPECT_FALSE(params.AreValid(&error_details));
    EXPECT_EQ(error_details,
              "Invalid transport parameters [Client active_connection_id_limit"
              " 0 (Invalid)]");
  }
}

TEST_P(TransportParametersTest, NoClientParamsWithStatelessResetToken) {
  TransportParameters orig_params;
  orig_params.perspective = Perspective::IS_CLIENT;
  orig_params.version = kFakeVersionLabel;
  orig_params.max_idle_timeout_ms.set_value(kFakeIdleTimeoutMilliseconds);
  orig_params.stateless_reset_token = CreateStatelessResetTokenForTest();
  orig_params.max_udp_payload_size.set_value(kMaxPacketSizeForTest);

  std::vector<uint8_t> out;
  bool ok;
  EXPECT_QUIC_BUG(
      ok = SerializeTransportParameters(version_, orig_params, &out),
      "Not serializing invalid transport parameters: Client cannot send "
      "stateless reset token");
  EXPECT_FALSE(ok);
}

TEST_P(TransportParametersTest, ParseClientParams) {
  // clang-format off
  const uint8_t kClientParamsOld[] = {
      0x00, 0x84,              // length of the parameters array that follows
      // max_idle_timeout
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
      // max_udp_payload_size
      0x00, 0x03,  // parameter id
      0x00, 0x02,  // length
      0x63, 0x29,  // value
      // initial_max_data
      0x00, 0x04,  // parameter id
      0x00, 0x02,  // length
      0x40, 0x65,  // value
      // initial_max_stream_data_bidi_local
      0x00, 0x05,  // parameter id
      0x00, 0x02,  // length
      0x47, 0xD1,  // value
      // initial_max_stream_data_bidi_remote
      0x00, 0x06,  // parameter id
      0x00, 0x02,  // length
      0x47, 0xD2,  // value
      // initial_max_stream_data_uni
      0x00, 0x07,  // parameter id
      0x00, 0x02,  // length
      0x4B, 0xB8,  // value
      // initial_max_streams_bidi
      0x00, 0x08,  // parameter id
      0x00, 0x01,  // length
      0x15,  // value
      // initial_max_streams_uni
      0x00, 0x09,  // parameter id
      0x00, 0x01,  // length
      0x16,  // value
      // ack_delay_exponent
      0x00, 0x0a,  // parameter id
      0x00, 0x01,  // length
      0x0a,  // value
      // max_ack_delay
      0x00, 0x0b,  // parameter id
      0x00, 0x01,  // length
      0x33,  // value
      // min_ack_delay_us
      0xde, 0x1a,  // parameter id
      0x00, 0x02,  // length
      0x43, 0xe8,  // value
      // disable_active_migration
      0x00, 0x0c,  // parameter id
      0x00, 0x00,  // length
      // active_connection_id_limit
      0x00, 0x0e,  // parameter id
      0x00, 0x01,  // length
      0x34,  // value
      // initial_source_connection_id
      0x00, 0x0f,  // parameter id
      0x00, 0x08,  // length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23, 0x45,
      // initial_round_trip_time_us
      0x31, 0x27,  // parameter id
      0x00, 0x01,  // length
      0x35,  // value
      // google_connection_options
      0x31, 0x28,  // parameter id
      0x00, 0x0c,  // length
      'A', 'L', 'P', 'N',  // value
      'E', 'F', 'G', 0x00,
      'H', 'I', 'J', 0xff,
      // user_agent_id
      0x31, 0x29,  // parameter id
      0x00, 0x08,  // length
      'F', 'a', 'k', 'e', 'U', 'A', 'I', 'D',  // value
      // support_handshake_done
      0x31, 0x2A,  // parameter id
      0x00, 0x00,  // value
      // key_update_not_yet_supported
      0x31, 0x2B,  // parameter id
      0x00, 0x00,  // value
      // Google version extension
      0x47, 0x52,  // parameter id
      0x00, 0x04,  // length
      0x01, 0x23, 0x45, 0x67,  // initial version
  };
  const uint8_t kClientParams[] = {
      // max_idle_timeout
      0x01,  // parameter id
      0x02,  // length
      0x6e, 0xec,  // value
      // max_udp_payload_size
      0x03,  // parameter id
      0x02,  // length
      0x63, 0x29,  // value
      // initial_max_data
      0x04,  // parameter id
      0x02,  // length
      0x40, 0x65,  // value
      // initial_max_stream_data_bidi_local
      0x05,  // parameter id
      0x02,  // length
      0x47, 0xD1,  // value
      // initial_max_stream_data_bidi_remote
      0x06,  // parameter id
      0x02,  // length
      0x47, 0xD2,  // value
      // initial_max_stream_data_uni
      0x07,  // parameter id
      0x02,  // length
      0x4B, 0xB8,  // value
      // initial_max_streams_bidi
      0x08,  // parameter id
      0x01,  // length
      0x15,  // value
      // initial_max_streams_uni
      0x09,  // parameter id
      0x01,  // length
      0x16,  // value
      // ack_delay_exponent
      0x0a,  // parameter id
      0x01,  // length
      0x0a,  // value
      // max_ack_delay
      0x0b,  // parameter id
      0x01,  // length
      0x33,  // value
      // min_ack_delay_us
      0x80, 0x00, 0xde, 0x1a,  // parameter id
      0x02,  // length
      0x43, 0xe8,  // value
      // disable_active_migration
      0x0c,  // parameter id
      0x00,  // length
      // active_connection_id_limit
      0x0e,  // parameter id
      0x01,  // length
      0x34,  // value
      // initial_source_connection_id
      0x0f,  // parameter id
      0x08,  // length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23, 0x45,
      // initial_round_trip_time_us
      0x71, 0x27,  // parameter id
      0x01,  // length
      0x35,  // value
      // google_connection_options
      0x71, 0x28,  // parameter id
      0x0c,  // length
      'A', 'L', 'P', 'N',  // value
      'E', 'F', 'G', 0x00,
      'H', 'I', 'J', 0xff,
      // user_agent_id
      0x71, 0x29,  // parameter id
      0x08,  // length
      'F', 'a', 'k', 'e', 'U', 'A', 'I', 'D',  // value
      // support_handshake_done
      0x71, 0x2A,  // parameter id
      0x00,  // length
      // key_update_not_yet_supported
      0x71, 0x2B,  // parameter id
      0x00,  // length
      // Google version extension
      0x80, 0x00, 0x47, 0x52,  // parameter id
      0x04,  // length
      0x01, 0x23, 0x45, 0x67,  // initial version
  };
  // clang-format on
  const uint8_t* client_params =
      reinterpret_cast<const uint8_t*>(kClientParams);
  size_t client_params_length = QUICHE_ARRAYSIZE(kClientParams);
  if (!version_.HasVarIntTransportParams()) {
    client_params = reinterpret_cast<const uint8_t*>(kClientParamsOld);
    client_params_length = QUICHE_ARRAYSIZE(kClientParamsOld);
  }
  TransportParameters new_params;
  std::string error_details;
  ASSERT_TRUE(ParseTransportParameters(version_, Perspective::IS_CLIENT,
                                       client_params, client_params_length,
                                       &new_params, &error_details))
      << error_details;
  EXPECT_TRUE(error_details.empty());
  EXPECT_EQ(Perspective::IS_CLIENT, new_params.perspective);
  EXPECT_EQ(kFakeVersionLabel, new_params.version);
  EXPECT_TRUE(new_params.supported_versions.empty());
  EXPECT_FALSE(new_params.original_destination_connection_id.has_value());
  EXPECT_EQ(kFakeIdleTimeoutMilliseconds,
            new_params.max_idle_timeout_ms.value());
  EXPECT_TRUE(new_params.stateless_reset_token.empty());
  EXPECT_EQ(kMaxPacketSizeForTest, new_params.max_udp_payload_size.value());
  EXPECT_EQ(kFakeInitialMaxData, new_params.initial_max_data.value());
  EXPECT_EQ(kFakeInitialMaxStreamDataBidiLocal,
            new_params.initial_max_stream_data_bidi_local.value());
  EXPECT_EQ(kFakeInitialMaxStreamDataBidiRemote,
            new_params.initial_max_stream_data_bidi_remote.value());
  EXPECT_EQ(kFakeInitialMaxStreamDataUni,
            new_params.initial_max_stream_data_uni.value());
  EXPECT_EQ(kFakeInitialMaxStreamsBidi,
            new_params.initial_max_streams_bidi.value());
  EXPECT_EQ(kFakeInitialMaxStreamsUni,
            new_params.initial_max_streams_uni.value());
  EXPECT_EQ(kAckDelayExponentForTest, new_params.ack_delay_exponent.value());
  EXPECT_EQ(kMaxAckDelayForTest, new_params.max_ack_delay.value());
  EXPECT_EQ(kMinAckDelayUsForTest, new_params.min_ack_delay_us.value());
  EXPECT_EQ(kFakeDisableMigration, new_params.disable_active_migration);
  EXPECT_EQ(kActiveConnectionIdLimitForTest,
            new_params.active_connection_id_limit.value());
  ASSERT_TRUE(new_params.initial_source_connection_id.has_value());
  EXPECT_EQ(CreateFakeInitialSourceConnectionId(),
            new_params.initial_source_connection_id.value());
  EXPECT_FALSE(new_params.retry_source_connection_id.has_value());
  EXPECT_EQ(kFakeInitialRoundTripTime,
            new_params.initial_round_trip_time_us.value());
  ASSERT_TRUE(new_params.google_connection_options.has_value());
  EXPECT_EQ(CreateFakeGoogleConnectionOptions(),
            new_params.google_connection_options.value());
  ASSERT_TRUE(new_params.user_agent_id.has_value());
  EXPECT_EQ(CreateFakeUserAgentId(), new_params.user_agent_id.value());
  EXPECT_TRUE(new_params.support_handshake_done);
  EXPECT_TRUE(new_params.key_update_not_yet_supported);
}

TEST_P(TransportParametersTest,
       ParseClientParamsFailsWithFullStatelessResetToken) {
  // clang-format off
  const uint8_t kClientParamsWithFullTokenOld[] = {
      0x00, 0x26,  // length parameters array that follows
      // max_idle_timeout
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x00, 0x02,  // parameter id
      0x00, 0x10,  // length
      0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
      0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
      // max_udp_payload_size
      0x00, 0x03,  // parameter id
      0x00, 0x02,  // length
      0x63, 0x29,  // value
      // initial_max_data
      0x00, 0x04,  // parameter id
      0x00, 0x02,  // length
      0x40, 0x65,  // value
  };
  const uint8_t kClientParamsWithFullToken[] = {
      // max_idle_timeout
      0x01,  // parameter id
      0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x02,  // parameter id
      0x10,  // length
      0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
      0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
      // max_udp_payload_size
      0x03,  // parameter id
      0x02,  // length
      0x63, 0x29,  // value
      // initial_max_data
      0x04,  // parameter id
      0x02,  // length
      0x40, 0x65,  // value
  };
  // clang-format on
  const uint8_t* client_params =
      reinterpret_cast<const uint8_t*>(kClientParamsWithFullToken);
  size_t client_params_length = QUICHE_ARRAYSIZE(kClientParamsWithFullToken);
  if (!version_.HasVarIntTransportParams()) {
    client_params =
        reinterpret_cast<const uint8_t*>(kClientParamsWithFullTokenOld);
    client_params_length = QUICHE_ARRAYSIZE(kClientParamsWithFullTokenOld);
  }
  TransportParameters out_params;
  std::string error_details;
  EXPECT_FALSE(ParseTransportParameters(version_, Perspective::IS_CLIENT,
                                        client_params, client_params_length,
                                        &out_params, &error_details));
  EXPECT_EQ(error_details, "Client cannot send stateless reset token");
}

TEST_P(TransportParametersTest,
       ParseClientParamsFailsWithEmptyStatelessResetToken) {
  // clang-format off
  const uint8_t kClientParamsWithEmptyTokenOld[] = {
      0x00, 0x16,  // length parameters array that follows
      // max_idle_timeout
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x00, 0x02,  // parameter id
      0x00, 0x00,
      // max_udp_payload_size
      0x00, 0x03,  // parameter id
      0x00, 0x02,  // length
      0x63, 0x29,  // value
      // initial_max_data
      0x00, 0x04,  // parameter id
      0x00, 0x02,  // length
      0x40, 0x65,  // value
  };
  const uint8_t kClientParamsWithEmptyToken[] = {
      // max_idle_timeout
      0x01,  // parameter id
      0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x02,  // parameter id
      0x00,  // length
      // max_udp_payload_size
      0x03,  // parameter id
      0x02,  // length
      0x63, 0x29,  // value
      // initial_max_data
      0x04,  // parameter id
      0x02,  // length
      0x40, 0x65,  // value
  };
  // clang-format on
  const uint8_t* client_params =
      reinterpret_cast<const uint8_t*>(kClientParamsWithEmptyToken);
  size_t client_params_length = QUICHE_ARRAYSIZE(kClientParamsWithEmptyToken);
  if (!version_.HasVarIntTransportParams()) {
    client_params =
        reinterpret_cast<const uint8_t*>(kClientParamsWithEmptyTokenOld);
    client_params_length = QUICHE_ARRAYSIZE(kClientParamsWithEmptyTokenOld);
  }
  TransportParameters out_params;
  std::string error_details;
  EXPECT_FALSE(ParseTransportParameters(version_, Perspective::IS_CLIENT,
                                        client_params, client_params_length,
                                        &out_params, &error_details));
  EXPECT_EQ(error_details,
            "Received stateless_reset_token of invalid length 0");
}

TEST_P(TransportParametersTest, ParseClientParametersRepeated) {
  // clang-format off
  const uint8_t kClientParamsRepeatedOld[] = {
      0x00, 0x12,  // length parameters array that follows
      // max_idle_timeout
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
      // max_udp_payload_size
      0x00, 0x03,  // parameter id
      0x00, 0x02,  // length
      0x63, 0x29,  // value
      // max_idle_timeout (repeated)
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
  };
  const uint8_t kClientParamsRepeated[] = {
      // max_idle_timeout
      0x01,  // parameter id
      0x02,  // length
      0x6e, 0xec,  // value
      // max_udp_payload_size
      0x03,  // parameter id
      0x02,  // length
      0x63, 0x29,  // value
      // max_idle_timeout (repeated)
      0x01,  // parameter id
      0x02,  // length
      0x6e, 0xec,  // value
  };
  // clang-format on
  const uint8_t* client_params =
      reinterpret_cast<const uint8_t*>(kClientParamsRepeated);
  size_t client_params_length = QUICHE_ARRAYSIZE(kClientParamsRepeated);
  if (!version_.HasVarIntTransportParams()) {
    client_params = reinterpret_cast<const uint8_t*>(kClientParamsRepeatedOld);
    client_params_length = QUICHE_ARRAYSIZE(kClientParamsRepeatedOld);
  }
  TransportParameters out_params;
  std::string error_details;
  EXPECT_FALSE(ParseTransportParameters(version_, Perspective::IS_CLIENT,
                                        client_params, client_params_length,
                                        &out_params, &error_details));
  EXPECT_EQ(error_details, "Received a second max_idle_timeout");
}

TEST_P(TransportParametersTest, ParseServerParams) {
  // clang-format off
  const uint8_t kServerParamsOld[] = {
      0x00, 0xdd,  // length of parameters array that follows
      // original_destination_connection_id
      0x00, 0x00,  // parameter id
      0x00, 0x08,  // length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x37,
      // max_idle_timeout
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x00, 0x02,  // parameter id
      0x00, 0x10,  // length
      0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
      0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
      // max_udp_payload_size
      0x00, 0x03,  // parameter id
      0x00, 0x02,  // length
      0x63, 0x29,  // value
      // initial_max_data
      0x00, 0x04,  // parameter id
      0x00, 0x02,  // length
      0x40, 0x65,  // value
      // initial_max_stream_data_bidi_local
      0x00, 0x05,  // parameter id
      0x00, 0x02,  // length
      0x47, 0xD1,  // value
      // initial_max_stream_data_bidi_remote
      0x00, 0x06,  // parameter id
      0x00, 0x02,  // length
      0x47, 0xD2,  // value
      // initial_max_stream_data_uni
      0x00, 0x07,  // parameter id
      0x00, 0x02,  // length
      0x4B, 0xB8,  // value
      // initial_max_streams_bidi
      0x00, 0x08,  // parameter id
      0x00, 0x01,  // length
      0x15,  // value
      // initial_max_streams_uni
      0x00, 0x09,  // parameter id
      0x00, 0x01,  // length
      0x16,  // value
      // ack_delay_exponent
      0x00, 0x0a,  // parameter id
      0x00, 0x01,  // length
      0x0a,  // value
      // max_ack_delay
      0x00, 0x0b,  // parameter id
      0x00, 0x01,  // length
      0x33,  // value
      // min_ack_delay_us
      0xde, 0x1a,  // parameter id
      0x00, 0x02,  // length
      0x43, 0xe8,  // value
      // disable_active_migration
      0x00, 0x0c,  // parameter id
      0x00, 0x00,  // length
      // preferred_address
      0x00, 0x0d,  // parameter id
      0x00, 0x31,  // length
      0x41, 0x42, 0x43, 0x44,  // IPv4 address
      0x48, 0x84,  // IPv4 port
      0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,  // IPv6 address
      0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
      0x63, 0x36,  // IPv6 port
      0x08,        // connection ID length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBE, 0xEF,  // connection ID
      0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,  // stateless reset token
      0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
      // active_connection_id_limit
      0x00, 0x0e,  // parameter id
      0x00, 0x01,  // length
      0x34,  // value
      // initial_source_connection_id
      0x00, 0x0f,  // parameter id
      0x00, 0x08,  // length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23, 0x45,
      // retry_source_connection_id
      0x00, 0x10,  // parameter id
      0x00, 0x08,  // length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x98, 0x76,
      // google_connection_options
      0x31, 0x28,  // parameter id
      0x00, 0x0c,  // length
      'A', 'L', 'P', 'N',  // value
      'E', 'F', 'G', 0x00,
      'H', 'I', 'J', 0xff,
      // support_handshake_done
      0x31, 0x2A,  // parameter id
      0x00, 0x00,  // value
      // key_update_not_yet_supported
      0x31, 0x2B,  // parameter id
      0x00, 0x00,  // value
      // Google version extension
      0x47, 0x52,  // parameter id
      0x00, 0x0d,  // length
      0x01, 0x23, 0x45, 0x67,  // negotiated_version
      0x08,  // length of supported versions array
      0x01, 0x23, 0x45, 0x67,
      0x89, 0xab, 0xcd, 0xef,
  };
  const uint8_t kServerParams[] = {
      // original_destination_connection_id
      0x00,  // parameter id
      0x08,  // length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x37,
      // max_idle_timeout
      0x01,  // parameter id
      0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x02,  // parameter id
      0x10,  // length
      0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
      0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
      // max_udp_payload_size
      0x03,  // parameter id
      0x02,  // length
      0x63, 0x29,  // value
      // initial_max_data
      0x04,  // parameter id
      0x02,  // length
      0x40, 0x65,  // value
      // initial_max_stream_data_bidi_local
      0x05,  // parameter id
      0x02,  // length
      0x47, 0xD1,  // value
      // initial_max_stream_data_bidi_remote
      0x06,  // parameter id
      0x02,  // length
      0x47, 0xD2,  // value
      // initial_max_stream_data_uni
      0x07,  // parameter id
      0x02,  // length
      0x4B, 0xB8,  // value
      // initial_max_streams_bidi
      0x08,  // parameter id
      0x01,  // length
      0x15,  // value
      // initial_max_streams_uni
      0x09,  // parameter id
      0x01,  // length
      0x16,  // value
      // ack_delay_exponent
      0x0a,  // parameter id
      0x01,  // length
      0x0a,  // value
      // max_ack_delay
      0x0b,  // parameter id
      0x01,  // length
      0x33,  // value
      // min_ack_delay_us
      0x80, 0x00, 0xde, 0x1a,  // parameter id
      0x02,  // length
      0x43, 0xe8,  // value
      // disable_active_migration
      0x0c,  // parameter id
      0x00,  // length
      // preferred_address
      0x0d,  // parameter id
      0x31,  // length
      0x41, 0x42, 0x43, 0x44,  // IPv4 address
      0x48, 0x84,  // IPv4 port
      0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,  // IPv6 address
      0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
      0x63, 0x36,  // IPv6 port
      0x08,        // connection ID length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBE, 0xEF,  // connection ID
      0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,  // stateless reset token
      0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
      // active_connection_id_limit
      0x0e,  // parameter id
      0x01,  // length
      0x34,  // value
      // initial_source_connection_id
      0x0f,  // parameter id
      0x08,  // length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x23, 0x45,
      // retry_source_connection_id
      0x10,  // parameter id
      0x08,  // length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x98, 0x76,
      // google_connection_options
      0x71, 0x28,  // parameter id
      0x0c,  // length
      'A', 'L', 'P', 'N',  // value
      'E', 'F', 'G', 0x00,
      'H', 'I', 'J', 0xff,
      // support_handshake_done
      0x71, 0x2A,  // parameter id
      0x00,  // length
      // key_update_not_yet_supported
      0x71, 0x2B,  // parameter id
      0x00,  // length
      // Google version extension
      0x80, 0x00, 0x47, 0x52,  // parameter id
      0x0d,  // length
      0x01, 0x23, 0x45, 0x67,  // negotiated_version
      0x08,  // length of supported versions array
      0x01, 0x23, 0x45, 0x67,
      0x89, 0xab, 0xcd, 0xef,
  };
  // clang-format on
  const uint8_t* server_params =
      reinterpret_cast<const uint8_t*>(kServerParams);
  size_t server_params_length = QUICHE_ARRAYSIZE(kServerParams);
  if (!version_.HasVarIntTransportParams()) {
    server_params = reinterpret_cast<const uint8_t*>(kServerParamsOld);
    server_params_length = QUICHE_ARRAYSIZE(kServerParamsOld);
  }
  TransportParameters new_params;
  std::string error_details;
  ASSERT_TRUE(ParseTransportParameters(version_, Perspective::IS_SERVER,
                                       server_params, server_params_length,
                                       &new_params, &error_details))
      << error_details;
  EXPECT_TRUE(error_details.empty());
  EXPECT_EQ(Perspective::IS_SERVER, new_params.perspective);
  EXPECT_EQ(kFakeVersionLabel, new_params.version);
  EXPECT_EQ(2u, new_params.supported_versions.size());
  EXPECT_EQ(kFakeVersionLabel, new_params.supported_versions[0]);
  EXPECT_EQ(kFakeVersionLabel2, new_params.supported_versions[1]);
  ASSERT_TRUE(new_params.original_destination_connection_id.has_value());
  EXPECT_EQ(CreateFakeOriginalDestinationConnectionId(),
            new_params.original_destination_connection_id.value());
  EXPECT_EQ(kFakeIdleTimeoutMilliseconds,
            new_params.max_idle_timeout_ms.value());
  EXPECT_EQ(CreateStatelessResetTokenForTest(),
            new_params.stateless_reset_token);
  EXPECT_EQ(kMaxPacketSizeForTest, new_params.max_udp_payload_size.value());
  EXPECT_EQ(kFakeInitialMaxData, new_params.initial_max_data.value());
  EXPECT_EQ(kFakeInitialMaxStreamDataBidiLocal,
            new_params.initial_max_stream_data_bidi_local.value());
  EXPECT_EQ(kFakeInitialMaxStreamDataBidiRemote,
            new_params.initial_max_stream_data_bidi_remote.value());
  EXPECT_EQ(kFakeInitialMaxStreamDataUni,
            new_params.initial_max_stream_data_uni.value());
  EXPECT_EQ(kFakeInitialMaxStreamsBidi,
            new_params.initial_max_streams_bidi.value());
  EXPECT_EQ(kFakeInitialMaxStreamsUni,
            new_params.initial_max_streams_uni.value());
  EXPECT_EQ(kAckDelayExponentForTest, new_params.ack_delay_exponent.value());
  EXPECT_EQ(kMaxAckDelayForTest, new_params.max_ack_delay.value());
  EXPECT_EQ(kMinAckDelayUsForTest, new_params.min_ack_delay_us.value());
  EXPECT_EQ(kFakeDisableMigration, new_params.disable_active_migration);
  ASSERT_NE(nullptr, new_params.preferred_address.get());
  EXPECT_EQ(CreateFakeV4SocketAddress(),
            new_params.preferred_address->ipv4_socket_address);
  EXPECT_EQ(CreateFakeV6SocketAddress(),
            new_params.preferred_address->ipv6_socket_address);
  EXPECT_EQ(CreateFakePreferredConnectionId(),
            new_params.preferred_address->connection_id);
  EXPECT_EQ(CreateFakePreferredStatelessResetToken(),
            new_params.preferred_address->stateless_reset_token);
  EXPECT_EQ(kActiveConnectionIdLimitForTest,
            new_params.active_connection_id_limit.value());
  ASSERT_TRUE(new_params.initial_source_connection_id.has_value());
  EXPECT_EQ(CreateFakeInitialSourceConnectionId(),
            new_params.initial_source_connection_id.value());
  ASSERT_TRUE(new_params.retry_source_connection_id.has_value());
  EXPECT_EQ(CreateFakeRetrySourceConnectionId(),
            new_params.retry_source_connection_id.value());
  ASSERT_TRUE(new_params.google_connection_options.has_value());
  EXPECT_EQ(CreateFakeGoogleConnectionOptions(),
            new_params.google_connection_options.value());
  EXPECT_FALSE(new_params.user_agent_id.has_value());
  EXPECT_TRUE(new_params.support_handshake_done);
  EXPECT_TRUE(new_params.key_update_not_yet_supported);
}

TEST_P(TransportParametersTest, ParseServerParametersRepeated) {
  // clang-format off
  const uint8_t kServerParamsRepeatedOld[] = {
      0x00, 0x2c,  // length of parameters array that follows
      // original_destination_connection_id
      0x00, 0x00,  // parameter id
      0x00, 0x08,  // length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x37,
      // max_idle_timeout
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x00, 0x02,  // parameter id
      0x00, 0x10,  // length
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
      0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
      // max_idle_timeout (repeated)
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
  };
  const uint8_t kServerParamsRepeated[] = {
      // original_destination_connection_id
      0x00,  // parameter id
      0x08,  // length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x37,
      // max_idle_timeout
      0x01,  // parameter id
      0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x02,  // parameter id
      0x10,  // length
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
      0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
      // max_idle_timeout (repeated)
      0x01,  // parameter id
      0x02,  // length
      0x6e, 0xec,  // value
  };
  // clang-format on
  const uint8_t* server_params =
      reinterpret_cast<const uint8_t*>(kServerParamsRepeated);
  size_t server_params_length = QUICHE_ARRAYSIZE(kServerParamsRepeated);
  if (!version_.HasVarIntTransportParams()) {
    server_params = reinterpret_cast<const uint8_t*>(kServerParamsRepeatedOld);
    server_params_length = QUICHE_ARRAYSIZE(kServerParamsRepeatedOld);
  }
  TransportParameters out_params;
  std::string error_details;
  EXPECT_FALSE(ParseTransportParameters(version_, Perspective::IS_SERVER,
                                        server_params, server_params_length,
                                        &out_params, &error_details));
  EXPECT_EQ(error_details, "Received a second max_idle_timeout");
}

TEST_P(TransportParametersTest,
       ParseServerParametersEmptyOriginalConnectionId) {
  // clang-format off
  const uint8_t kServerParamsEmptyOriginalConnectionIdOld[] = {
      0x00, 0x1e,  // length of parameters array that follows
      // original_destination_connection_id
      0x00, 0x00,  // parameter id
      0x00, 0x00,  // length
      // max_idle_timeout
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x00, 0x02,  // parameter id
      0x00, 0x10,  // length
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
      0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
  };
  const uint8_t kServerParamsEmptyOriginalConnectionId[] = {
      // original_destination_connection_id
      0x00,  // parameter id
      0x00,  // length
      // max_idle_timeout
      0x01,  // parameter id
      0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x02,  // parameter id
      0x10,  // length
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
      0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
  };
  // clang-format on
  const uint8_t* server_params =
      reinterpret_cast<const uint8_t*>(kServerParamsEmptyOriginalConnectionId);
  size_t server_params_length =
      QUICHE_ARRAYSIZE(kServerParamsEmptyOriginalConnectionId);
  if (!version_.HasVarIntTransportParams()) {
    server_params = reinterpret_cast<const uint8_t*>(
        kServerParamsEmptyOriginalConnectionIdOld);
    server_params_length =
        QUICHE_ARRAYSIZE(kServerParamsEmptyOriginalConnectionIdOld);
  }
  TransportParameters out_params;
  std::string error_details;
  ASSERT_TRUE(ParseTransportParameters(version_, Perspective::IS_SERVER,
                                       server_params, server_params_length,
                                       &out_params, &error_details))
      << error_details;
  ASSERT_TRUE(out_params.original_destination_connection_id.has_value());
  EXPECT_EQ(out_params.original_destination_connection_id.value(),
            EmptyQuicConnectionId());
}

TEST_P(TransportParametersTest, VeryLongCustomParameter) {
  // Ensure we can handle a 70KB custom parameter on both send and receive.
  size_t custom_value_length = 70000;
  if (!version_.HasVarIntTransportParams()) {
    // These versions encode lengths as uint16 so they cannot send as much.
    custom_value_length = 65000;
  }
  std::string custom_value(custom_value_length, '?');
  TransportParameters orig_params;
  orig_params.perspective = Perspective::IS_CLIENT;
  orig_params.version = kFakeVersionLabel;
  orig_params.custom_parameters[kCustomParameter1] = custom_value;

  std::vector<uint8_t> serialized;
  ASSERT_TRUE(SerializeTransportParameters(version_, orig_params, &serialized));

  TransportParameters new_params;
  std::string error_details;
  ASSERT_TRUE(ParseTransportParameters(version_, Perspective::IS_CLIENT,
                                       serialized.data(), serialized.size(),
                                       &new_params, &error_details))
      << error_details;
  EXPECT_TRUE(error_details.empty());
  RemoveGreaseParameters(&new_params);
  EXPECT_EQ(new_params, orig_params);
}

class TransportParametersTicketSerializationTest : public QuicTest {
 protected:
  void SetUp() override {
    original_params_.perspective = Perspective::IS_SERVER;
    original_params_.version = kFakeVersionLabel;
    original_params_.supported_versions.push_back(kFakeVersionLabel);
    original_params_.supported_versions.push_back(kFakeVersionLabel2);
    original_params_.original_destination_connection_id =
        CreateFakeOriginalDestinationConnectionId();
    original_params_.max_idle_timeout_ms.set_value(
        kFakeIdleTimeoutMilliseconds);
    original_params_.stateless_reset_token = CreateStatelessResetTokenForTest();
    original_params_.max_udp_payload_size.set_value(kMaxPacketSizeForTest);
    original_params_.initial_max_data.set_value(kFakeInitialMaxData);
    original_params_.initial_max_stream_data_bidi_local.set_value(
        kFakeInitialMaxStreamDataBidiLocal);
    original_params_.initial_max_stream_data_bidi_remote.set_value(
        kFakeInitialMaxStreamDataBidiRemote);
    original_params_.initial_max_stream_data_uni.set_value(
        kFakeInitialMaxStreamDataUni);
    original_params_.initial_max_streams_bidi.set_value(
        kFakeInitialMaxStreamsBidi);
    original_params_.initial_max_streams_uni.set_value(
        kFakeInitialMaxStreamsUni);
    original_params_.ack_delay_exponent.set_value(kAckDelayExponentForTest);
    original_params_.max_ack_delay.set_value(kMaxAckDelayForTest);
    original_params_.min_ack_delay_us.set_value(kMinAckDelayUsForTest);
    original_params_.disable_active_migration = kFakeDisableMigration;
    original_params_.preferred_address = CreateFakePreferredAddress();
    original_params_.active_connection_id_limit.set_value(
        kActiveConnectionIdLimitForTest);
    original_params_.initial_source_connection_id =
        CreateFakeInitialSourceConnectionId();
    original_params_.retry_source_connection_id =
        CreateFakeRetrySourceConnectionId();
    original_params_.google_connection_options =
        CreateFakeGoogleConnectionOptions();

    ASSERT_TRUE(SerializeTransportParametersForTicket(
        original_params_, application_state_, &original_serialized_params_));
  }

  TransportParameters original_params_;
  std::vector<uint8_t> application_state_ = {0, 1};
  std::vector<uint8_t> original_serialized_params_;
};

TEST_F(TransportParametersTicketSerializationTest,
       StatelessResetTokenDoesntChangeOutput) {
  // Test that changing the stateless reset token doesn't change the ticket
  // serialization.
  TransportParameters new_params = original_params_;
  new_params.stateless_reset_token = CreateFakePreferredStatelessResetToken();
  EXPECT_NE(new_params, original_params_);

  std::vector<uint8_t> serialized;
  ASSERT_TRUE(SerializeTransportParametersForTicket(
      new_params, application_state_, &serialized));
  EXPECT_EQ(original_serialized_params_, serialized);
}

TEST_F(TransportParametersTicketSerializationTest,
       ConnectionIDDoesntChangeOutput) {
  // Changing original destination CID doesn't change serialization.
  TransportParameters new_params = original_params_;
  new_params.original_destination_connection_id = TestConnectionId(0xCAFE);
  EXPECT_NE(new_params, original_params_);

  std::vector<uint8_t> serialized;
  ASSERT_TRUE(SerializeTransportParametersForTicket(
      new_params, application_state_, &serialized));
  EXPECT_EQ(original_serialized_params_, serialized);
}

TEST_F(TransportParametersTicketSerializationTest, StreamLimitChangesOutput) {
  // Changing a stream limit does change the serialization.
  TransportParameters new_params = original_params_;
  new_params.initial_max_stream_data_bidi_local.set_value(
      kFakeInitialMaxStreamDataBidiLocal + 1);
  EXPECT_NE(new_params, original_params_);

  std::vector<uint8_t> serialized;
  ASSERT_TRUE(SerializeTransportParametersForTicket(
      new_params, application_state_, &serialized));
  EXPECT_NE(original_serialized_params_, serialized);
}

TEST_F(TransportParametersTicketSerializationTest,
       ApplicationStateChangesOutput) {
  // Changing the application state changes the serialization.
  std::vector<uint8_t> new_application_state = {0};
  EXPECT_NE(new_application_state, application_state_);

  std::vector<uint8_t> serialized;
  ASSERT_TRUE(SerializeTransportParametersForTicket(
      original_params_, new_application_state, &serialized));
  EXPECT_NE(original_serialized_params_, serialized);
}

}  // namespace test
}  // namespace quic

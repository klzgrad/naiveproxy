// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/transport_parameters.h"

#include <cstring>
#include <utility>

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

using testing::Pair;
using testing::UnorderedElementsAre;

const QuicVersionLabel kFakeVersionLabel = 0x01234567;
const QuicVersionLabel kFakeVersionLabel2 = 0x89ABCDEF;
const uint64_t kFakeIdleTimeoutMilliseconds = 12012;
const uint8_t kFakeStatelessResetTokenData[16] = {
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F};
const uint64_t kFakeMaxPacketSize = 9001;
const uint64_t kFakeInitialMaxData = 101;
const uint64_t kFakeInitialMaxStreamDataBidiLocal = 2001;
const uint64_t kFakeInitialMaxStreamDataBidiRemote = 2002;
const uint64_t kFakeInitialMaxStreamDataUni = 3000;
const uint64_t kFakeInitialMaxStreamsBidi = 21;
const uint64_t kFakeInitialMaxStreamsUni = 22;
const uint64_t kFakeAckDelayExponent = 10;
const uint64_t kFakeMaxAckDelay = 51;
const bool kFakeDisableMigration = true;
const uint64_t kFakeActiveConnectionIdLimit = 52;
const uint8_t kFakePreferredStatelessResetTokenData[16] = {
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F};

const auto kCustomParameter1 =
    static_cast<TransportParameters::TransportParameterId>(0xffcd);
const char* kCustomParameter1Value = "foo";
const auto kCustomParameter2 =
    static_cast<TransportParameters::TransportParameterId>(0xff34);
const char* kCustomParameter2Value = "bar";

QuicConnectionId CreateFakeOriginalConnectionId() {
  return TestConnectionId(0x1337);
}

QuicConnectionId CreateFakePreferredConnectionId() {
  return TestConnectionId(0xBEEF);
}

std::vector<uint8_t> CreateFakeStatelessResetToken() {
  return std::vector<uint8_t>(
      kFakeStatelessResetTokenData,
      kFakeStatelessResetTokenData + sizeof(kFakeStatelessResetTokenData));
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

std::vector<ParsedQuicVersion> AllSupportedTlsVersions() {
  std::vector<ParsedQuicVersion> tls_versions;
  for (const ParsedQuicVersion& version : AllSupportedVersions()) {
    if (version.handshake_protocol == PROTOCOL_TLS1_3) {
      tls_versions.push_back(version);
    }
  }
  return tls_versions;
}

}  // namespace

class TransportParametersTest : public QuicTestWithParam<ParsedQuicVersion> {
 protected:
  TransportParametersTest() : version_(GetParam()) {}

  ParsedQuicVersion version_;
};

INSTANTIATE_TEST_SUITE_P(TransportParametersTests,
                         TransportParametersTest,
                         ::testing::ValuesIn(AllSupportedTlsVersions()),
                         ::testing::PrintToStringParamName());

TEST_P(TransportParametersTest, RoundTripClient) {
  TransportParameters orig_params;
  orig_params.perspective = Perspective::IS_CLIENT;
  orig_params.version = kFakeVersionLabel;
  orig_params.idle_timeout_milliseconds.set_value(kFakeIdleTimeoutMilliseconds);
  orig_params.max_packet_size.set_value(kFakeMaxPacketSize);
  orig_params.initial_max_data.set_value(kFakeInitialMaxData);
  orig_params.initial_max_stream_data_bidi_local.set_value(
      kFakeInitialMaxStreamDataBidiLocal);
  orig_params.initial_max_stream_data_bidi_remote.set_value(
      kFakeInitialMaxStreamDataBidiRemote);
  orig_params.initial_max_stream_data_uni.set_value(
      kFakeInitialMaxStreamDataUni);
  orig_params.initial_max_streams_bidi.set_value(kFakeInitialMaxStreamsBidi);
  orig_params.initial_max_streams_uni.set_value(kFakeInitialMaxStreamsUni);
  orig_params.ack_delay_exponent.set_value(kFakeAckDelayExponent);
  orig_params.max_ack_delay.set_value(kFakeMaxAckDelay);
  orig_params.disable_migration = kFakeDisableMigration;
  orig_params.active_connection_id_limit.set_value(
      kFakeActiveConnectionIdLimit);
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
  EXPECT_EQ(Perspective::IS_CLIENT, new_params.perspective);
  EXPECT_EQ(kFakeVersionLabel, new_params.version);
  EXPECT_TRUE(new_params.supported_versions.empty());
  EXPECT_EQ(EmptyQuicConnectionId(), new_params.original_connection_id);
  EXPECT_EQ(kFakeIdleTimeoutMilliseconds,
            new_params.idle_timeout_milliseconds.value());
  EXPECT_TRUE(new_params.stateless_reset_token.empty());
  EXPECT_EQ(kFakeMaxPacketSize, new_params.max_packet_size.value());
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
  EXPECT_EQ(kFakeAckDelayExponent, new_params.ack_delay_exponent.value());
  EXPECT_EQ(kFakeMaxAckDelay, new_params.max_ack_delay.value());
  EXPECT_EQ(kFakeDisableMigration, new_params.disable_migration);
  EXPECT_EQ(kFakeActiveConnectionIdLimit,
            new_params.active_connection_id_limit.value());
  EXPECT_THAT(
      new_params.custom_parameters,
      UnorderedElementsAre(Pair(kCustomParameter1, kCustomParameter1Value),
                           Pair(kCustomParameter2, kCustomParameter2Value)));
}

TEST_P(TransportParametersTest, RoundTripServer) {
  TransportParameters orig_params;
  orig_params.perspective = Perspective::IS_SERVER;
  orig_params.version = kFakeVersionLabel;
  orig_params.supported_versions.push_back(kFakeVersionLabel);
  orig_params.supported_versions.push_back(kFakeVersionLabel2);
  orig_params.original_connection_id = CreateFakeOriginalConnectionId();
  orig_params.idle_timeout_milliseconds.set_value(kFakeIdleTimeoutMilliseconds);
  orig_params.stateless_reset_token = CreateFakeStatelessResetToken();
  orig_params.max_packet_size.set_value(kFakeMaxPacketSize);
  orig_params.initial_max_data.set_value(kFakeInitialMaxData);
  orig_params.initial_max_stream_data_bidi_local.set_value(
      kFakeInitialMaxStreamDataBidiLocal);
  orig_params.initial_max_stream_data_bidi_remote.set_value(
      kFakeInitialMaxStreamDataBidiRemote);
  orig_params.initial_max_stream_data_uni.set_value(
      kFakeInitialMaxStreamDataUni);
  orig_params.initial_max_streams_bidi.set_value(kFakeInitialMaxStreamsBidi);
  orig_params.initial_max_streams_uni.set_value(kFakeInitialMaxStreamsUni);
  orig_params.ack_delay_exponent.set_value(kFakeAckDelayExponent);
  orig_params.max_ack_delay.set_value(kFakeMaxAckDelay);
  orig_params.disable_migration = kFakeDisableMigration;
  orig_params.preferred_address = CreateFakePreferredAddress();
  orig_params.active_connection_id_limit.set_value(
      kFakeActiveConnectionIdLimit);

  std::vector<uint8_t> serialized;
  ASSERT_TRUE(SerializeTransportParameters(version_, orig_params, &serialized));

  TransportParameters new_params;
  std::string error_details;
  ASSERT_TRUE(ParseTransportParameters(version_, Perspective::IS_SERVER,
                                       serialized.data(), serialized.size(),
                                       &new_params, &error_details))
      << error_details;
  EXPECT_TRUE(error_details.empty());
  EXPECT_EQ(Perspective::IS_SERVER, new_params.perspective);
  EXPECT_EQ(kFakeVersionLabel, new_params.version);
  EXPECT_EQ(2u, new_params.supported_versions.size());
  EXPECT_EQ(kFakeVersionLabel, new_params.supported_versions[0]);
  EXPECT_EQ(kFakeVersionLabel2, new_params.supported_versions[1]);
  EXPECT_EQ(CreateFakeOriginalConnectionId(),
            new_params.original_connection_id);
  EXPECT_EQ(kFakeIdleTimeoutMilliseconds,
            new_params.idle_timeout_milliseconds.value());
  EXPECT_EQ(CreateFakeStatelessResetToken(), new_params.stateless_reset_token);
  EXPECT_EQ(kFakeMaxPacketSize, new_params.max_packet_size.value());
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
  EXPECT_EQ(kFakeAckDelayExponent, new_params.ack_delay_exponent.value());
  EXPECT_EQ(kFakeMaxAckDelay, new_params.max_ack_delay.value());
  EXPECT_EQ(kFakeDisableMigration, new_params.disable_migration);
  ASSERT_NE(nullptr, new_params.preferred_address.get());
  EXPECT_EQ(CreateFakeV4SocketAddress(),
            new_params.preferred_address->ipv4_socket_address);
  EXPECT_EQ(CreateFakeV6SocketAddress(),
            new_params.preferred_address->ipv6_socket_address);
  EXPECT_EQ(CreateFakePreferredConnectionId(),
            new_params.preferred_address->connection_id);
  EXPECT_EQ(CreateFakePreferredStatelessResetToken(),
            new_params.preferred_address->stateless_reset_token);
  EXPECT_EQ(kFakeActiveConnectionIdLimit,
            new_params.active_connection_id_limit.value());
  EXPECT_EQ(0u, new_params.custom_parameters.size());
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
    params.idle_timeout_milliseconds.set_value(kFakeIdleTimeoutMilliseconds);
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.idle_timeout_milliseconds.set_value(601000);
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
  }
  {
    TransportParameters params;
    std::string error_details;
    params.perspective = Perspective::IS_CLIENT;
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.max_packet_size.set_value(1200);
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.max_packet_size.set_value(65535);
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.max_packet_size.set_value(9999999);
    EXPECT_TRUE(params.AreValid(&error_details));
    EXPECT_TRUE(error_details.empty());
    params.max_packet_size.set_value(0);
    error_details = "";
    EXPECT_FALSE(params.AreValid(&error_details));
    EXPECT_EQ(
        error_details,
        "Invalid transport parameters [Client max_packet_size 0 (Invalid)]");
    params.max_packet_size.set_value(1199);
    error_details = "";
    EXPECT_FALSE(params.AreValid(&error_details));
    EXPECT_EQ(
        error_details,
        "Invalid transport parameters [Client max_packet_size 1199 (Invalid)]");
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
}

TEST_P(TransportParametersTest, NoClientParamsWithStatelessResetToken) {
  TransportParameters orig_params;
  orig_params.perspective = Perspective::IS_CLIENT;
  orig_params.version = kFakeVersionLabel;
  orig_params.idle_timeout_milliseconds.set_value(kFakeIdleTimeoutMilliseconds);
  orig_params.stateless_reset_token = CreateFakeStatelessResetToken();
  orig_params.max_packet_size.set_value(kFakeMaxPacketSize);

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
      0x00, 0x49,              // length of the parameters array that follows
      // idle_timeout
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
      // max_packet_size
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
      // disable_migration
      0x00, 0x0c,  // parameter id
      0x00, 0x00,  // length
      // active_connection_id_limit
      0x00, 0x0e,  // parameter id
      0x00, 0x01,  // length
      0x34,  // value
      // Google version extension
      0x47, 0x52,  // parameter id
      0x00, 0x04,  // length
      0x01, 0x23, 0x45, 0x67,  // initial version
  };
  const uint8_t kClientParams[] = {
      // idle_timeout
      0x01,  // parameter id
      0x02,  // length
      0x6e, 0xec,  // value
      // max_packet_size
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
      // disable_migration
      0x0c,  // parameter id
      0x00,  // length
      // active_connection_id_limit
      0x0e,  // parameter id
      0x01,  // length
      0x34,  // value
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
  EXPECT_EQ(EmptyQuicConnectionId(), new_params.original_connection_id);
  EXPECT_EQ(kFakeIdleTimeoutMilliseconds,
            new_params.idle_timeout_milliseconds.value());
  EXPECT_TRUE(new_params.stateless_reset_token.empty());
  EXPECT_EQ(kFakeMaxPacketSize, new_params.max_packet_size.value());
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
  EXPECT_EQ(kFakeAckDelayExponent, new_params.ack_delay_exponent.value());
  EXPECT_EQ(kFakeMaxAckDelay, new_params.max_ack_delay.value());
  EXPECT_EQ(kFakeDisableMigration, new_params.disable_migration);
  EXPECT_EQ(kFakeActiveConnectionIdLimit,
            new_params.active_connection_id_limit.value());
}

TEST_P(TransportParametersTest,
       ParseClientParamsFailsWithFullStatelessResetToken) {
  // clang-format off
  const uint8_t kClientParamsWithFullTokenOld[] = {
      0x00, 0x26,  // length parameters array that follows
      // idle_timeout
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x00, 0x02,  // parameter id
      0x00, 0x10,  // length
      0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
      0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
      // max_packet_size
      0x00, 0x03,  // parameter id
      0x00, 0x02,  // length
      0x63, 0x29,  // value
      // initial_max_data
      0x00, 0x04,  // parameter id
      0x00, 0x02,  // length
      0x40, 0x65,  // value
  };
  const uint8_t kClientParamsWithFullToken[] = {
      // idle_timeout
      0x01,  // parameter id
      0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x02,  // parameter id
      0x10,  // length
      0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
      0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
      // max_packet_size
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
      // idle_timeout
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x00, 0x02,  // parameter id
      0x00, 0x00,
      // max_packet_size
      0x00, 0x03,  // parameter id
      0x00, 0x02,  // length
      0x63, 0x29,  // value
      // initial_max_data
      0x00, 0x04,  // parameter id
      0x00, 0x02,  // length
      0x40, 0x65,  // value
  };
  const uint8_t kClientParamsWithEmptyToken[] = {
      // idle_timeout
      0x01,  // parameter id
      0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x02,  // parameter id
      0x00,  // length
      // max_packet_size
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
            "Received stateless reset token of invalid length 0");
}

TEST_P(TransportParametersTest, ParseClientParametersRepeated) {
  // clang-format off
  const uint8_t kClientParamsRepeatedOld[] = {
      0x00, 0x12,  // length parameters array that follows
      // idle_timeout
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
      // max_packet_size
      0x00, 0x03,  // parameter id
      0x00, 0x02,  // length
      0x63, 0x29,  // value
      // idle_timeout (repeated)
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
  };
  const uint8_t kClientParamsRepeated[] = {
      // idle_timeout
      0x01,  // parameter id
      0x02,  // length
      0x6e, 0xec,  // value
      // max_packet_size
      0x03,  // parameter id
      0x02,  // length
      0x63, 0x29,  // value
      // idle_timeout (repeated)
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
  EXPECT_EQ(error_details, "Received a second idle_timeout");
}

TEST_P(TransportParametersTest, ParseServerParams) {
  // clang-format off
  const uint8_t kServerParamsOld[] = {
      0x00, 0xa7,  // length of parameters array that follows
      // original_connection_id
      0x00, 0x00,  // parameter id
      0x00, 0x08,  // length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x37,
      // idle_timeout
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x00, 0x02,  // parameter id
      0x00, 0x10,  // length
      0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
      0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
      // max_packet_size
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
      // disable_migration
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
      // Google version extension
      0x47, 0x52,  // parameter id
      0x00, 0x0d,  // length
      0x01, 0x23, 0x45, 0x67,  // negotiated_version
      0x08,  // length of supported versions array
      0x01, 0x23, 0x45, 0x67,
      0x89, 0xab, 0xcd, 0xef,
  };
  const uint8_t kServerParams[] = {
      // original_connection_id
      0x00,  // parameter id
      0x08,  // length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x37,
      // idle_timeout
      0x01,  // parameter id
      0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x02,  // parameter id
      0x10,  // length
      0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
      0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
      // max_packet_size
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
      // disable_migration
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
  EXPECT_EQ(CreateFakeOriginalConnectionId(),
            new_params.original_connection_id);
  EXPECT_EQ(kFakeIdleTimeoutMilliseconds,
            new_params.idle_timeout_milliseconds.value());
  EXPECT_EQ(CreateFakeStatelessResetToken(), new_params.stateless_reset_token);
  EXPECT_EQ(kFakeMaxPacketSize, new_params.max_packet_size.value());
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
  EXPECT_EQ(kFakeAckDelayExponent, new_params.ack_delay_exponent.value());
  EXPECT_EQ(kFakeMaxAckDelay, new_params.max_ack_delay.value());
  EXPECT_EQ(kFakeDisableMigration, new_params.disable_migration);
  ASSERT_NE(nullptr, new_params.preferred_address.get());
  EXPECT_EQ(CreateFakeV4SocketAddress(),
            new_params.preferred_address->ipv4_socket_address);
  EXPECT_EQ(CreateFakeV6SocketAddress(),
            new_params.preferred_address->ipv6_socket_address);
  EXPECT_EQ(CreateFakePreferredConnectionId(),
            new_params.preferred_address->connection_id);
  EXPECT_EQ(CreateFakePreferredStatelessResetToken(),
            new_params.preferred_address->stateless_reset_token);
  EXPECT_EQ(kFakeActiveConnectionIdLimit,
            new_params.active_connection_id_limit.value());
}

TEST_P(TransportParametersTest, ParseServerParametersRepeated) {
  // clang-format off
  const uint8_t kServerParamsRepeatedOld[] = {
      0x00, 0x2c,  // length of parameters array that follows
      // original_connection_id
      0x00, 0x00,  // parameter id
      0x00, 0x08,  // length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x37,
      // idle_timeout
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x00, 0x02,  // parameter id
      0x00, 0x10,  // length
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
      0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
      // idle_timeout (repeated)
      0x00, 0x01,  // parameter id
      0x00, 0x02,  // length
      0x6e, 0xec,  // value
  };
  const uint8_t kServerParamsRepeated[] = {
      // original_connection_id
      0x00,  // parameter id
      0x08,  // length
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x37,
      // idle_timeout
      0x01,  // parameter id
      0x02,  // length
      0x6e, 0xec,  // value
      // stateless_reset_token
      0x02,  // parameter id
      0x10,  // length
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
      0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
      // idle_timeout (repeated)
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
  EXPECT_EQ(error_details, "Received a second idle_timeout");
}

TEST_P(TransportParametersTest, CryptoHandshakeMessageRoundtrip) {
  TransportParameters orig_params;
  orig_params.perspective = Perspective::IS_CLIENT;
  orig_params.version = kFakeVersionLabel;
  orig_params.max_packet_size.set_value(kFakeMaxPacketSize);

  orig_params.google_quic_params = std::make_unique<CryptoHandshakeMessage>();
  const std::string kTestString = "test string";
  orig_params.google_quic_params->SetStringPiece(42, kTestString);
  const uint32_t kTestValue = 12;
  orig_params.google_quic_params->SetValue(1337, kTestValue);

  std::vector<uint8_t> serialized;
  ASSERT_TRUE(SerializeTransportParameters(version_, orig_params, &serialized));

  TransportParameters new_params;
  std::string error_details;
  ASSERT_TRUE(ParseTransportParameters(version_, Perspective::IS_CLIENT,
                                       serialized.data(), serialized.size(),
                                       &new_params, &error_details))
      << error_details;
  EXPECT_TRUE(error_details.empty());
  ASSERT_NE(new_params.google_quic_params.get(), nullptr);
  EXPECT_EQ(new_params.google_quic_params->tag(),
            orig_params.google_quic_params->tag());
  quiche::QuicheStringPiece test_string;
  EXPECT_TRUE(new_params.google_quic_params->GetStringPiece(42, &test_string));
  EXPECT_EQ(test_string, kTestString);
  uint32_t test_value;
  EXPECT_THAT(new_params.google_quic_params->GetUint32(1337, &test_value),
              IsQuicNoError());
  EXPECT_EQ(test_value, kTestValue);
  EXPECT_EQ(kFakeVersionLabel, new_params.version);
  EXPECT_EQ(kFakeMaxPacketSize, new_params.max_packet_size.value());
}

}  // namespace test
}  // namespace quic

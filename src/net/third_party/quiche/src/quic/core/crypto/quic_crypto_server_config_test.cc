// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_server_config.h"

#include <stdarg.h>

#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/cert_compressor.h"
#include "net/third_party/quiche/src/quic/core/crypto/chacha20_poly1305_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake_message.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_secret_boxer.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/proto/crypto_server_config_proto.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_crypto_server_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {
namespace test {
using ::testing::Not;

// NOTE: This matcher depends on the wire format of serialzied protocol buffers,
// which may change in the future.
// Switch to ::testing::EqualsProto once it is available in Chromium.
MATCHER_P(SerializedProtoEquals, message, "") {
  std::string expected_serialized, actual_serialized;
  message.SerializeToString(&expected_serialized);
  arg.SerializeToString(&actual_serialized);
  return expected_serialized == actual_serialized;
}

class QuicCryptoServerConfigTest : public QuicTest {};

TEST_F(QuicCryptoServerConfigTest, ServerConfig) {
  QuicRandom* rand = QuicRandom::GetInstance();
  QuicCryptoServerConfig server(QuicCryptoServerConfig::TESTING, rand,
                                crypto_test_utils::ProofSourceForTesting(),
                                KeyExchangeSource::Default());
  MockClock clock;

  std::unique_ptr<CryptoHandshakeMessage> message(server.AddDefaultConfig(
      rand, &clock, QuicCryptoServerConfig::ConfigOptions()));

  // The default configuration should have AES-GCM and at least one ChaCha20
  // cipher.
  QuicTagVector aead;
  ASSERT_THAT(message->GetTaglist(kAEAD, &aead), IsQuicNoError());
  EXPECT_THAT(aead, ::testing::Contains(kAESG));
  EXPECT_LE(1u, aead.size());
}

TEST_F(QuicCryptoServerConfigTest, CompressCerts) {
  QuicCompressedCertsCache compressed_certs_cache(
      QuicCompressedCertsCache::kQuicCompressedCertsCacheSize);

  QuicRandom* rand = QuicRandom::GetInstance();
  QuicCryptoServerConfig server(QuicCryptoServerConfig::TESTING, rand,
                                crypto_test_utils::ProofSourceForTesting(),
                                KeyExchangeSource::Default());
  QuicCryptoServerConfigPeer peer(&server);

  std::vector<std::string> certs = {"testcert"};
  QuicReferenceCountedPointer<ProofSource::Chain> chain(
      new ProofSource::Chain(certs));

  std::string compressed = QuicCryptoServerConfigPeer::CompressChain(
      &compressed_certs_cache, chain, "", "", nullptr);

  EXPECT_EQ(compressed_certs_cache.Size(), 1u);
}

TEST_F(QuicCryptoServerConfigTest, CompressSameCertsTwice) {
  QuicCompressedCertsCache compressed_certs_cache(
      QuicCompressedCertsCache::kQuicCompressedCertsCacheSize);

  QuicRandom* rand = QuicRandom::GetInstance();
  QuicCryptoServerConfig server(QuicCryptoServerConfig::TESTING, rand,
                                crypto_test_utils::ProofSourceForTesting(),
                                KeyExchangeSource::Default());
  QuicCryptoServerConfigPeer peer(&server);

  // Compress the certs for the first time.
  std::vector<std::string> certs = {"testcert"};
  QuicReferenceCountedPointer<ProofSource::Chain> chain(
      new ProofSource::Chain(certs));
  std::string common_certs = "";
  std::string cached_certs = "";

  std::string compressed = QuicCryptoServerConfigPeer::CompressChain(
      &compressed_certs_cache, chain, common_certs, cached_certs, nullptr);
  EXPECT_EQ(compressed_certs_cache.Size(), 1u);

  // Compress the same certs, should use cache if available.
  std::string compressed2 = QuicCryptoServerConfigPeer::CompressChain(
      &compressed_certs_cache, chain, common_certs, cached_certs, nullptr);
  EXPECT_EQ(compressed, compressed2);
  EXPECT_EQ(compressed_certs_cache.Size(), 1u);
}

TEST_F(QuicCryptoServerConfigTest, CompressDifferentCerts) {
  // This test compresses a set of similar but not identical certs. Cache if
  // used should return cache miss and add all the compressed certs.
  QuicCompressedCertsCache compressed_certs_cache(
      QuicCompressedCertsCache::kQuicCompressedCertsCacheSize);

  QuicRandom* rand = QuicRandom::GetInstance();
  QuicCryptoServerConfig server(QuicCryptoServerConfig::TESTING, rand,
                                crypto_test_utils::ProofSourceForTesting(),
                                KeyExchangeSource::Default());
  QuicCryptoServerConfigPeer peer(&server);

  std::vector<std::string> certs = {"testcert"};
  QuicReferenceCountedPointer<ProofSource::Chain> chain(
      new ProofSource::Chain(certs));
  std::string common_certs = "";
  std::string cached_certs = "";

  std::string compressed = QuicCryptoServerConfigPeer::CompressChain(
      &compressed_certs_cache, chain, common_certs, cached_certs, nullptr);
  EXPECT_EQ(compressed_certs_cache.Size(), 1u);

  // Compress a similar certs which only differs in the chain.
  QuicReferenceCountedPointer<ProofSource::Chain> chain2(
      new ProofSource::Chain(certs));

  std::string compressed2 = QuicCryptoServerConfigPeer::CompressChain(
      &compressed_certs_cache, chain2, common_certs, cached_certs, nullptr);
  EXPECT_EQ(compressed_certs_cache.Size(), 2u);

  // Compress a similar certs which only differs in common certs field.
  static const uint64_t set_hash = 42;
  std::unique_ptr<CommonCertSets> common_sets(
      crypto_test_utils::MockCommonCertSets(certs[0], set_hash, 1));
  quiche::QuicheStringPiece different_common_certs(
      reinterpret_cast<const char*>(&set_hash), sizeof(set_hash));
  std::string compressed3 = QuicCryptoServerConfigPeer::CompressChain(
      &compressed_certs_cache, chain, std::string(different_common_certs),
      cached_certs, common_sets.get());
  EXPECT_EQ(compressed_certs_cache.Size(), 3u);
}

class SourceAddressTokenTest : public QuicTest {
 public:
  SourceAddressTokenTest()
      : ip4_(QuicIpAddress::Loopback4()),
        ip4_dual_(ip4_.DualStacked()),
        ip6_(QuicIpAddress::Loopback6()),
        original_time_(QuicWallTime::Zero()),
        rand_(QuicRandom::GetInstance()),
        server_(QuicCryptoServerConfig::TESTING,
                rand_,
                crypto_test_utils::ProofSourceForTesting(),
                KeyExchangeSource::Default()),
        peer_(&server_) {
    // Advance the clock to some non-zero time.
    clock_.AdvanceTime(QuicTime::Delta::FromSeconds(1000000));
    original_time_ = clock_.WallNow();

    primary_config_ = server_.AddDefaultConfig(
        rand_, &clock_, QuicCryptoServerConfig::ConfigOptions());
  }

  std::string NewSourceAddressToken(std::string config_id,
                                    const QuicIpAddress& ip) {
    return NewSourceAddressToken(config_id, ip, nullptr);
  }

  std::string NewSourceAddressToken(
      std::string config_id,
      const QuicIpAddress& ip,
      const SourceAddressTokens& previous_tokens) {
    return peer_.NewSourceAddressToken(config_id, previous_tokens, ip, rand_,
                                       clock_.WallNow(), nullptr);
  }

  std::string NewSourceAddressToken(
      std::string config_id,
      const QuicIpAddress& ip,
      CachedNetworkParameters* cached_network_params) {
    SourceAddressTokens previous_tokens;
    return peer_.NewSourceAddressToken(config_id, previous_tokens, ip, rand_,
                                       clock_.WallNow(), cached_network_params);
  }

  HandshakeFailureReason ValidateSourceAddressTokens(
      std::string config_id,
      quiche::QuicheStringPiece srct,
      const QuicIpAddress& ip) {
    return ValidateSourceAddressTokens(config_id, srct, ip, nullptr);
  }

  HandshakeFailureReason ValidateSourceAddressTokens(
      std::string config_id,
      quiche::QuicheStringPiece srct,
      const QuicIpAddress& ip,
      CachedNetworkParameters* cached_network_params) {
    return peer_.ValidateSourceAddressTokens(
        config_id, srct, ip, clock_.WallNow(), cached_network_params);
  }

  const std::string kPrimary = "<primary>";
  const std::string kOverride = "Config with custom source address token key";

  QuicIpAddress ip4_;
  QuicIpAddress ip4_dual_;
  QuicIpAddress ip6_;

  MockClock clock_;
  QuicWallTime original_time_;
  QuicRandom* rand_ = QuicRandom::GetInstance();
  QuicCryptoServerConfig server_;
  QuicCryptoServerConfigPeer peer_;
  // Stores the primary config.
  std::unique_ptr<CryptoHandshakeMessage> primary_config_;
  std::unique_ptr<QuicServerConfigProtobuf> override_config_protobuf_;
};

// Test basic behavior of source address tokens including being specific
// to a single IP address and server config.
TEST_F(SourceAddressTokenTest, SourceAddressToken) {
  // Primary config generates configs that validate successfully.
  const std::string token4 = NewSourceAddressToken(kPrimary, ip4_);
  const std::string token4d = NewSourceAddressToken(kPrimary, ip4_dual_);
  const std::string token6 = NewSourceAddressToken(kPrimary, ip6_);
  EXPECT_EQ(HANDSHAKE_OK, ValidateSourceAddressTokens(kPrimary, token4, ip4_));
  ASSERT_EQ(HANDSHAKE_OK,
            ValidateSourceAddressTokens(kPrimary, token4, ip4_dual_));
  ASSERT_EQ(SOURCE_ADDRESS_TOKEN_DIFFERENT_IP_ADDRESS_FAILURE,
            ValidateSourceAddressTokens(kPrimary, token4, ip6_));
  ASSERT_EQ(HANDSHAKE_OK, ValidateSourceAddressTokens(kPrimary, token4d, ip4_));
  ASSERT_EQ(HANDSHAKE_OK,
            ValidateSourceAddressTokens(kPrimary, token4d, ip4_dual_));
  ASSERT_EQ(SOURCE_ADDRESS_TOKEN_DIFFERENT_IP_ADDRESS_FAILURE,
            ValidateSourceAddressTokens(kPrimary, token4d, ip6_));
  ASSERT_EQ(HANDSHAKE_OK, ValidateSourceAddressTokens(kPrimary, token6, ip6_));
}

TEST_F(SourceAddressTokenTest, SourceAddressTokenExpiration) {
  const std::string token = NewSourceAddressToken(kPrimary, ip4_);

  // Validation fails if the token is from the future.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(-3600 * 2));
  ASSERT_EQ(SOURCE_ADDRESS_TOKEN_CLOCK_SKEW_FAILURE,
            ValidateSourceAddressTokens(kPrimary, token, ip4_));

  // Validation fails after tokens expire.
  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(86400 * 7));
  ASSERT_EQ(SOURCE_ADDRESS_TOKEN_EXPIRED_FAILURE,
            ValidateSourceAddressTokens(kPrimary, token, ip4_));
}

TEST_F(SourceAddressTokenTest, SourceAddressTokenWithNetworkParams) {
  // Make sure that if the source address token contains CachedNetworkParameters
  // that this gets written to ValidateSourceAddressToken output argument.
  CachedNetworkParameters cached_network_params_input;
  cached_network_params_input.set_bandwidth_estimate_bytes_per_second(1234);
  const std::string token4_with_cached_network_params =
      NewSourceAddressToken(kPrimary, ip4_, &cached_network_params_input);

  CachedNetworkParameters cached_network_params_output;
  EXPECT_THAT(cached_network_params_output,
              Not(SerializedProtoEquals(cached_network_params_input)));
  ValidateSourceAddressTokens(kPrimary, token4_with_cached_network_params, ip4_,
                              &cached_network_params_output);
  EXPECT_THAT(cached_network_params_output,
              SerializedProtoEquals(cached_network_params_input));
}

// Test the ability for a source address token to be valid for multiple
// addresses.
TEST_F(SourceAddressTokenTest, SourceAddressTokenMultipleAddresses) {
  QuicWallTime now = clock_.WallNow();

  // Now create a token which is usable for both addresses.
  SourceAddressToken previous_token;
  previous_token.set_ip(ip6_.DualStacked().ToPackedString());
  previous_token.set_timestamp(now.ToUNIXSeconds());
  SourceAddressTokens previous_tokens;
  (*previous_tokens.add_tokens()) = previous_token;
  const std::string token4or6 =
      NewSourceAddressToken(kPrimary, ip4_, previous_tokens);

  EXPECT_EQ(HANDSHAKE_OK,
            ValidateSourceAddressTokens(kPrimary, token4or6, ip4_));
  ASSERT_EQ(HANDSHAKE_OK,
            ValidateSourceAddressTokens(kPrimary, token4or6, ip6_));
}

class CryptoServerConfigsTest : public QuicTest {
 public:
  CryptoServerConfigsTest()
      : rand_(QuicRandom::GetInstance()),
        config_(QuicCryptoServerConfig::TESTING,
                rand_,
                crypto_test_utils::ProofSourceForTesting(),
                KeyExchangeSource::Default()),
        test_peer_(&config_) {}

  void SetUp() override {
    clock_.AdvanceTime(QuicTime::Delta::FromSeconds(1000));
  }

  // SetConfigs constructs suitable config protobufs and calls SetConfigs on
  // |config_|.
  // Each struct in the input vector contains 3 elements.
  // The first is the server config ID of a Config. The second is
  // the |primary_time| of that Config, given in epoch seconds. (Although note
  // that, in these tests, time is set to 1000 seconds since the epoch.).
  // The third is the priority.
  //
  // For example:
  //   SetConfigs(std::vector<ServerConfigIDWithTimeAndPriority>());  // calls
  //   |config_.SetConfigs| with no protobufs.
  //
  //   // Calls |config_.SetConfigs| with two protobufs: one for a Config with
  //   // a |primary_time| of 900 and priority 1, and another with
  //   // a |primary_time| of 1000 and priority 2.

  //   CheckConfigs(
  //     {{"id1", 900,  1},
  //      {"id2", 1000, 2}});
  //
  // If the server config id starts with "INVALID" then the generated protobuf
  // will be invalid.
  struct ServerConfigIDWithTimeAndPriority {
    ServerConfigID server_config_id;
    int primary_time;
    int priority;
  };
  void SetConfigs(std::vector<ServerConfigIDWithTimeAndPriority> configs) {
    const char kOrbit[] = "12345678";

    bool has_invalid = false;

    std::vector<QuicServerConfigProtobuf> protobufs;
    for (const auto& config : configs) {
      const ServerConfigID& server_config_id = config.server_config_id;
      const int primary_time = config.primary_time;
      const int priority = config.priority;

      QuicCryptoServerConfig::ConfigOptions options;
      options.id = server_config_id;
      options.orbit = kOrbit;
      QuicServerConfigProtobuf protobuf =
          QuicCryptoServerConfig::GenerateConfig(rand_, &clock_, options);
      protobuf.set_primary_time(primary_time);
      protobuf.set_priority(priority);
      if (quiche::QuicheTextUtils::StartsWith(std::string(server_config_id),
                                              "INVALID")) {
        protobuf.clear_key();
        has_invalid = true;
      }
      protobufs.push_back(std::move(protobuf));
    }

    ASSERT_EQ(!has_invalid && !configs.empty(),
              config_.SetConfigs(protobufs, /* fallback_protobuf = */ nullptr,
                                 clock_.WallNow()));
  }

 protected:
  QuicRandom* const rand_;
  MockClock clock_;
  QuicCryptoServerConfig config_;
  QuicCryptoServerConfigPeer test_peer_;
};

TEST_F(CryptoServerConfigsTest, NoConfigs) {
  test_peer_.CheckConfigs(std::vector<std::pair<std::string, bool>>());
}

TEST_F(CryptoServerConfigsTest, MakePrimaryFirst) {
  // Make sure that "b" is primary even though "a" comes first.
  SetConfigs({{"a", 1100, 1}, {"b", 900, 1}});
  test_peer_.CheckConfigs({{"a", false}, {"b", true}});
}

TEST_F(CryptoServerConfigsTest, MakePrimarySecond) {
  // Make sure that a remains primary after b is added.
  SetConfigs({{"a", 900, 1}, {"b", 1100, 1}});
  test_peer_.CheckConfigs({{"a", true}, {"b", false}});
}

TEST_F(CryptoServerConfigsTest, Delete) {
  // Ensure that configs get deleted when removed.
  SetConfigs({{"a", 800, 1}, {"b", 900, 1}, {"c", 1100, 1}});
  test_peer_.CheckConfigs({{"a", false}, {"b", true}, {"c", false}});
  SetConfigs({{"b", 900, 1}, {"c", 1100, 1}});
  test_peer_.CheckConfigs({{"b", true}, {"c", false}});
}

TEST_F(CryptoServerConfigsTest, DeletePrimary) {
  // Ensure that deleting the primary config works.
  SetConfigs({{"a", 800, 1}, {"b", 900, 1}, {"c", 1100, 1}});
  test_peer_.CheckConfigs({{"a", false}, {"b", true}, {"c", false}});
  SetConfigs({{"a", 800, 1}, {"c", 1100, 1}});
  test_peer_.CheckConfigs({{"a", true}, {"c", false}});
}

TEST_F(CryptoServerConfigsTest, FailIfDeletingAllConfigs) {
  // Ensure that configs get deleted when removed.
  SetConfigs({{"a", 800, 1}, {"b", 900, 1}});
  test_peer_.CheckConfigs({{"a", false}, {"b", true}});
  SetConfigs(std::vector<ServerConfigIDWithTimeAndPriority>());
  // Config change is rejected, still using old configs.
  test_peer_.CheckConfigs({{"a", false}, {"b", true}});
}

TEST_F(CryptoServerConfigsTest, ChangePrimaryTime) {
  // Check that updates to primary time get picked up.
  SetConfigs({{"a", 400, 1}, {"b", 800, 1}, {"c", 1200, 1}});
  test_peer_.SelectNewPrimaryConfig(500);
  test_peer_.CheckConfigs({{"a", true}, {"b", false}, {"c", false}});
  SetConfigs({{"a", 1200, 1}, {"b", 800, 1}, {"c", 400, 1}});
  test_peer_.SelectNewPrimaryConfig(500);
  test_peer_.CheckConfigs({{"a", false}, {"b", false}, {"c", true}});
}

TEST_F(CryptoServerConfigsTest, AllConfigsInThePast) {
  // Check that the most recent config is selected.
  SetConfigs({{"a", 400, 1}, {"b", 800, 1}, {"c", 1200, 1}});
  test_peer_.SelectNewPrimaryConfig(1500);
  test_peer_.CheckConfigs({{"a", false}, {"b", false}, {"c", true}});
}

TEST_F(CryptoServerConfigsTest, AllConfigsInTheFuture) {
  // Check that the first config is selected.
  SetConfigs({{"a", 400, 1}, {"b", 800, 1}, {"c", 1200, 1}});
  test_peer_.SelectNewPrimaryConfig(100);
  test_peer_.CheckConfigs({{"a", true}, {"b", false}, {"c", false}});
}

TEST_F(CryptoServerConfigsTest, SortByPriority) {
  // Check that priority is used to decide on a primary config when
  // configs have the same primary time.
  SetConfigs({{"a", 900, 1}, {"b", 900, 2}, {"c", 900, 3}});
  test_peer_.CheckConfigs({{"a", true}, {"b", false}, {"c", false}});
  test_peer_.SelectNewPrimaryConfig(800);
  test_peer_.CheckConfigs({{"a", true}, {"b", false}, {"c", false}});
  test_peer_.SelectNewPrimaryConfig(1000);
  test_peer_.CheckConfigs({{"a", true}, {"b", false}, {"c", false}});

  // Change priorities and expect sort order to change.
  SetConfigs({{"a", 900, 2}, {"b", 900, 1}, {"c", 900, 0}});
  test_peer_.CheckConfigs({{"a", false}, {"b", false}, {"c", true}});
  test_peer_.SelectNewPrimaryConfig(800);
  test_peer_.CheckConfigs({{"a", false}, {"b", false}, {"c", true}});
  test_peer_.SelectNewPrimaryConfig(1000);
  test_peer_.CheckConfigs({{"a", false}, {"b", false}, {"c", true}});
}

TEST_F(CryptoServerConfigsTest, AdvancePrimary) {
  // Check that a new primary config is enabled at the right time.
  SetConfigs({{"a", 900, 1}, {"b", 1100, 1}});
  test_peer_.SelectNewPrimaryConfig(1000);
  test_peer_.CheckConfigs({{"a", true}, {"b", false}});
  test_peer_.SelectNewPrimaryConfig(1101);
  test_peer_.CheckConfigs({{"a", false}, {"b", true}});
}

class ValidateCallback : public ValidateClientHelloResultCallback {
 public:
  void Run(QuicReferenceCountedPointer<Result> /*result*/,
           std::unique_ptr<ProofSource::Details> /*details*/) override {}
};

TEST_F(CryptoServerConfigsTest, AdvancePrimaryViaValidate) {
  // Check that a new primary config is enabled at the right time.
  SetConfigs({{"a", 900, 1}, {"b", 1100, 1}});
  test_peer_.SelectNewPrimaryConfig(1000);
  test_peer_.CheckConfigs({{"a", true}, {"b", false}});
  CryptoHandshakeMessage client_hello;
  QuicIpAddress client_ip;
  QuicSocketAddress server_address;
  QuicTransportVersion transport_version = QUIC_VERSION_UNSUPPORTED;
  for (const ParsedQuicVersion& version : AllSupportedVersions()) {
    if (version.handshake_protocol == PROTOCOL_QUIC_CRYPTO) {
      transport_version = version.transport_version;
      break;
    }
  }
  ASSERT_NE(transport_version, QUIC_VERSION_UNSUPPORTED);
  MockClock clock;
  QuicReferenceCountedPointer<QuicSignedServerConfig> signed_config(
      new QuicSignedServerConfig);
  std::unique_ptr<ValidateClientHelloResultCallback> done_cb(
      new ValidateCallback);
  clock.AdvanceTime(QuicTime::Delta::FromSeconds(1100));
  config_.ValidateClientHello(client_hello, client_ip, server_address,
                              transport_version, &clock, signed_config,
                              std::move(done_cb));
  test_peer_.CheckConfigs({{"a", false}, {"b", true}});
}

TEST_F(CryptoServerConfigsTest, InvalidConfigs) {
  // Ensure that invalid configs don't change anything.
  SetConfigs({{"a", 800, 1}, {"b", 900, 1}, {"c", 1100, 1}});
  test_peer_.CheckConfigs({{"a", false}, {"b", true}, {"c", false}});
  SetConfigs({{"a", 800, 1}, {"c", 1100, 1}, {"INVALID1", 1000, 1}});
  test_peer_.CheckConfigs({{"a", false}, {"b", true}, {"c", false}});
}

}  // namespace test
}  // namespace quic

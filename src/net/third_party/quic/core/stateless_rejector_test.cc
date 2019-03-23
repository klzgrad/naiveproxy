// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/stateless_rejector.h"

#include <memory>
#include <vector>

#include "net/third_party/quic/core/crypto/crypto_handshake_message.h"
#include "net/third_party/quic/core/crypto/proof_source.h"
#include "net/third_party/quic/core/quic_utils.h"
#include "net/third_party/quic/core/tls_server_handshaker.h"
#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quic/test_tools/quic_crypto_server_config_peer.h"
#include "net/third_party/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {
namespace {

QuicConnectionId TestServerDesignatedConnectionId() {
  return TestConnectionId(24);
}

// All four combinations of the two flags involved.
enum FlagsMode { ENABLED, STATELESS_DISABLED, CHEAP_DISABLED, BOTH_DISABLED };

const char* FlagsModeToString(FlagsMode mode) {
  switch (mode) {
    case ENABLED:
      return "ENABLED";
    case STATELESS_DISABLED:
      return "STATELESS_DISABLED";
    case CHEAP_DISABLED:
      return "CHEAP_DISABLED";
    case BOTH_DISABLED:
      return "BOTH_DISABLED";
    default:
      QUIC_DLOG(FATAL) << "Unexpected FlagsMode";
      return nullptr;
  }
}

// Test various combinations of QUIC version and flag state.
struct TestParams {
  ParsedQuicVersion version =
      ParsedQuicVersion{PROTOCOL_UNSUPPORTED, QUIC_VERSION_UNSUPPORTED};
  FlagsMode flags;
};

QuicString TestParamToString(const testing::TestParamInfo<TestParams>& params) {
  return QuicStrCat("v", ParsedQuicVersionToString(params.param.version), "_",
                    FlagsModeToString(params.param.flags));
}

std::vector<TestParams> GetTestParams() {
  std::vector<TestParams> params;
  for (FlagsMode flags :
       {ENABLED, STATELESS_DISABLED, CHEAP_DISABLED, BOTH_DISABLED}) {
    for (ParsedQuicVersion version : AllSupportedVersions()) {
      TestParams param;
      param.version = version;
      param.flags = flags;
      params.push_back(param);
    }
  }
  return params;
}

class StatelessRejectorTest : public QuicTestWithParam<TestParams> {
 public:
  StatelessRejectorTest()
      : proof_source_(crypto_test_utils::ProofSourceForTesting()),
        config_(QuicCryptoServerConfig::TESTING,
                QuicRandom::GetInstance(),
                crypto_test_utils::ProofSourceForTesting(),
                KeyExchangeSource::Default(),
                TlsServerHandshaker::CreateSslCtx()),
        config_peer_(&config_),
        compressed_certs_cache_(
            QuicCompressedCertsCache::kQuicCompressedCertsCacheSize),
        rejector_(QuicMakeUnique<StatelessRejector>(
            GetParam().version,
            AllSupportedVersions(),
            &config_,
            &compressed_certs_cache_,
            &clock_,
            QuicRandom::GetInstance(),
            kDefaultMaxPacketSize,
            QuicSocketAddress(QuicIpAddress::Loopback4(), 12345),
            QuicSocketAddress(QuicIpAddress::Loopback4(), 443))) {
    SetQuicReloadableFlag(
        enable_quic_stateless_reject_support,
        GetParam().flags == ENABLED || GetParam().flags == CHEAP_DISABLED);
    SetQuicReloadableFlag(
        quic_use_cheap_stateless_rejects,
        GetParam().flags == ENABLED || GetParam().flags == STATELESS_DISABLED);

    // Add a new primary config.
    std::unique_ptr<CryptoHandshakeMessage> msg(config_.AddDefaultConfig(
        QuicRandom::GetInstance(), &clock_, config_options_));

    // Save the server config.
    scid_hex_ =
        "#" + QuicTextUtils::HexEncode(config_peer_.GetPrimaryConfig()->id);

    // Encode the QUIC version.
    ver_hex_ = ParsedQuicVersionToString(GetParam().version);

    // Generate a public value.
    char public_value[32];
    memset(public_value, 42, sizeof(public_value));
    pubs_hex_ =
        "#" + QuicTextUtils::HexEncode(public_value, sizeof(public_value));

    // Generate a client nonce.
    QuicString nonce;
    CryptoUtils::GenerateNonce(
        clock_.WallNow(), QuicRandom::GetInstance(),
        QuicStringPiece(
            reinterpret_cast<char*>(config_peer_.GetPrimaryConfig()->orbit),
            kOrbitSize),
        &nonce);
    nonc_hex_ = "#" + QuicTextUtils::HexEncode(nonce);

    // Generate a source address token.
    SourceAddressTokens previous_tokens;
    QuicIpAddress ip = QuicIpAddress::Loopback4();
    MockRandom rand;
    QuicString stk = config_peer_.NewSourceAddressToken(
        config_peer_.GetPrimaryConfig()->id, previous_tokens, ip, &rand,
        clock_.WallNow(), nullptr);
    stk_hex_ = "#" + QuicTextUtils::HexEncode(stk);
  }

 protected:
  class ProcessDoneCallback : public StatelessRejector::ProcessDoneCallback {
   public:
    explicit ProcessDoneCallback(StatelessRejectorTest* test) : test_(test) {}
    void Run(std::unique_ptr<StatelessRejector> rejector) override {
      test_->rejector_ = std::move(rejector);
    }

   private:
    StatelessRejectorTest* test_;
  };

  std::unique_ptr<ProofSource> proof_source_;
  MockClock clock_;
  QuicCryptoServerConfig config_;
  QuicCryptoServerConfigPeer config_peer_;
  QuicCompressedCertsCache compressed_certs_cache_;
  QuicCryptoServerConfig::ConfigOptions config_options_;
  std::unique_ptr<StatelessRejector> rejector_;

  // Values used in CHLO messages
  QuicString scid_hex_;
  QuicString nonc_hex_;
  QuicString pubs_hex_;
  QuicString ver_hex_;
  QuicString stk_hex_;
};

INSTANTIATE_TEST_CASE_P(Flags,
                        StatelessRejectorTest,
                        ::testing::ValuesIn(GetTestParams()),
                        TestParamToString);

TEST_P(StatelessRejectorTest, InvalidChlo) {
  // clang-format off
  const CryptoHandshakeMessage client_hello = crypto_test_utils::CreateCHLO(
      {{"PDMD", "X509"},
       {"COPT", "SREJ"}});
  // clang-format on
  rejector_->OnChlo(GetParam().version.transport_version, TestConnectionId(),
                    TestServerDesignatedConnectionId(), client_hello);

  if (GetParam().flags != ENABLED) {
    EXPECT_EQ(StatelessRejector::UNSUPPORTED, rejector_->state());
    return;
  }

  // The StatelessRejector is undecided - proceed with async processing
  ASSERT_EQ(StatelessRejector::UNKNOWN, rejector_->state());
  StatelessRejector::Process(std::move(rejector_),
                             QuicMakeUnique<ProcessDoneCallback>(this));

  EXPECT_EQ(StatelessRejector::FAILED, rejector_->state());
  EXPECT_EQ(QUIC_INVALID_CRYPTO_MESSAGE_PARAMETER, rejector_->error());
}

TEST_P(StatelessRejectorTest, ValidChloWithoutSrejSupport) {
  // clang-format off
  const CryptoHandshakeMessage client_hello = crypto_test_utils::CreateCHLO(
      {{"PDMD", "X509"},
       {"AEAD", "AESG"},
       {"KEXS", "C255"},
       {"PUBS", pubs_hex_},
       {"NONC", nonc_hex_},
       {"VER\0", ver_hex_}},
      kClientHelloMinimumSize);
  // clang-format on

  rejector_->OnChlo(GetParam().version.transport_version, TestConnectionId(),
                    TestServerDesignatedConnectionId(), client_hello);
  EXPECT_EQ(StatelessRejector::UNSUPPORTED, rejector_->state());
}

TEST_P(StatelessRejectorTest, RejectChlo) {
  // clang-format off
  const CryptoHandshakeMessage client_hello = crypto_test_utils::CreateCHLO(
      {{"PDMD", "X509"},
       {"AEAD", "AESG"},
       {"KEXS", "C255"},
       {"COPT", "SREJ"},
       {"SCID", scid_hex_},
       {"PUBS", pubs_hex_},
       {"NONC", nonc_hex_},
       {"#004b5453", stk_hex_},
       {"VER\0", ver_hex_}},
      kClientHelloMinimumSize);
  // clang-format on

  rejector_->OnChlo(GetParam().version.transport_version, TestConnectionId(),
                    TestServerDesignatedConnectionId(), client_hello);
  if (GetParam().flags != ENABLED) {
    EXPECT_EQ(StatelessRejector::UNSUPPORTED, rejector_->state());
    return;
  }

  // The StatelessRejector is undecided - proceed with async processing
  ASSERT_EQ(StatelessRejector::UNKNOWN, rejector_->state());
  StatelessRejector::Process(std::move(rejector_),
                             QuicMakeUnique<ProcessDoneCallback>(this));

  ASSERT_EQ(StatelessRejector::REJECTED, rejector_->state());
  const CryptoHandshakeMessage& reply = rejector_->reply();
  EXPECT_EQ(kSREJ, reply.tag());
  QuicTagVector reject_reasons;
  EXPECT_EQ(QUIC_NO_ERROR, reply.GetTaglist(kRREJ, &reject_reasons));
  EXPECT_EQ(1u, reject_reasons.size());
  EXPECT_EQ(INVALID_EXPECTED_LEAF_CERTIFICATE,
            static_cast<HandshakeFailureReason>(reject_reasons[0]));
}

TEST_P(StatelessRejectorTest, AcceptChlo) {
  const uint64_t xlct = crypto_test_utils::LeafCertHashForTesting();
  const QuicString xlct_hex =
      "#" + QuicTextUtils::HexEncode(reinterpret_cast<const char*>(&xlct),
                                     sizeof(xlct));
  // clang-format off
  const CryptoHandshakeMessage client_hello = crypto_test_utils::CreateCHLO(
      {{"PDMD", "X509"},
       {"AEAD", "AESG"},
       {"KEXS", "C255"},
       {"COPT", "SREJ"},
       {"SCID", scid_hex_},
       {"PUBS", pubs_hex_},
       {"NONC", nonc_hex_},
       {"#004b5453", stk_hex_},
       {"VER\0", ver_hex_},
       {"XLCT", xlct_hex}},
      kClientHelloMinimumSize);
  // clang-format on

  rejector_->OnChlo(GetParam().version.transport_version, TestConnectionId(),
                    TestServerDesignatedConnectionId(), client_hello);
  if (GetParam().flags != ENABLED) {
    EXPECT_EQ(StatelessRejector::UNSUPPORTED, rejector_->state());
    return;
  }

  // The StatelessRejector is undecided - proceed with async processing
  ASSERT_EQ(StatelessRejector::UNKNOWN, rejector_->state());
  StatelessRejector::Process(std::move(rejector_),
                             QuicMakeUnique<ProcessDoneCallback>(this));

  EXPECT_EQ(StatelessRejector::ACCEPTED, rejector_->state());
}

}  // namespace
}  // namespace test
}  // namespace quic

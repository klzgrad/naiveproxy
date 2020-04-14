// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "third_party/boringssl/src/include/openssl/sha.h"
#include "net/third_party/quiche/src/quic/core/crypto/cert_compressor.h"
#include "net/third_party/quiche/src/quic/core/crypto/common_cert_set.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_utils.h"
#include "net/third_party/quiche/src/quic/core/crypto/proof_source.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_server_config.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/proto/crypto_server_config_proto.h"
#include "net/third_party/quiche/src/quic/core/quic_socket_address_coder.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/failing_proof_source.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_random.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_crypto_server_config_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {
namespace test {

namespace {

class DummyProofVerifierCallback : public ProofVerifierCallback {
 public:
  DummyProofVerifierCallback() {}
  ~DummyProofVerifierCallback() override {}

  void Run(bool /*ok*/,
           const std::string& /*error_details*/,
           std::unique_ptr<ProofVerifyDetails>* /*details*/) override {
    DCHECK(false);
  }
};

const char kOldConfigId[] = "old-config-id";

}  // namespace

struct TestParams {
  TestParams(ParsedQuicVersionVector supported_versions)
      : supported_versions(std::move(supported_versions)) {}

  friend std::ostream& operator<<(std::ostream& os, const TestParams& p) {
    os << "  versions: "
       << ParsedQuicVersionVectorToString(p.supported_versions) << " }";
    return os;
  }

  // Versions supported by client and server.
  ParsedQuicVersionVector supported_versions;
};

// Used by ::testing::PrintToStringParamName().
std::string PrintToString(const TestParams& p) {
  std::string rv = ParsedQuicVersionVectorToString(p.supported_versions);
  std::replace(rv.begin(), rv.end(), ',', '_');
  return rv;
}

// Constructs various test permutations.
std::vector<TestParams> GetTestParams() {
  std::vector<TestParams> params;

  // Start with all versions, remove highest on each iteration.
  ParsedQuicVersionVector supported_versions = AllSupportedVersions();
  while (!supported_versions.empty()) {
    params.push_back(TestParams(supported_versions));
    supported_versions.erase(supported_versions.begin());
  }

  return params;
}

class CryptoServerTest : public QuicTestWithParam<TestParams> {
 public:
  CryptoServerTest()
      : rand_(QuicRandom::GetInstance()),
        client_address_(QuicIpAddress::Loopback4(), 1234),
        client_version_(UnsupportedQuicVersion()),
        config_(QuicCryptoServerConfig::TESTING,
                rand_,
                crypto_test_utils::ProofSourceForTesting(),
                KeyExchangeSource::Default()),
        peer_(&config_),
        compressed_certs_cache_(
            QuicCompressedCertsCache::kQuicCompressedCertsCacheSize),
        params_(new QuicCryptoNegotiatedParameters),
        signed_config_(new QuicSignedServerConfig),
        chlo_packet_size_(kDefaultMaxPacketSize) {
    supported_versions_ = GetParam().supported_versions;
    config_.set_enable_serving_sct(true);

    client_version_ = supported_versions_.front();
    client_version_label_ = CreateQuicVersionLabel(client_version_);
    client_version_string_ =
        std::string(reinterpret_cast<const char*>(&client_version_label_),
                    sizeof(client_version_label_));
  }

  void SetUp() override {
    QuicCryptoServerConfig::ConfigOptions old_config_options;
    old_config_options.id = kOldConfigId;
    config_.AddDefaultConfig(rand_, &clock_, old_config_options);
    clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(1000));
    QuicServerConfigProtobuf primary_config =
        config_.GenerateConfig(rand_, &clock_, config_options_);
    primary_config.set_primary_time(clock_.WallNow().ToUNIXSeconds());
    std::unique_ptr<CryptoHandshakeMessage> msg(
        config_.AddConfig(primary_config, clock_.WallNow()));

    quiche::QuicheStringPiece orbit;
    CHECK(msg->GetStringPiece(kORBT, &orbit));
    CHECK_EQ(sizeof(orbit_), orbit.size());
    memcpy(orbit_, orbit.data(), orbit.size());

    char public_value[32];
    memset(public_value, 42, sizeof(public_value));

    nonce_hex_ = "#" + quiche::QuicheTextUtils::HexEncode(GenerateNonce());
    pub_hex_ = "#" + quiche::QuicheTextUtils::HexEncode(public_value,
                                                        sizeof(public_value));

    CryptoHandshakeMessage client_hello =
        crypto_test_utils::CreateCHLO({{"PDMD", "X509"},
                                       {"AEAD", "AESG"},
                                       {"KEXS", "C255"},
                                       {"PUBS", pub_hex_},
                                       {"NONC", nonce_hex_},
                                       {"CSCT", ""},
                                       {"VER\0", client_version_string_}},
                                      kClientHelloMinimumSize);
    ShouldSucceed(client_hello);
    // The message should be rejected because the source-address token is
    // missing.
    CheckRejectTag();
    const HandshakeFailureReason kRejectReasons[] = {
        SERVER_CONFIG_INCHOATE_HELLO_FAILURE};
    CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));

    quiche::QuicheStringPiece srct;
    ASSERT_TRUE(out_.GetStringPiece(kSourceAddressTokenTag, &srct));
    srct_hex_ = "#" + quiche::QuicheTextUtils::HexEncode(srct);

    quiche::QuicheStringPiece scfg;
    ASSERT_TRUE(out_.GetStringPiece(kSCFG, &scfg));
    server_config_ = CryptoFramer::ParseMessage(scfg);

    quiche::QuicheStringPiece scid;
    ASSERT_TRUE(server_config_->GetStringPiece(kSCID, &scid));
    scid_hex_ = "#" + quiche::QuicheTextUtils::HexEncode(scid);

    signed_config_ = QuicReferenceCountedPointer<QuicSignedServerConfig>(
        new QuicSignedServerConfig());
    DCHECK(signed_config_->chain.get() == nullptr);
  }

  // Helper used to accept the result of ValidateClientHello and pass
  // it on to ProcessClientHello.
  class ValidateCallback : public ValidateClientHelloResultCallback {
   public:
    ValidateCallback(CryptoServerTest* test,
                     bool should_succeed,
                     const char* error_substr,
                     bool* called)
        : test_(test),
          should_succeed_(should_succeed),
          error_substr_(error_substr),
          called_(called) {
      *called_ = false;
    }

    void Run(QuicReferenceCountedPointer<Result> result,
             std::unique_ptr<ProofSource::Details> /* details */) override {
      ASSERT_FALSE(*called_);
      test_->ProcessValidationResult(std::move(result), should_succeed_,
                                     error_substr_);
      *called_ = true;
    }

   private:
    CryptoServerTest* test_;
    const bool should_succeed_;
    const char* const error_substr_;
    bool* called_;
  };

  void CheckServerHello(const CryptoHandshakeMessage& server_hello) {
    QuicVersionLabelVector versions;
    server_hello.GetVersionLabelList(kVER, &versions);
    ASSERT_EQ(supported_versions_.size(), versions.size());
    for (size_t i = 0; i < versions.size(); ++i) {
      EXPECT_EQ(CreateQuicVersionLabel(supported_versions_[i]), versions[i]);
    }

    quiche::QuicheStringPiece address;
    ASSERT_TRUE(server_hello.GetStringPiece(kCADR, &address));
    QuicSocketAddressCoder decoder;
    ASSERT_TRUE(decoder.Decode(address.data(), address.size()));
    EXPECT_EQ(client_address_.host(), decoder.ip());
    EXPECT_EQ(client_address_.port(), decoder.port());
  }

  void ShouldSucceed(const CryptoHandshakeMessage& message) {
    bool called = false;
    QuicSocketAddress server_address(QuicIpAddress::Any4(), 5);
    config_.ValidateClientHello(
        message, client_address_.host(), server_address,
        supported_versions_.front().transport_version, &clock_, signed_config_,
        std::make_unique<ValidateCallback>(this, true, "", &called));
    EXPECT_TRUE(called);
  }

  void ShouldFailMentioning(const char* error_substr,
                            const CryptoHandshakeMessage& message) {
    bool called = false;
    ShouldFailMentioning(error_substr, message, &called);
    EXPECT_TRUE(called);
  }

  void ShouldFailMentioning(const char* error_substr,
                            const CryptoHandshakeMessage& message,
                            bool* called) {
    QuicSocketAddress server_address(QuicIpAddress::Any4(), 5);
    config_.ValidateClientHello(
        message, client_address_.host(), server_address,
        supported_versions_.front().transport_version, &clock_, signed_config_,
        std::make_unique<ValidateCallback>(this, false, error_substr, called));
  }

  class ProcessCallback : public ProcessClientHelloResultCallback {
   public:
    ProcessCallback(
        QuicReferenceCountedPointer<ValidateCallback::Result> result,
        bool should_succeed,
        const char* error_substr,
        bool* called,
        CryptoHandshakeMessage* out)
        : result_(std::move(result)),
          should_succeed_(should_succeed),
          error_substr_(error_substr),
          called_(called),
          out_(out) {
      *called_ = false;
    }

    void Run(QuicErrorCode error,
             const std::string& error_details,
             std::unique_ptr<CryptoHandshakeMessage> message,
             std::unique_ptr<DiversificationNonce> /*diversification_nonce*/,
             std::unique_ptr<ProofSource::Details> /*proof_source_details*/)
        override {
      if (should_succeed_) {
        ASSERT_EQ(error, QUIC_NO_ERROR)
            << "Message failed with error " << error_details << ": "
            << result_->client_hello.DebugString();
      } else {
        ASSERT_NE(error, QUIC_NO_ERROR)
            << "Message didn't fail: " << result_->client_hello.DebugString();
        EXPECT_TRUE(error_details.find(error_substr_) != std::string::npos)
            << error_substr_ << " not in " << error_details;
      }
      if (message != nullptr) {
        *out_ = *message;
      }
      *called_ = true;
    }

   private:
    const QuicReferenceCountedPointer<ValidateCallback::Result> result_;
    const bool should_succeed_;
    const char* const error_substr_;
    bool* called_;
    CryptoHandshakeMessage* out_;
  };

  void ProcessValidationResult(
      QuicReferenceCountedPointer<ValidateCallback::Result> result,
      bool should_succeed,
      const char* error_substr) {
    QuicSocketAddress server_address(QuicIpAddress::Any4(), 5);
    bool called;
    config_.ProcessClientHello(
        result, /*reject_only=*/false,
        /*connection_id=*/TestConnectionId(1), server_address, client_address_,
        supported_versions_.front(), supported_versions_, &clock_, rand_,
        &compressed_certs_cache_, params_, signed_config_,
        /*total_framing_overhead=*/50, chlo_packet_size_,
        std::make_unique<ProcessCallback>(result, should_succeed, error_substr,
                                          &called, &out_));
    EXPECT_TRUE(called);
  }

  std::string GenerateNonce() {
    std::string nonce;
    CryptoUtils::GenerateNonce(
        clock_.WallNow(), rand_,
        quiche::QuicheStringPiece(reinterpret_cast<const char*>(orbit_),
                                  sizeof(orbit_)),
        &nonce);
    return nonce;
  }

  void CheckRejectReasons(
      const HandshakeFailureReason* expected_handshake_failures,
      size_t expected_count) {
    QuicTagVector reject_reasons;
    static_assert(sizeof(QuicTag) == sizeof(uint32_t), "header out of sync");
    QuicErrorCode error_code = out_.GetTaglist(kRREJ, &reject_reasons);
    ASSERT_THAT(error_code, IsQuicNoError());

    EXPECT_EQ(expected_count, reject_reasons.size());
    for (size_t i = 0; i < reject_reasons.size(); ++i) {
      EXPECT_EQ(static_cast<QuicTag>(expected_handshake_failures[i]),
                reject_reasons[i]);
    }
  }

  void CheckRejectTag() {
    ASSERT_EQ(kREJ, out_.tag()) << QuicTagToString(out_.tag());
  }

  std::string XlctHexString() {
    uint64_t xlct = crypto_test_utils::LeafCertHashForTesting();
    return "#" + quiche::QuicheTextUtils::HexEncode(
                     reinterpret_cast<char*>(&xlct), sizeof(xlct));
  }

 protected:
  QuicRandom* const rand_;
  MockRandom rand_for_id_generation_;
  MockClock clock_;
  QuicSocketAddress client_address_;
  ParsedQuicVersionVector supported_versions_;
  ParsedQuicVersion client_version_;
  QuicVersionLabel client_version_label_;
  std::string client_version_string_;
  QuicCryptoServerConfig config_;
  QuicCryptoServerConfigPeer peer_;
  QuicCompressedCertsCache compressed_certs_cache_;
  QuicCryptoServerConfig::ConfigOptions config_options_;
  QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> params_;
  QuicReferenceCountedPointer<QuicSignedServerConfig> signed_config_;
  CryptoHandshakeMessage out_;
  uint8_t orbit_[kOrbitSize];
  size_t chlo_packet_size_;

  // These strings contain hex escaped values from the server suitable for using
  // when constructing client hello messages.
  std::string nonce_hex_, pub_hex_, srct_hex_, scid_hex_;
  std::unique_ptr<CryptoHandshakeMessage> server_config_;
};

INSTANTIATE_TEST_SUITE_P(CryptoServerTests,
                         CryptoServerTest,
                         ::testing::ValuesIn(GetTestParams()),
                         ::testing::PrintToStringParamName());

TEST_P(CryptoServerTest, BadSNI) {
  // clang-format off
  static const char* const kBadSNIs[] = {
    "",
    "foo",
    "#00",
    "#ff00",
    "127.0.0.1",
    "ffee::1",
  };
  // clang-format on

  for (size_t i = 0; i < QUICHE_ARRAYSIZE(kBadSNIs); i++) {
    CryptoHandshakeMessage msg =
        crypto_test_utils::CreateCHLO({{"PDMD", "X509"},
                                       {"SNI", kBadSNIs[i]},
                                       {"VER\0", client_version_string_}},
                                      kClientHelloMinimumSize);
    ShouldFailMentioning("SNI", msg);
    const HandshakeFailureReason kRejectReasons[] = {
        SERVER_CONFIG_INCHOATE_HELLO_FAILURE};
    CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));
  }
}

TEST_P(CryptoServerTest, DefaultCert) {
  // Check that the server replies with a default certificate when no SNI is
  // specified. The CHLO is constructed to generate a REJ with certs, so must
  // not contain a valid STK, and must include PDMD.
  CryptoHandshakeMessage msg =
      crypto_test_utils::CreateCHLO({{"AEAD", "AESG"},
                                     {"KEXS", "C255"},
                                     {"PUBS", pub_hex_},
                                     {"NONC", nonce_hex_},
                                     {"PDMD", "X509"},
                                     {"VER\0", client_version_string_}},
                                    kClientHelloMinimumSize);

  ShouldSucceed(msg);
  quiche::QuicheStringPiece cert, proof, cert_sct;
  EXPECT_TRUE(out_.GetStringPiece(kCertificateTag, &cert));
  EXPECT_TRUE(out_.GetStringPiece(kPROF, &proof));
  EXPECT_TRUE(out_.GetStringPiece(kCertificateSCTTag, &cert_sct));
  EXPECT_NE(0u, cert.size());
  EXPECT_NE(0u, proof.size());
  const HandshakeFailureReason kRejectReasons[] = {
      SERVER_CONFIG_INCHOATE_HELLO_FAILURE};
  CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));
  EXPECT_LT(0u, cert_sct.size());
}

TEST_P(CryptoServerTest, RejectTooLarge) {
  // Check that the server replies with no certificate when a CHLO is
  // constructed with a PDMD but no SKT when the REJ would be too large.
  CryptoHandshakeMessage msg =
      crypto_test_utils::CreateCHLO({{"PDMD", "X509"},
                                     {"AEAD", "AESG"},
                                     {"KEXS", "C255"},
                                     {"PUBS", pub_hex_},
                                     {"NONC", nonce_hex_},
                                     {"PDMD", "X509"},
                                     {"VER\0", client_version_string_}},
                                    kClientHelloMinimumSize);

  // The REJ will be larger than the CHLO so no PROF or CRT will be sent.
  config_.set_chlo_multiplier(1);

  ShouldSucceed(msg);
  quiche::QuicheStringPiece cert, proof, cert_sct;
  EXPECT_FALSE(out_.GetStringPiece(kCertificateTag, &cert));
  EXPECT_FALSE(out_.GetStringPiece(kPROF, &proof));
  EXPECT_FALSE(out_.GetStringPiece(kCertificateSCTTag, &cert_sct));
  const HandshakeFailureReason kRejectReasons[] = {
      SERVER_CONFIG_INCHOATE_HELLO_FAILURE};
  CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));
}

TEST_P(CryptoServerTest, RejectNotTooLarge) {
  // When the CHLO packet is large enough, ensure that a full REJ is sent.
  chlo_packet_size_ *= 2;

  CryptoHandshakeMessage msg =
      crypto_test_utils::CreateCHLO({{"PDMD", "X509"},
                                     {"AEAD", "AESG"},
                                     {"KEXS", "C255"},
                                     {"PUBS", pub_hex_},
                                     {"NONC", nonce_hex_},
                                     {"PDMD", "X509"},
                                     {"VER\0", client_version_string_}},
                                    kClientHelloMinimumSize);

  // The REJ will be larger than the CHLO so no PROF or CRT will be sent.
  config_.set_chlo_multiplier(1);

  ShouldSucceed(msg);
  quiche::QuicheStringPiece cert, proof, cert_sct;
  EXPECT_TRUE(out_.GetStringPiece(kCertificateTag, &cert));
  EXPECT_TRUE(out_.GetStringPiece(kPROF, &proof));
  EXPECT_TRUE(out_.GetStringPiece(kCertificateSCTTag, &cert_sct));
  const HandshakeFailureReason kRejectReasons[] = {
      SERVER_CONFIG_INCHOATE_HELLO_FAILURE};
  CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));
}

TEST_P(CryptoServerTest, RejectTooLargeButValidSTK) {
  // Check that the server replies with no certificate when a CHLO is
  // constructed with a PDMD but no SKT when the REJ would be too large.
  CryptoHandshakeMessage msg =
      crypto_test_utils::CreateCHLO({{"PDMD", "X509"},
                                     {"AEAD", "AESG"},
                                     {"KEXS", "C255"},
                                     {"PUBS", pub_hex_},
                                     {"NONC", nonce_hex_},
                                     {"#004b5453", srct_hex_},
                                     {"PDMD", "X509"},
                                     {"VER\0", client_version_string_}},
                                    kClientHelloMinimumSize);

  // The REJ will be larger than the CHLO so no PROF or CRT will be sent.
  config_.set_chlo_multiplier(1);

  ShouldSucceed(msg);
  quiche::QuicheStringPiece cert, proof, cert_sct;
  EXPECT_TRUE(out_.GetStringPiece(kCertificateTag, &cert));
  EXPECT_TRUE(out_.GetStringPiece(kPROF, &proof));
  EXPECT_TRUE(out_.GetStringPiece(kCertificateSCTTag, &cert_sct));
  EXPECT_NE(0u, cert.size());
  EXPECT_NE(0u, proof.size());
  const HandshakeFailureReason kRejectReasons[] = {
      SERVER_CONFIG_INCHOATE_HELLO_FAILURE};
  CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));
}

TEST_P(CryptoServerTest, TooSmall) {
  ShouldFailMentioning(
      "too small", crypto_test_utils::CreateCHLO(
                       {{"PDMD", "X509"}, {"VER\0", client_version_string_}}));

  const HandshakeFailureReason kRejectReasons[] = {
      SERVER_CONFIG_INCHOATE_HELLO_FAILURE};
  CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));
}

TEST_P(CryptoServerTest, BadSourceAddressToken) {
  // Invalid source-address tokens should be ignored.
  // clang-format off
  static const char* const kBadSourceAddressTokens[] = {
    "",
    "foo",
    "#0000",
    "#0000000000000000000000000000000000000000",
  };
  // clang-format on

  for (size_t i = 0; i < QUICHE_ARRAYSIZE(kBadSourceAddressTokens); i++) {
    CryptoHandshakeMessage msg =
        crypto_test_utils::CreateCHLO({{"PDMD", "X509"},
                                       {"STK", kBadSourceAddressTokens[i]},
                                       {"VER\0", client_version_string_}},
                                      kClientHelloMinimumSize);
    ShouldSucceed(msg);
    const HandshakeFailureReason kRejectReasons[] = {
        SERVER_CONFIG_INCHOATE_HELLO_FAILURE};
    CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));
  }
}

TEST_P(CryptoServerTest, BadClientNonce) {
  // clang-format off
  static const char* const kBadNonces[] = {
    "",
    "#0000",
    "#0000000000000000000000000000000000000000",
  };
  // clang-format on

  for (size_t i = 0; i < QUICHE_ARRAYSIZE(kBadNonces); i++) {
    // Invalid nonces should be ignored, in an inchoate CHLO.

    CryptoHandshakeMessage msg =
        crypto_test_utils::CreateCHLO({{"PDMD", "X509"},
                                       {"NONC", kBadNonces[i]},
                                       {"VER\0", client_version_string_}},
                                      kClientHelloMinimumSize);

    ShouldSucceed(msg);
    const HandshakeFailureReason kRejectReasons[] = {
        SERVER_CONFIG_INCHOATE_HELLO_FAILURE};
    CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));

    // Invalid nonces should result in CLIENT_NONCE_INVALID_FAILURE.
    CryptoHandshakeMessage msg1 =
        crypto_test_utils::CreateCHLO({{"PDMD", "X509"},
                                       {"AEAD", "AESG"},
                                       {"KEXS", "C255"},
                                       {"SCID", scid_hex_},
                                       {"#004b5453", srct_hex_},
                                       {"PUBS", pub_hex_},
                                       {"NONC", kBadNonces[i]},
                                       {"NONP", kBadNonces[i]},
                                       {"XLCT", XlctHexString()},
                                       {"VER\0", client_version_string_}},
                                      kClientHelloMinimumSize);

    ShouldSucceed(msg1);

    CheckRejectTag();
    const HandshakeFailureReason kRejectReasons1[] = {
        CLIENT_NONCE_INVALID_FAILURE};
    CheckRejectReasons(kRejectReasons1, QUICHE_ARRAYSIZE(kRejectReasons1));
  }
}

TEST_P(CryptoServerTest, NoClientNonce) {
  // No client nonces should result in INCHOATE_HELLO_FAILURE.

  CryptoHandshakeMessage msg = crypto_test_utils::CreateCHLO(
      {{"PDMD", "X509"}, {"VER\0", client_version_string_}},
      kClientHelloMinimumSize);

  ShouldSucceed(msg);
  const HandshakeFailureReason kRejectReasons[] = {
      SERVER_CONFIG_INCHOATE_HELLO_FAILURE};
  CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));

  CryptoHandshakeMessage msg1 =
      crypto_test_utils::CreateCHLO({{"PDMD", "X509"},
                                     {"AEAD", "AESG"},
                                     {"KEXS", "C255"},
                                     {"SCID", scid_hex_},
                                     {"#004b5453", srct_hex_},
                                     {"PUBS", pub_hex_},
                                     {"XLCT", XlctHexString()},
                                     {"VER\0", client_version_string_}},
                                    kClientHelloMinimumSize);

  ShouldSucceed(msg1);
  CheckRejectTag();
  const HandshakeFailureReason kRejectReasons1[] = {
      SERVER_CONFIG_INCHOATE_HELLO_FAILURE};
  CheckRejectReasons(kRejectReasons1, QUICHE_ARRAYSIZE(kRejectReasons1));
}

TEST_P(CryptoServerTest, DowngradeAttack) {
  if (supported_versions_.size() == 1) {
    // No downgrade attack is possible if the server only supports one version.
    return;
  }
  // Set the client's preferred version to a supported version that
  // is not the "current" version (supported_versions_.front()).
  std::string bad_version =
      ParsedQuicVersionToString(supported_versions_.back());

  CryptoHandshakeMessage msg = crypto_test_utils::CreateCHLO(
      {{"PDMD", "X509"}, {"VER\0", bad_version}}, kClientHelloMinimumSize);

  ShouldFailMentioning("Downgrade", msg);
  const HandshakeFailureReason kRejectReasons[] = {
      SERVER_CONFIG_INCHOATE_HELLO_FAILURE};
  CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));
}

TEST_P(CryptoServerTest, CorruptServerConfig) {
  // This tests corrupted server config.
  CryptoHandshakeMessage msg = crypto_test_utils::CreateCHLO(
      {{"PDMD", "X509"},
       {"AEAD", "AESG"},
       {"KEXS", "C255"},
       {"SCID", (std::string(1, 'X') + scid_hex_)},
       {"#004b5453", srct_hex_},
       {"PUBS", pub_hex_},
       {"NONC", nonce_hex_},
       {"VER\0", client_version_string_}},
      kClientHelloMinimumSize);

  ShouldSucceed(msg);
  CheckRejectTag();
  const HandshakeFailureReason kRejectReasons[] = {
      SERVER_CONFIG_UNKNOWN_CONFIG_FAILURE};
  CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));
}

TEST_P(CryptoServerTest, CorruptSourceAddressToken) {
  // This tests corrupted source address token.
  CryptoHandshakeMessage msg = crypto_test_utils::CreateCHLO(
      {{"PDMD", "X509"},
       {"AEAD", "AESG"},
       {"KEXS", "C255"},
       {"SCID", scid_hex_},
       {"#004b5453", (std::string(1, 'X') + srct_hex_)},
       {"PUBS", pub_hex_},
       {"NONC", nonce_hex_},
       {"XLCT", XlctHexString()},
       {"VER\0", client_version_string_}},
      kClientHelloMinimumSize);

  ShouldSucceed(msg);
  CheckRejectTag();
  const HandshakeFailureReason kRejectReasons[] = {
      SOURCE_ADDRESS_TOKEN_DECRYPTION_FAILURE};
  CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));
}

TEST_P(CryptoServerTest, CorruptSourceAddressTokenIsStillAccepted) {
  // This tests corrupted source address token.
  CryptoHandshakeMessage msg = crypto_test_utils::CreateCHLO(
      {{"PDMD", "X509"},
       {"AEAD", "AESG"},
       {"KEXS", "C255"},
       {"SCID", scid_hex_},
       {"#004b5453", (std::string(1, 'X') + srct_hex_)},
       {"PUBS", pub_hex_},
       {"NONC", nonce_hex_},
       {"XLCT", XlctHexString()},
       {"VER\0", client_version_string_}},
      kClientHelloMinimumSize);

  config_.set_validate_source_address_token(false);

  ShouldSucceed(msg);
  EXPECT_EQ(kSHLO, out_.tag());
}

TEST_P(CryptoServerTest, CorruptClientNonceAndSourceAddressToken) {
  // This test corrupts client nonce and source address token.
  CryptoHandshakeMessage msg = crypto_test_utils::CreateCHLO(
      {{"PDMD", "X509"},
       {"AEAD", "AESG"},
       {"KEXS", "C255"},
       {"SCID", scid_hex_},
       {"#004b5453", (std::string(1, 'X') + srct_hex_)},
       {"PUBS", pub_hex_},
       {"NONC", (std::string(1, 'X') + nonce_hex_)},
       {"XLCT", XlctHexString()},
       {"VER\0", client_version_string_}},
      kClientHelloMinimumSize);

  ShouldSucceed(msg);
  CheckRejectTag();
  const HandshakeFailureReason kRejectReasons[] = {
      SOURCE_ADDRESS_TOKEN_DECRYPTION_FAILURE, CLIENT_NONCE_INVALID_FAILURE};
  CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));
}

TEST_P(CryptoServerTest, CorruptMultipleTags) {
  // This test corrupts client nonce, server nonce and source address token.
  CryptoHandshakeMessage msg = crypto_test_utils::CreateCHLO(
      {{"PDMD", "X509"},
       {"AEAD", "AESG"},
       {"KEXS", "C255"},
       {"SCID", scid_hex_},
       {"#004b5453", (std::string(1, 'X') + srct_hex_)},
       {"PUBS", pub_hex_},
       {"NONC", (std::string(1, 'X') + nonce_hex_)},
       {"NONP", (std::string(1, 'X') + nonce_hex_)},
       {"SNO\0", (std::string(1, 'X') + nonce_hex_)},
       {"XLCT", XlctHexString()},
       {"VER\0", client_version_string_}},
      kClientHelloMinimumSize);

  ShouldSucceed(msg);
  CheckRejectTag();

  const HandshakeFailureReason kRejectReasons[] = {
      SOURCE_ADDRESS_TOKEN_DECRYPTION_FAILURE, CLIENT_NONCE_INVALID_FAILURE};
  CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));
}

TEST_P(CryptoServerTest, NoServerNonce) {
  // When no server nonce is present and no strike register is configured,
  // the CHLO should be rejected.
  CryptoHandshakeMessage msg =
      crypto_test_utils::CreateCHLO({{"PDMD", "X509"},
                                     {"AEAD", "AESG"},
                                     {"KEXS", "C255"},
                                     {"SCID", scid_hex_},
                                     {"#004b5453", srct_hex_},
                                     {"PUBS", pub_hex_},
                                     {"NONC", nonce_hex_},
                                     {"NONP", nonce_hex_},
                                     {"XLCT", XlctHexString()},
                                     {"VER\0", client_version_string_}},
                                    kClientHelloMinimumSize);

  ShouldSucceed(msg);

  // Even without a server nonce, this ClientHello should be accepted in
  // version 33.
  ASSERT_EQ(kSHLO, out_.tag());
  CheckServerHello(out_);
}

TEST_P(CryptoServerTest, ProofForSuppliedServerConfig) {
  client_address_ = QuicSocketAddress(QuicIpAddress::Loopback6(), 1234);

  CryptoHandshakeMessage msg =
      crypto_test_utils::CreateCHLO({{"AEAD", "AESG"},
                                     {"KEXS", "C255"},
                                     {"PDMD", "X509"},
                                     {"SCID", kOldConfigId},
                                     {"#004b5453", srct_hex_},
                                     {"PUBS", pub_hex_},
                                     {"NONC", nonce_hex_},
                                     {"NONP", "123456789012345678901234567890"},
                                     {"VER\0", client_version_string_},
                                     {"XLCT", XlctHexString()}},
                                    kClientHelloMinimumSize);

  ShouldSucceed(msg);
  // The message should be rejected because the source-address token is no
  // longer valid.
  CheckRejectTag();
  const HandshakeFailureReason kRejectReasons[] = {
      SOURCE_ADDRESS_TOKEN_DIFFERENT_IP_ADDRESS_FAILURE};
  CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));

  quiche::QuicheStringPiece cert, proof, scfg_str;
  EXPECT_TRUE(out_.GetStringPiece(kCertificateTag, &cert));
  EXPECT_TRUE(out_.GetStringPiece(kPROF, &proof));
  EXPECT_TRUE(out_.GetStringPiece(kSCFG, &scfg_str));
  std::unique_ptr<CryptoHandshakeMessage> scfg(
      CryptoFramer::ParseMessage(scfg_str));
  quiche::QuicheStringPiece scid;
  EXPECT_TRUE(scfg->GetStringPiece(kSCID, &scid));
  EXPECT_NE(scid, kOldConfigId);

  // Get certs from compressed certs.
  const CommonCertSets* common_cert_sets(CommonCertSets::GetInstanceQUIC());
  std::vector<std::string> cached_certs;

  std::vector<std::string> certs;
  ASSERT_TRUE(CertCompressor::DecompressChain(cert, cached_certs,
                                              common_cert_sets, &certs));

  // Check that the proof in the REJ message is valid.
  std::unique_ptr<ProofVerifier> proof_verifier(
      crypto_test_utils::ProofVerifierForTesting());
  std::unique_ptr<ProofVerifyContext> verify_context(
      crypto_test_utils::ProofVerifyContextForTesting());
  std::unique_ptr<ProofVerifyDetails> details;
  std::string error_details;
  std::unique_ptr<ProofVerifierCallback> callback(
      new DummyProofVerifierCallback());
  const std::string chlo_hash =
      CryptoUtils::HashHandshakeMessage(msg, Perspective::IS_SERVER);
  EXPECT_EQ(QUIC_SUCCESS,
            proof_verifier->VerifyProof(
                "test.example.com", 443, (std::string(scfg_str)),
                client_version_.transport_version, chlo_hash, certs, "",
                (std::string(proof)), verify_context.get(), &error_details,
                &details, std::move(callback)));
}

TEST_P(CryptoServerTest, RejectInvalidXlct) {
  CryptoHandshakeMessage msg =
      crypto_test_utils::CreateCHLO({{"PDMD", "X509"},
                                     {"AEAD", "AESG"},
                                     {"KEXS", "C255"},
                                     {"SCID", scid_hex_},
                                     {"#004b5453", srct_hex_},
                                     {"PUBS", pub_hex_},
                                     {"NONC", nonce_hex_},
                                     {"VER\0", client_version_string_},
                                     {"XLCT", "#0102030405060708"}},
                                    kClientHelloMinimumSize);

  // If replay protection isn't disabled, then
  // QuicCryptoServerConfig::EvaluateClientHello will leave info.unique as false
  // and cause ProcessClientHello to exit early (and generate a REJ message).
  config_.set_replay_protection(false);

  ShouldSucceed(msg);

  const HandshakeFailureReason kRejectReasons[] = {
      INVALID_EXPECTED_LEAF_CERTIFICATE};

  CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));
}

TEST_P(CryptoServerTest, ValidXlct) {
  CryptoHandshakeMessage msg =
      crypto_test_utils::CreateCHLO({{"PDMD", "X509"},
                                     {"AEAD", "AESG"},
                                     {"KEXS", "C255"},
                                     {"SCID", scid_hex_},
                                     {"#004b5453", srct_hex_},
                                     {"PUBS", pub_hex_},
                                     {"NONC", nonce_hex_},
                                     {"VER\0", client_version_string_},
                                     {"XLCT", XlctHexString()}},
                                    kClientHelloMinimumSize);

  // If replay protection isn't disabled, then
  // QuicCryptoServerConfig::EvaluateClientHello will leave info.unique as false
  // and cause ProcessClientHello to exit early (and generate a REJ message).
  config_.set_replay_protection(false);

  ShouldSucceed(msg);
  EXPECT_EQ(kSHLO, out_.tag());
}

TEST_P(CryptoServerTest, NonceInSHLO) {
  CryptoHandshakeMessage msg =
      crypto_test_utils::CreateCHLO({{"PDMD", "X509"},
                                     {"AEAD", "AESG"},
                                     {"KEXS", "C255"},
                                     {"SCID", scid_hex_},
                                     {"#004b5453", srct_hex_},
                                     {"PUBS", pub_hex_},
                                     {"NONC", nonce_hex_},
                                     {"VER\0", client_version_string_},
                                     {"XLCT", XlctHexString()}},
                                    kClientHelloMinimumSize);

  // If replay protection isn't disabled, then
  // QuicCryptoServerConfig::EvaluateClientHello will leave info.unique as false
  // and cause ProcessClientHello to exit early (and generate a REJ message).
  config_.set_replay_protection(false);

  ShouldSucceed(msg);
  EXPECT_EQ(kSHLO, out_.tag());

  quiche::QuicheStringPiece nonce;
  EXPECT_TRUE(out_.GetStringPiece(kServerNonceTag, &nonce));
}

TEST_P(CryptoServerTest, ProofSourceFailure) {
  // Install a ProofSource which will unconditionally fail
  peer_.ResetProofSource(std::unique_ptr<ProofSource>(new FailingProofSource));

  CryptoHandshakeMessage msg =
      crypto_test_utils::CreateCHLO({{"AEAD", "AESG"},
                                     {"KEXS", "C255"},
                                     {"SCID", scid_hex_},
                                     {"PUBS", pub_hex_},
                                     {"NONC", nonce_hex_},
                                     {"PDMD", "X509"},
                                     {"VER\0", client_version_string_}},
                                    kClientHelloMinimumSize);

  // Just ensure that we don't crash as occurred in b/33916924.
  ShouldFailMentioning("", msg);
}

// Regression test for crbug.com/723604
// For 2RTT, if the first CHLO from the client contains hashes of cached
// certs (stored in CCRT tag) but the second CHLO does not, then the second REJ
// from the server should not contain hashes of cached certs.
TEST_P(CryptoServerTest, TwoRttServerDropCachedCerts) {
  // Send inchoate CHLO to get cert chain from server. This CHLO is only for
  // the purpose of getting the server's certs; it is not part of the 2RTT
  // handshake.
  CryptoHandshakeMessage msg = crypto_test_utils::CreateCHLO(
      {{"PDMD", "X509"}, {"VER\0", client_version_string_}},
      kClientHelloMinimumSize);
  ShouldSucceed(msg);

  // Decompress cert chain from server to individual certs.
  quiche::QuicheStringPiece certs_compressed;
  ASSERT_TRUE(out_.GetStringPiece(kCertificateTag, &certs_compressed));
  ASSERT_NE(0u, certs_compressed.size());
  std::vector<std::string> certs;
  ASSERT_TRUE(CertCompressor::DecompressChain(
      certs_compressed, /*cached_certs=*/{}, /*common_sets=*/nullptr, &certs));

  // Start 2-RTT. Client sends CHLO with bad source-address token and hashes of
  // the certs, which tells the server that the client has cached those certs.
  config_.set_chlo_multiplier(1);
  const char kBadSourceAddressToken[] = "";
  msg.SetStringPiece(kSourceAddressTokenTag, kBadSourceAddressToken);
  std::vector<uint64_t> hashes(certs.size());
  for (size_t i = 0; i < certs.size(); ++i) {
    hashes[i] = QuicUtils::QuicUtils::FNV1a_64_Hash(certs[i]);
  }
  msg.SetVector(kCCRT, hashes);
  ShouldSucceed(msg);

  // Server responds with inchoate REJ containing valid source-address token.
  quiche::QuicheStringPiece srct;
  ASSERT_TRUE(out_.GetStringPiece(kSourceAddressTokenTag, &srct));

  // Client now drops cached certs; sends CHLO with updated source-address
  // token but no hashes of certs.
  msg.SetStringPiece(kSourceAddressTokenTag, srct);
  msg.Erase(kCCRT);
  ShouldSucceed(msg);

  // Server response's cert chain should not contain hashes of
  // previously-cached certs.
  ASSERT_TRUE(out_.GetStringPiece(kCertificateTag, &certs_compressed));
  ASSERT_NE(0u, certs_compressed.size());
  ASSERT_TRUE(CertCompressor::DecompressChain(
      certs_compressed, /*cached_certs=*/{}, /*common_sets=*/nullptr, &certs));
}

class CryptoServerConfigGenerationTest : public QuicTest {};

TEST_F(CryptoServerConfigGenerationTest, Determinism) {
  // Test that using a deterministic PRNG causes the server-config to be
  // deterministic.

  MockRandom rand_a, rand_b;
  const QuicCryptoServerConfig::ConfigOptions options;
  MockClock clock;

  QuicCryptoServerConfig a(QuicCryptoServerConfig::TESTING, &rand_a,
                           crypto_test_utils::ProofSourceForTesting(),
                           KeyExchangeSource::Default());
  QuicCryptoServerConfig b(QuicCryptoServerConfig::TESTING, &rand_b,
                           crypto_test_utils::ProofSourceForTesting(),
                           KeyExchangeSource::Default());
  std::unique_ptr<CryptoHandshakeMessage> scfg_a(
      a.AddDefaultConfig(&rand_a, &clock, options));
  std::unique_ptr<CryptoHandshakeMessage> scfg_b(
      b.AddDefaultConfig(&rand_b, &clock, options));

  ASSERT_EQ(scfg_a->DebugString(), scfg_b->DebugString());
}

TEST_F(CryptoServerConfigGenerationTest, SCIDVaries) {
  // This test ensures that the server config ID varies for different server
  // configs.

  MockRandom rand_a, rand_b;
  const QuicCryptoServerConfig::ConfigOptions options;
  MockClock clock;

  QuicCryptoServerConfig a(QuicCryptoServerConfig::TESTING, &rand_a,
                           crypto_test_utils::ProofSourceForTesting(),
                           KeyExchangeSource::Default());
  rand_b.ChangeValue();
  QuicCryptoServerConfig b(QuicCryptoServerConfig::TESTING, &rand_b,
                           crypto_test_utils::ProofSourceForTesting(),
                           KeyExchangeSource::Default());
  std::unique_ptr<CryptoHandshakeMessage> scfg_a(
      a.AddDefaultConfig(&rand_a, &clock, options));
  std::unique_ptr<CryptoHandshakeMessage> scfg_b(
      b.AddDefaultConfig(&rand_b, &clock, options));

  quiche::QuicheStringPiece scid_a, scid_b;
  EXPECT_TRUE(scfg_a->GetStringPiece(kSCID, &scid_a));
  EXPECT_TRUE(scfg_b->GetStringPiece(kSCID, &scid_b));

  EXPECT_NE(scid_a, scid_b);
}

TEST_F(CryptoServerConfigGenerationTest, SCIDIsHashOfServerConfig) {
  MockRandom rand_a;
  const QuicCryptoServerConfig::ConfigOptions options;
  MockClock clock;

  QuicCryptoServerConfig a(QuicCryptoServerConfig::TESTING, &rand_a,
                           crypto_test_utils::ProofSourceForTesting(),
                           KeyExchangeSource::Default());
  std::unique_ptr<CryptoHandshakeMessage> scfg(
      a.AddDefaultConfig(&rand_a, &clock, options));

  quiche::QuicheStringPiece scid;
  EXPECT_TRUE(scfg->GetStringPiece(kSCID, &scid));
  // Need to take a copy of |scid| has we're about to call |Erase|.
  const std::string scid_str(scid);

  scfg->Erase(kSCID);
  scfg->MarkDirty();
  const QuicData& serialized(scfg->GetSerialized());

  uint8_t digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const uint8_t*>(serialized.data()),
         serialized.length(), digest);

  // scid is a SHA-256 hash, truncated to 16 bytes.
  ASSERT_EQ(scid.size(), 16u);
  EXPECT_EQ(0, memcmp(digest, scid_str.c_str(), scid.size()));
}

// Those tests were declared incorrectly and thus never ran in first place.
// TODO(b/147891553): figure out if we should fix or delete those.
#if 0

class CryptoServerTestNoConfig : public CryptoServerTest {
 public:
  void SetUp() override {
    // Deliberately don't add a config so that we can test this situation.
  }
};

INSTANTIATE_TEST_SUITE_P(CryptoServerTestsNoConfig,
                         CryptoServerTestNoConfig,
                         ::testing::ValuesIn(GetTestParams()),
                         ::testing::PrintToStringParamName());

TEST_P(CryptoServerTestNoConfig, DontCrash) {
  CryptoHandshakeMessage msg = crypto_test_utils::CreateCHLO(
      {{"PDMD", "X509"}, {"VER\0", client_version_string_}},
      kClientHelloMinimumSize);

  ShouldFailMentioning("No config", msg);

  const HandshakeFailureReason kRejectReasons[] = {
      SERVER_CONFIG_INCHOATE_HELLO_FAILURE};
  CheckRejectReasons(kRejectReasons, QUICHE_ARRAYSIZE(kRejectReasons));
}

class CryptoServerTestOldVersion : public CryptoServerTest {
 public:
  void SetUp() override {
    client_version_ = supported_versions_.back();
    client_version_string_ = ParsedQuicVersionToString(client_version_);
    CryptoServerTest::SetUp();
  }
};

INSTANTIATE_TEST_SUITE_P(CryptoServerTestsOldVersion,
                         CryptoServerTestOldVersion,
                         ::testing::ValuesIn(GetTestParams()),
                         ::testing::PrintToStringParamName());

TEST_P(CryptoServerTestOldVersion, ServerIgnoresXlct) {
  CryptoHandshakeMessage msg =
      crypto_test_utils::CreateCHLO({{"PDMD", "X509"},
                                     {"AEAD", "AESG"},
                                     {"KEXS", "C255"},
                                     {"SCID", scid_hex_},
                                     {"#004b5453", srct_hex_},
                                     {"PUBS", pub_hex_},
                                     {"NONC", nonce_hex_},
                                     {"VER\0", client_version_string_},
                                     {"XLCT", "#0100000000000000"}},
                                    kClientHelloMinimumSize);

  // If replay protection isn't disabled, then
  // QuicCryptoServerConfig::EvaluateClientHello will leave info.unique as false
  // and cause ProcessClientHello to exit early (and generate a REJ message).
  config_.set_replay_protection(false);

  ShouldSucceed(msg);
  EXPECT_EQ(kSHLO, out_.tag());
}

TEST_P(CryptoServerTestOldVersion, XlctNotRequired) {
  CryptoHandshakeMessage msg =
      crypto_test_utils::CreateCHLO({{"PDMD", "X509"},
                                     {"AEAD", "AESG"},
                                     {"KEXS", "C255"},
                                     {"SCID", scid_hex_},
                                     {"#004b5453", srct_hex_},
                                     {"PUBS", pub_hex_},
                                     {"NONC", nonce_hex_},
                                     {"VER\0", client_version_string_}},
                                    kClientHelloMinimumSize);

  // If replay protection isn't disabled, then
  // QuicCryptoServerConfig::EvaluateClientHello will leave info.unique as false
  // and cause ProcessClientHello to exit early (and generate a REJ message).
  config_.set_replay_protection(false);

  ShouldSucceed(msg);
  EXPECT_EQ(kSHLO, out_.tag());
}

#endif  // 0

}  // namespace test
}  // namespace quic

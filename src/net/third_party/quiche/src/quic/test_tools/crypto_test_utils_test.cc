// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"

#include <utility>

#include "net/third_party/quiche/src/quic/core/proto/crypto_server_config_proto.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

namespace quic {
namespace test {

class ShloVerifier {
 public:
  ShloVerifier(
      QuicCryptoServerConfig* crypto_config,
      QuicSocketAddress server_addr,
      QuicSocketAddress client_addr,
      const QuicClock* clock,
      QuicReferenceCountedPointer<QuicSignedServerConfig> signed_config,
      QuicCompressedCertsCache* compressed_certs_cache,
      ParsedQuicVersion version)
      : crypto_config_(crypto_config),
        server_addr_(server_addr),
        client_addr_(client_addr),
        clock_(clock),
        signed_config_(signed_config),
        compressed_certs_cache_(compressed_certs_cache),
        params_(new QuicCryptoNegotiatedParameters),
        version_(version) {}

  class ValidateClientHelloCallback : public ValidateClientHelloResultCallback {
   public:
    explicit ValidateClientHelloCallback(ShloVerifier* shlo_verifier)
        : shlo_verifier_(shlo_verifier) {}
    void Run(QuicReferenceCountedPointer<
                 ValidateClientHelloResultCallback::Result> result,
             std::unique_ptr<ProofSource::Details> /* details */) override {
      shlo_verifier_->ValidateClientHelloDone(result);
    }

   private:
    ShloVerifier* shlo_verifier_;
  };

  std::unique_ptr<ValidateClientHelloCallback>
  GetValidateClientHelloCallback() {
    return std::make_unique<ValidateClientHelloCallback>(this);
  }

 private:
  void ValidateClientHelloDone(
      const QuicReferenceCountedPointer<
          ValidateClientHelloResultCallback::Result>& result) {
    result_ = result;
    crypto_config_->ProcessClientHello(
        result_, /*reject_only=*/false,
        /*connection_id=*/TestConnectionId(1), server_addr_, client_addr_,
        version_, AllSupportedVersions(), clock_, QuicRandom::GetInstance(),
        compressed_certs_cache_, params_, signed_config_,
        /*total_framing_overhead=*/50, kDefaultMaxPacketSize,
        GetProcessClientHelloCallback());
  }

  class ProcessClientHelloCallback : public ProcessClientHelloResultCallback {
   public:
    explicit ProcessClientHelloCallback(ShloVerifier* shlo_verifier)
        : shlo_verifier_(shlo_verifier) {}
    void Run(QuicErrorCode /*error*/,
             const std::string& /*error_details*/,
             std::unique_ptr<CryptoHandshakeMessage> message,
             std::unique_ptr<DiversificationNonce> /*diversification_nonce*/,
             std::unique_ptr<ProofSource::Details> /*proof_source_details*/)
        override {
      shlo_verifier_->ProcessClientHelloDone(std::move(message));
    }

   private:
    ShloVerifier* shlo_verifier_;
  };

  std::unique_ptr<ProcessClientHelloCallback> GetProcessClientHelloCallback() {
    return std::make_unique<ProcessClientHelloCallback>(this);
  }

  void ProcessClientHelloDone(std::unique_ptr<CryptoHandshakeMessage> message) {
    // Verify output is a SHLO.
    EXPECT_EQ(message->tag(), kSHLO)
        << "Fail to pass validation. Get " << message->DebugString();
  }

  QuicCryptoServerConfig* crypto_config_;
  QuicSocketAddress server_addr_;
  QuicSocketAddress client_addr_;
  const QuicClock* clock_;
  QuicReferenceCountedPointer<QuicSignedServerConfig> signed_config_;
  QuicCompressedCertsCache* compressed_certs_cache_;

  QuicReferenceCountedPointer<QuicCryptoNegotiatedParameters> params_;
  QuicReferenceCountedPointer<ValidateClientHelloResultCallback::Result>
      result_;

  const ParsedQuicVersion version_;
};

class CryptoTestUtilsTest : public QuicTest {};

TEST_F(CryptoTestUtilsTest, TestGenerateFullCHLO) {
  MockClock clock;
  QuicCryptoServerConfig crypto_config(
      QuicCryptoServerConfig::TESTING, QuicRandom::GetInstance(),
      crypto_test_utils::ProofSourceForTesting(), KeyExchangeSource::Default());
  QuicSocketAddress server_addr(QuicIpAddress::Any4(), 5);
  QuicSocketAddress client_addr(QuicIpAddress::Loopback4(), 1);
  QuicReferenceCountedPointer<QuicSignedServerConfig> signed_config(
      new QuicSignedServerConfig);
  QuicCompressedCertsCache compressed_certs_cache(
      QuicCompressedCertsCache::kQuicCompressedCertsCacheSize);
  CryptoHandshakeMessage full_chlo;

  QuicCryptoServerConfig::ConfigOptions old_config_options;
  old_config_options.id = "old-config-id";
  crypto_config.AddDefaultConfig(QuicRandom::GetInstance(), &clock,
                                 old_config_options);
  QuicCryptoServerConfig::ConfigOptions new_config_options;
  QuicServerConfigProtobuf primary_config = crypto_config.GenerateConfig(
      QuicRandom::GetInstance(), &clock, new_config_options);
  primary_config.set_primary_time(clock.WallNow().ToUNIXSeconds());
  std::unique_ptr<CryptoHandshakeMessage> msg =
      crypto_config.AddConfig(primary_config, clock.WallNow());
  quiche::QuicheStringPiece orbit;
  ASSERT_TRUE(msg->GetStringPiece(kORBT, &orbit));
  std::string nonce;
  CryptoUtils::GenerateNonce(clock.WallNow(), QuicRandom::GetInstance(), orbit,
                             &nonce);
  std::string nonce_hex = "#" + quiche::QuicheTextUtils::HexEncode(nonce);

  char public_value[32];
  memset(public_value, 42, sizeof(public_value));
  std::string pub_hex = "#" + quiche::QuicheTextUtils::HexEncode(
                                  public_value, sizeof(public_value));

  // The methods below use a PROTOCOL_QUIC_CRYPTO version so we pick the
  // first one from the list of supported versions.
  QuicTransportVersion transport_version = QUIC_VERSION_UNSUPPORTED;
  for (const ParsedQuicVersion& version : AllSupportedVersions()) {
    if (version.handshake_protocol == PROTOCOL_QUIC_CRYPTO) {
      transport_version = version.transport_version;
      break;
    }
  }
  ASSERT_NE(QUIC_VERSION_UNSUPPORTED, transport_version);

  CryptoHandshakeMessage inchoate_chlo = crypto_test_utils::CreateCHLO(
      {{"PDMD", "X509"},
       {"AEAD", "AESG"},
       {"KEXS", "C255"},
       {"COPT", "SREJ"},
       {"PUBS", pub_hex},
       {"NONC", nonce_hex},
       {"VER\0", QuicVersionLabelToString(
                     QuicVersionToQuicVersionLabel(transport_version))}},
      kClientHelloMinimumSize);

  crypto_test_utils::GenerateFullCHLO(inchoate_chlo, &crypto_config,
                                      server_addr, client_addr,
                                      transport_version, &clock, signed_config,
                                      &compressed_certs_cache, &full_chlo);
  // Verify that full_chlo can pass crypto_config's verification.
  ShloVerifier shlo_verifier(
      &crypto_config, server_addr, client_addr, &clock, signed_config,
      &compressed_certs_cache,
      ParsedQuicVersion(PROTOCOL_QUIC_CRYPTO, transport_version));
  crypto_config.ValidateClientHello(
      full_chlo, client_addr.host(), server_addr, transport_version, &clock,
      signed_config, shlo_verifier.GetValidateClientHelloCallback());
}

}  // namespace test
}  // namespace quic

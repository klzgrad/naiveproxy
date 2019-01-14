// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/test_tools/crypto_test_utils.h"

#include "net/test/gtest_util.h"
#include "net/third_party/quic/core/crypto/crypto_server_config_protobuf.h"
#include "net/third_party/quic/core/quic_utils.h"
#include "net/third_party/quic/core/tls_server_handshaker.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quic/test_tools/mock_clock.h"


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
      QuicCompressedCertsCache* compressed_certs_cache)
      : crypto_config_(crypto_config),
        server_addr_(server_addr),
        client_addr_(client_addr),
        clock_(clock),
        signed_config_(signed_config),
        compressed_certs_cache_(compressed_certs_cache),
        params_(new QuicCryptoNegotiatedParameters) {}

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
    return QuicMakeUnique<ValidateClientHelloCallback>(this);
  }

 private:
  void ValidateClientHelloDone(
      const QuicReferenceCountedPointer<
          ValidateClientHelloResultCallback::Result>& result) {
    result_ = result;
    crypto_config_->ProcessClientHello(
        result_, /*reject_only=*/false, /*connection_id=*/1, server_addr_,
        client_addr_, AllSupportedVersions().front(), AllSupportedVersions(),
        /*use_stateless_rejects=*/true, /*server_designated_connection_id=*/0,
        clock_, QuicRandom::GetInstance(), compressed_certs_cache_, params_,
        signed_config_, /*total_framing_overhead=*/50, kDefaultMaxPacketSize,
        GetProcessClientHelloCallback());
  }

  class ProcessClientHelloCallback : public ProcessClientHelloResultCallback {
   public:
    explicit ProcessClientHelloCallback(ShloVerifier* shlo_verifier)
        : shlo_verifier_(shlo_verifier) {}
    void Run(
        QuicErrorCode error,
        const QuicString& error_details,
        std::unique_ptr<CryptoHandshakeMessage> message,
        std::unique_ptr<DiversificationNonce> diversification_nonce,
        std::unique_ptr<ProofSource::Details> proof_source_details) override {
      shlo_verifier_->ProcessClientHelloDone(std::move(message));
    }

   private:
    ShloVerifier* shlo_verifier_;
  };

  std::unique_ptr<ProcessClientHelloCallback> GetProcessClientHelloCallback() {
    return QuicMakeUnique<ProcessClientHelloCallback>(this);
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
};

class CryptoTestUtilsTest : public QuicTest {};

TEST(CryptoTestUtilsTest, TestGenerateFullCHLO) {
  MockClock clock;
  QuicCryptoServerConfig crypto_config(
      QuicCryptoServerConfig::TESTING, QuicRandom::GetInstance(),
      crypto_test_utils::ProofSourceForTesting(), KeyExchangeSource::Default(),
      TlsServerHandshaker::CreateSslCtx());
  QuicSocketAddress server_addr;
  QuicSocketAddress client_addr(QuicIpAddress::Loopback4(), 1);
  QuicReferenceCountedPointer<QuicSignedServerConfig> signed_config(
      new QuicSignedServerConfig);
  QuicCompressedCertsCache compressed_certs_cache(
      QuicCompressedCertsCache::kQuicCompressedCertsCacheSize);
  CryptoHandshakeMessage full_chlo;

  QuicCryptoServerConfig::ConfigOptions old_config_options;
  old_config_options.id = "old-config-id";
  delete crypto_config.AddDefaultConfig(QuicRandom::GetInstance(), &clock,
                                        old_config_options);
  QuicCryptoServerConfig::ConfigOptions new_config_options;
  std::unique_ptr<QuicServerConfigProtobuf> primary_config(
      crypto_config.GenerateConfig(QuicRandom::GetInstance(), &clock,
                                   new_config_options));
  primary_config->set_primary_time(clock.WallNow().ToUNIXSeconds());
  std::unique_ptr<CryptoHandshakeMessage> msg(
      crypto_config.AddConfig(std::move(primary_config), clock.WallNow()));
  QuicStringPiece orbit;
  ASSERT_TRUE(msg->GetStringPiece(kORBT, &orbit));
  QuicString nonce;
  CryptoUtils::GenerateNonce(
      clock.WallNow(), QuicRandom::GetInstance(),
      QuicStringPiece(reinterpret_cast<const char*>(orbit.data()),
                      sizeof(orbit.size())),
      &nonce);
  QuicString nonce_hex = "#" + QuicTextUtils::HexEncode(nonce);

  char public_value[32];
  memset(public_value, 42, sizeof(public_value));
  QuicString pub_hex =
      "#" + QuicTextUtils::HexEncode(public_value, sizeof(public_value));

  QuicTransportVersion version(AllSupportedTransportVersions().front());
  CryptoHandshakeMessage inchoate_chlo = crypto_test_utils::CreateCHLO(
      {{"PDMD", "X509"},
       {"AEAD", "AESG"},
       {"KEXS", "C255"},
       {"COPT", "SREJ"},
       {"PUBS", pub_hex},
       {"NONC", nonce_hex},
       {"VER\0",
        QuicVersionLabelToString(QuicVersionToQuicVersionLabel(version))}},
      kClientHelloMinimumSize);

  crypto_test_utils::GenerateFullCHLO(
      inchoate_chlo, &crypto_config, server_addr, client_addr, version, &clock,
      signed_config, &compressed_certs_cache, &full_chlo);
  // Verify that full_chlo can pass crypto_config's verification.
  ShloVerifier shlo_verifier(&crypto_config, server_addr, client_addr, &clock,
                             signed_config, &compressed_certs_cache);
  crypto_config.ValidateClientHello(
      full_chlo, client_addr.host(), server_addr, version, &clock,
      signed_config, shlo_verifier.GetValidateClientHelloCallback());
}

}  // namespace test
}  // namespace quic

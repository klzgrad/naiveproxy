// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/crypto/proof_source_x509.h"

#include <memory>

#include "absl/strings/string_view.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "quic/core/crypto/certificate_view.h"
#include "quic/core/crypto/proof_source.h"
#include "quic/platform/api/quic_expect_bug.h"
#include "quic/platform/api/quic_ip_address.h"
#include "quic/platform/api/quic_reference_counted.h"
#include "quic/platform/api/quic_socket_address.h"
#include "quic/platform/api/quic_test.h"
#include "quic/test_tools/test_certificates.h"

namespace quic {
namespace test {
namespace {

QuicReferenceCountedPointer<ProofSource::Chain> MakeChain(
    absl::string_view cert) {
  return QuicReferenceCountedPointer<ProofSource::Chain>(
      new ProofSource::Chain(std::vector<std::string>{std::string(cert)}));
}

class ProofSourceX509Test : public QuicTest {
 public:
  ProofSourceX509Test()
      : test_chain_(MakeChain(kTestCertificate)),
        wildcard_chain_(MakeChain(kWildcardCertificate)),
        test_key_(
            CertificatePrivateKey::LoadFromDer(kTestCertificatePrivateKey)),
        wildcard_key_(CertificatePrivateKey::LoadFromDer(
            kWildcardCertificatePrivateKey)) {
    QUICHE_CHECK(test_key_ != nullptr);
    QUICHE_CHECK(wildcard_key_ != nullptr);
  }

 protected:
  QuicReferenceCountedPointer<ProofSource::Chain> test_chain_, wildcard_chain_;
  std::unique_ptr<CertificatePrivateKey> test_key_, wildcard_key_;
};

TEST_F(ProofSourceX509Test, AddCertificates) {
  std::unique_ptr<ProofSourceX509> proof_source =
      ProofSourceX509::Create(test_chain_, std::move(*test_key_));
  ASSERT_TRUE(proof_source != nullptr);
  EXPECT_TRUE(proof_source->AddCertificateChain(wildcard_chain_,
                                                std::move(*wildcard_key_)));
}

TEST_F(ProofSourceX509Test, AddCertificateKeyMismatch) {
  std::unique_ptr<ProofSourceX509> proof_source =
      ProofSourceX509::Create(test_chain_, std::move(*test_key_));
  ASSERT_TRUE(proof_source != nullptr);
  test_key_ = CertificatePrivateKey::LoadFromDer(kTestCertificatePrivateKey);
  bool result;
  EXPECT_QUIC_BUG(result = proof_source->AddCertificateChain(
                      wildcard_chain_, std::move(*test_key_)),
                  "Private key does not match");
}

TEST_F(ProofSourceX509Test, CertificateSelection) {
  std::unique_ptr<ProofSourceX509> proof_source =
      ProofSourceX509::Create(test_chain_, std::move(*test_key_));
  ASSERT_TRUE(proof_source != nullptr);
  ASSERT_TRUE(proof_source->AddCertificateChain(wildcard_chain_,
                                                std::move(*wildcard_key_)));

  // Default certificate.
  EXPECT_EQ(proof_source
                ->GetCertChain(QuicSocketAddress(), QuicSocketAddress(),
                               "unknown.test")
                ->certs[0],
            kTestCertificate);
  // mail.example.org is explicitly a SubjectAltName in kTestCertificate.
  EXPECT_EQ(proof_source
                ->GetCertChain(QuicSocketAddress(), QuicSocketAddress(),
                               "mail.example.org")
                ->certs[0],
            kTestCertificate);
  // www.foo.test is in kWildcardCertificate.
  EXPECT_EQ(proof_source
                ->GetCertChain(QuicSocketAddress(), QuicSocketAddress(),
                               "www.foo.test")
                ->certs[0],
            kWildcardCertificate);
  // *.wildcard.test is in kWildcardCertificate.
  EXPECT_EQ(proof_source
                ->GetCertChain(QuicSocketAddress(), QuicSocketAddress(),
                               "www.wildcard.test")
                ->certs[0],
            kWildcardCertificate);
  EXPECT_EQ(proof_source
                ->GetCertChain(QuicSocketAddress(), QuicSocketAddress(),
                               "etc.wildcard.test")
                ->certs[0],
            kWildcardCertificate);
  // wildcard.test itself is not in kWildcardCertificate.
  EXPECT_EQ(proof_source
                ->GetCertChain(QuicSocketAddress(), QuicSocketAddress(),
                               "wildcard.test")
                ->certs[0],
            kTestCertificate);
}

TEST_F(ProofSourceX509Test, TlsSignature) {
  class Callback : public ProofSource::SignatureCallback {
   public:
    void Run(bool ok,
             std::string signature,
             std::unique_ptr<ProofSource::Details> /*details*/) override {
      ASSERT_TRUE(ok);
      std::unique_ptr<CertificateView> view =
          CertificateView::ParseSingleCertificate(kTestCertificate);
      EXPECT_TRUE(view->VerifySignature("Test data", signature,
                                        SSL_SIGN_RSA_PSS_RSAE_SHA256));
    }
  };

  std::unique_ptr<ProofSourceX509> proof_source =
      ProofSourceX509::Create(test_chain_, std::move(*test_key_));
  ASSERT_TRUE(proof_source != nullptr);

  proof_source->ComputeTlsSignature(QuicSocketAddress(), QuicSocketAddress(),
                                    "example.com", SSL_SIGN_RSA_PSS_RSAE_SHA256,
                                    "Test data", std::make_unique<Callback>());
}

}  // namespace
}  // namespace test
}  // namespace quic

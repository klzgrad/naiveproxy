// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/certificate_view.h"

#include <memory>
#include <sstream>

#include "third_party/boringssl/src/include/openssl/base.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/test_certificates.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace test {
namespace {

using testing::ElementsAre;
using testing::HasSubstr;

TEST(CertificateViewTest, PemParser) {
  std::stringstream stream(kTestCertificatePem);
  PemReadResult result = ReadNextPemMessage(&stream);
  EXPECT_EQ(result.status, PemReadResult::kOk);
  EXPECT_EQ(result.type, "CERTIFICATE");
  EXPECT_EQ(result.contents, kTestCertificate);

  result = ReadNextPemMessage(&stream);
  EXPECT_EQ(result.status, PemReadResult::kEof);
}

TEST(CertificateViewTest, Parse) {
  std::unique_ptr<CertificateView> view =
      CertificateView::ParseSingleCertificate(kTestCertificate);
  ASSERT_TRUE(view != nullptr);

  EXPECT_THAT(view->subject_alt_name_domains(),
              ElementsAre(quiche::QuicheStringPiece("www.example.org"),
                          quiche::QuicheStringPiece("mail.example.org"),
                          quiche::QuicheStringPiece("mail.example.com")));
  EXPECT_THAT(view->subject_alt_name_ips(),
              ElementsAre(QuicIpAddress::Loopback4()));
  EXPECT_EQ(EVP_PKEY_id(view->public_key()), EVP_PKEY_RSA);
}

TEST(CertificateViewTest, PemSingleCertificate) {
  std::stringstream pem_stream(kTestCertificatePem);
  std::vector<std::string> chain =
      CertificateView::LoadPemFromStream(&pem_stream);
  EXPECT_THAT(chain, ElementsAre(kTestCertificate));
}

TEST(CertificateViewTest, PemMultipleCertificates) {
  std::stringstream pem_stream(kTestCertificateChainPem);
  std::vector<std::string> chain =
      CertificateView::LoadPemFromStream(&pem_stream);
  EXPECT_THAT(chain,
              ElementsAre(kTestCertificate, HasSubstr("QUIC Server Root CA")));
}

TEST(CertificateViewTest, PemNoCertificates) {
  std::stringstream pem_stream("one\ntwo\nthree\n");
  std::vector<std::string> chain =
      CertificateView::LoadPemFromStream(&pem_stream);
  EXPECT_TRUE(chain.empty());
}

TEST(CertificateViewTest, SignAndVerify) {
  std::unique_ptr<CertificatePrivateKey> key =
      CertificatePrivateKey::LoadFromDer(kTestCertificatePrivateKey);
  ASSERT_TRUE(key != nullptr);

  std::string data = "A really important message";
  std::string signature = key->Sign(data, SSL_SIGN_RSA_PSS_RSAE_SHA256);
  ASSERT_FALSE(signature.empty());

  std::unique_ptr<CertificateView> view =
      CertificateView::ParseSingleCertificate(kTestCertificate);
  ASSERT_TRUE(view != nullptr);
  EXPECT_TRUE(key->MatchesPublicKey(*view));

  EXPECT_TRUE(
      view->VerifySignature(data, signature, SSL_SIGN_RSA_PSS_RSAE_SHA256));
  EXPECT_FALSE(view->VerifySignature("An unimportant message", signature,
                                     SSL_SIGN_RSA_PSS_RSAE_SHA256));
  EXPECT_FALSE(view->VerifySignature(data, "Not a signature",
                                     SSL_SIGN_RSA_PSS_RSAE_SHA256));
}

TEST(CertificateViewTest, PrivateKeyPem) {
  std::unique_ptr<CertificateView> view =
      CertificateView::ParseSingleCertificate(kTestCertificate);
  ASSERT_TRUE(view != nullptr);

  std::stringstream pem_stream(kTestCertificatePrivateKeyPem);
  std::unique_ptr<CertificatePrivateKey> pem_key =
      CertificatePrivateKey::LoadPemFromStream(&pem_stream);
  ASSERT_TRUE(pem_key != nullptr);
  EXPECT_TRUE(pem_key->MatchesPublicKey(*view));

  std::stringstream legacy_stream(kTestCertificatePrivateKeyLegacyPem);
  std::unique_ptr<CertificatePrivateKey> legacy_key =
      CertificatePrivateKey::LoadPemFromStream(&legacy_stream);
  ASSERT_TRUE(legacy_key != nullptr);
  EXPECT_TRUE(legacy_key->MatchesPublicKey(*view));
}

}  // namespace
}  // namespace test
}  // namespace quic

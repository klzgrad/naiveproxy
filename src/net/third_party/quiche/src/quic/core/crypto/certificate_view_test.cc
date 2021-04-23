// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/crypto/certificate_view.h"

#include <memory>
#include <sstream>

#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "third_party/boringssl/src/include/openssl/base.h"
#include "third_party/boringssl/src/include/openssl/bytestring.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "quic/core/crypto/boring_utils.h"
#include "quic/core/quic_time.h"
#include "quic/platform/api/quic_ip_address.h"
#include "quic/platform/api/quic_test.h"
#include "quic/test_tools/test_certificates.h"
#include "common/platform/api/quiche_time_utils.h"

namespace quic {
namespace test {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::Optional;

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
              ElementsAre(absl::string_view("www.example.org"),
                          absl::string_view("mail.example.org"),
                          absl::string_view("mail.example.com")));
  EXPECT_THAT(view->subject_alt_name_ips(),
              ElementsAre(QuicIpAddress::Loopback4()));
  EXPECT_EQ(EVP_PKEY_id(view->public_key()), EVP_PKEY_RSA);

  const QuicWallTime validity_start = QuicWallTime::FromUNIXSeconds(
      *quiche::QuicheUtcDateTimeToUnixSeconds(2020, 1, 30, 18, 13, 59));
  EXPECT_EQ(view->validity_start(), validity_start);
  const QuicWallTime validity_end = QuicWallTime::FromUNIXSeconds(
      *quiche::QuicheUtcDateTimeToUnixSeconds(2020, 2, 2, 18, 13, 59));
  EXPECT_EQ(view->validity_end(), validity_end);

  EXPECT_EQ("C=US,ST=California,L=Mountain View,O=QUIC Server,CN=127.0.0.1",
            view->GetHumanReadableSubject());
}

TEST(CertificateViewTest, ParseCertWithUnknownSanType) {
  std::stringstream stream(kTestCertWithUnknownSanTypePem);
  PemReadResult result = ReadNextPemMessage(&stream);
  EXPECT_EQ(result.status, PemReadResult::kOk);
  EXPECT_EQ(result.type, "CERTIFICATE");

  std::unique_ptr<CertificateView> view =
      CertificateView::ParseSingleCertificate(result.contents);
  EXPECT_TRUE(view != nullptr);
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

TEST(CertificateViewTest, PrivateKeyEcdsaPem) {
  std::stringstream pem_stream(kTestEcPrivateKeyLegacyPem);
  std::unique_ptr<CertificatePrivateKey> key =
      CertificatePrivateKey::LoadPemFromStream(&pem_stream);
  ASSERT_TRUE(key != nullptr);
  EXPECT_TRUE(key->ValidForSignatureAlgorithm(SSL_SIGN_ECDSA_SECP256R1_SHA256));
}

TEST(CertificateViewTest, DerTime) {
  EXPECT_THAT(ParseDerTime(CBS_ASN1_GENERALIZEDTIME, "19700101000024Z"),
              Optional(QuicWallTime::FromUNIXSeconds(24)));
  EXPECT_THAT(ParseDerTime(CBS_ASN1_GENERALIZEDTIME, "19710101000024Z"),
              Optional(QuicWallTime::FromUNIXSeconds(365 * 86400 + 24)));
  EXPECT_THAT(ParseDerTime(CBS_ASN1_UTCTIME, "700101000024Z"),
              Optional(QuicWallTime::FromUNIXSeconds(24)));
  EXPECT_TRUE(ParseDerTime(CBS_ASN1_UTCTIME, "200101000024Z").has_value());

  EXPECT_EQ(ParseDerTime(CBS_ASN1_GENERALIZEDTIME, ""), absl::nullopt);
  EXPECT_EQ(ParseDerTime(CBS_ASN1_GENERALIZEDTIME, "19700101000024.001Z"),
            absl::nullopt);
  EXPECT_EQ(ParseDerTime(CBS_ASN1_GENERALIZEDTIME, "19700101000024Q"),
            absl::nullopt);
  EXPECT_EQ(ParseDerTime(CBS_ASN1_GENERALIZEDTIME, "19700101000024-0500"),
            absl::nullopt);
  EXPECT_EQ(ParseDerTime(CBS_ASN1_GENERALIZEDTIME, "700101000024ZZ"),
            absl::nullopt);
  EXPECT_EQ(ParseDerTime(CBS_ASN1_GENERALIZEDTIME, "19700101000024.00Z"),
            absl::nullopt);
  EXPECT_EQ(ParseDerTime(CBS_ASN1_GENERALIZEDTIME, "19700101000024.Z"),
            absl::nullopt);
  EXPECT_EQ(ParseDerTime(CBS_ASN1_GENERALIZEDTIME, "197O0101000024Z"),
            absl::nullopt);
  EXPECT_EQ(ParseDerTime(CBS_ASN1_GENERALIZEDTIME, "19700101000024.0O1Z"),
            absl::nullopt);
  EXPECT_EQ(ParseDerTime(CBS_ASN1_GENERALIZEDTIME, "-9700101000024Z"),
            absl::nullopt);
  EXPECT_EQ(ParseDerTime(CBS_ASN1_GENERALIZEDTIME, "1970-101000024Z"),
            absl::nullopt);

  EXPECT_TRUE(ParseDerTime(CBS_ASN1_UTCTIME, "490101000024Z").has_value());
  // This should parse as 1950, which predates UNIX epoch.
  EXPECT_FALSE(ParseDerTime(CBS_ASN1_UTCTIME, "500101000024Z").has_value());

  EXPECT_THAT(ParseDerTime(CBS_ASN1_GENERALIZEDTIME, "19700101230000Z"),
              Optional(QuicWallTime::FromUNIXSeconds(23 * 3600)));
  EXPECT_EQ(ParseDerTime(CBS_ASN1_GENERALIZEDTIME, "19700101240000Z"),
            absl::nullopt);
}

TEST(CertificateViewTest, NameAttribute) {
  // OBJECT_IDENTIFIER { 1.2.840.113554.4.1.112411 }
  // UTF8String { "Test" }
  std::string unknown_oid =
      absl::HexStringToBytes("060b2a864886f712040186ee1b0c0454657374");
  EXPECT_EQ("1.2.840.113554.4.1.112411=Test",
            X509NameAttributeToString(StringPieceToCbs(unknown_oid)));

  // OBJECT_IDENTIFIER { 2.5.4.3 }
  // UTF8String { "Bell: \x07" }
  std::string non_printable =
      absl::HexStringToBytes("06035504030c0742656c6c3a2007");
  EXPECT_EQ(R"(CN=Bell: \x07)",
            X509NameAttributeToString(StringPieceToCbs(non_printable)));

  // OBJECT_IDENTIFIER { "\x55\x80" }
  // UTF8String { "Test" }
  std::string invalid_oid = absl::HexStringToBytes("060255800c0454657374");
  EXPECT_EQ("(5580)=Test",
            X509NameAttributeToString(StringPieceToCbs(invalid_oid)));
}

}  // namespace
}  // namespace test
}  // namespace quic

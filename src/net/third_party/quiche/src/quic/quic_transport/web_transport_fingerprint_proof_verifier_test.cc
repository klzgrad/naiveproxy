// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/quic_transport/web_transport_fingerprint_proof_verifier.h"

#include <memory>

#include "absl/strings/string_view.h"
#include "quic/core/quic_types.h"
#include "quic/platform/api/quic_test.h"
#include "quic/test_tools/mock_clock.h"
#include "quic/test_tools/test_certificates.h"

namespace quic {
namespace test {
namespace {

using ::testing::HasSubstr;

// 2020-02-01 12:35:56 UTC
constexpr QuicTime::Delta kValidTime = QuicTime::Delta::FromSeconds(1580560556);

struct VerifyResult {
  QuicAsyncStatus status;
  WebTransportFingerprintProofVerifier::Status detailed_status;
  std::string error;
};

class WebTransportFingerprintProofVerifierTest : public QuicTest {
 public:
  WebTransportFingerprintProofVerifierTest() {
    clock_.AdvanceTime(kValidTime);
    verifier_ = std::make_unique<WebTransportFingerprintProofVerifier>(
        &clock_, /*max_validity_days=*/365);
    AddTestCertificate();
  }

 protected:
  VerifyResult Verify(absl::string_view certificate) {
    VerifyResult result;
    std::unique_ptr<ProofVerifyDetails> details;
    uint8_t tls_alert;
    result.status = verifier_->VerifyCertChain(
        /*hostname=*/"", /*port=*/0,
        std::vector<std::string>{std::string(certificate)},
        /*ocsp_response=*/"",
        /*cert_sct=*/"",
        /*context=*/nullptr, &result.error, &details, &tls_alert,
        /*callback=*/nullptr);
    result.detailed_status =
        static_cast<WebTransportFingerprintProofVerifier::Details*>(
            details.get())
            ->status();
    return result;
  }

  void AddTestCertificate() {
    EXPECT_TRUE(verifier_->AddFingerprint(
        CertificateFingerprint{CertificateFingerprint::kSha256,
                               ComputeSha256Fingerprint(kTestCertificate)}));
  }

  MockClock clock_;
  std::unique_ptr<WebTransportFingerprintProofVerifier> verifier_;
};

TEST_F(WebTransportFingerprintProofVerifierTest, Sha256Fingerprint) {
  // Computed using `openssl x509 -fingerprint -sha256`.
  EXPECT_EQ(ComputeSha256Fingerprint(kTestCertificate),
            "f2:e5:46:5e:2b:f7:ec:d6:f6:30:66:a5:a3:75:11:73:4a:a0:eb:7c:47:01:"
            "0e:86:d6:75:8e:d4:f4:fa:1b:0f");
}

TEST_F(WebTransportFingerprintProofVerifierTest, SimpleFingerprint) {
  VerifyResult result = Verify(kTestCertificate);
  EXPECT_EQ(result.status, QUIC_SUCCESS);
  EXPECT_EQ(result.detailed_status,
            WebTransportFingerprintProofVerifier::Status::kValidCertificate);

  result = Verify(kWildcardCertificate);
  EXPECT_EQ(result.status, QUIC_FAILURE);
  EXPECT_EQ(result.detailed_status,
            WebTransportFingerprintProofVerifier::Status::kUnknownFingerprint);

  result = Verify("Some random text");
  EXPECT_EQ(result.status, QUIC_FAILURE);
}

TEST_F(WebTransportFingerprintProofVerifierTest, Validity) {
  // Validity periods of kTestCertificate, according to `openssl x509 -text`:
  //     Not Before: Jan 30 18:13:59 2020 GMT
  //     Not After : Feb  2 18:13:59 2020 GMT

  // 2020-01-29 19:00:00 UTC
  constexpr QuicTime::Delta kStartTime =
      QuicTime::Delta::FromSeconds(1580324400);
  clock_.Reset();
  clock_.AdvanceTime(kStartTime);

  VerifyResult result = Verify(kTestCertificate);
  EXPECT_EQ(result.status, QUIC_FAILURE);
  EXPECT_EQ(result.detailed_status,
            WebTransportFingerprintProofVerifier::Status::kExpired);

  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(86400));
  result = Verify(kTestCertificate);
  EXPECT_EQ(result.status, QUIC_SUCCESS);
  EXPECT_EQ(result.detailed_status,
            WebTransportFingerprintProofVerifier::Status::kValidCertificate);

  clock_.AdvanceTime(QuicTime::Delta::FromSeconds(4 * 86400));
  result = Verify(kTestCertificate);
  EXPECT_EQ(result.status, QUIC_FAILURE);
  EXPECT_EQ(result.detailed_status,
            WebTransportFingerprintProofVerifier::Status::kExpired);
}

TEST_F(WebTransportFingerprintProofVerifierTest, MaxValidity) {
  verifier_ = std::make_unique<WebTransportFingerprintProofVerifier>(
      &clock_, /*max_validity_days=*/2);
  AddTestCertificate();
  VerifyResult result = Verify(kTestCertificate);
  EXPECT_EQ(result.status, QUIC_FAILURE);
  EXPECT_EQ(result.detailed_status,
            WebTransportFingerprintProofVerifier::Status::kExpiryTooLong);
  EXPECT_THAT(result.error, HasSubstr("limit of 2 days"));

  // kTestCertificate is valid for exactly four days.
  verifier_ = std::make_unique<WebTransportFingerprintProofVerifier>(
      &clock_, /*max_validity_days=*/4);
  AddTestCertificate();
  result = Verify(kTestCertificate);
  EXPECT_EQ(result.status, QUIC_SUCCESS);
  EXPECT_EQ(result.detailed_status,
            WebTransportFingerprintProofVerifier::Status::kValidCertificate);
}

TEST_F(WebTransportFingerprintProofVerifierTest, InvalidCertificate) {
  constexpr absl::string_view kInvalidCertificate = "Hello, world!";
  ASSERT_TRUE(verifier_->AddFingerprint(
      {CertificateFingerprint::kSha256,
       ComputeSha256Fingerprint(kInvalidCertificate)}));

  VerifyResult result = Verify(kInvalidCertificate);
  EXPECT_EQ(result.status, QUIC_FAILURE);
  EXPECT_EQ(
      result.detailed_status,
      WebTransportFingerprintProofVerifier::Status::kCertificateParseFailure);
}

TEST_F(WebTransportFingerprintProofVerifierTest, AddCertificate) {
  // Accept all-uppercase fingerprints.
  verifier_ = std::make_unique<WebTransportFingerprintProofVerifier>(
      &clock_, /*max_validity_days=*/365);
  EXPECT_TRUE(verifier_->AddFingerprint(
      {CertificateFingerprint::kSha256,
       "F2:E5:46:5E:2B:F7:EC:D6:F6:30:66:A5:A3:75:11:73:4A:A0:EB:"
       "7C:47:01:0E:86:D6:75:8E:D4:F4:FA:1B:0F"}));
  EXPECT_EQ(Verify(kTestCertificate).detailed_status,
            WebTransportFingerprintProofVerifier::Status::kValidCertificate);

  // Reject unknown hash algorithms.
  EXPECT_FALSE(verifier_->AddFingerprint(
      {"sha-1",
       "00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00"}));
  // Reject invalid length.
  EXPECT_FALSE(verifier_->AddFingerprint(
      {CertificateFingerprint::kSha256, "00:00:00:00"}));
  // Reject missing colons.
  EXPECT_FALSE(verifier_->AddFingerprint(
      {CertificateFingerprint::kSha256,
       "00.00.00.00.00.00.00.00.00.00.00.00.00.00.00.00.00.00.00."
       "00.00.00.00.00.00.00.00.00.00.00.00.00"}));
  // Reject non-hex symbols.
  EXPECT_FALSE(verifier_->AddFingerprint(
      {CertificateFingerprint::kSha256,
       "zz:zz:zz:zz:zz:zz:zz:zz:zz:zz:zz:zz:zz:zz:zz:zz:zz:zz:zz:"
       "zz:zz:zz:zz:zz:zz:zz:zz:zz:zz:zz:zz:zz"}));
}

}  // namespace
}  // namespace test
}  // namespace quic

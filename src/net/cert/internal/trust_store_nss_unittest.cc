// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/internal/trust_store_nss.h"

#include <cert.h>
#include <certdb.h>

#include <memory>

#include "base/strings/string_number_conversions.h"
#include "crypto/scoped_test_nss_db.h"
#include "net/cert/internal/cert_issuer_source_sync_unittest.h"
#include "net/cert/internal/test_helpers.h"
#include "net/cert/scoped_nss_types.h"
#include "net/cert/x509_util_nss.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

class TrustStoreNSSTest : public testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(test_nssdb_.is_open());
    ParsedCertificateList chain;
    ReadCertChainFromFile(
        "net/data/verify_certificate_chain_unittest/key-rollover/oldchain.pem",
        &chain);

    ASSERT_EQ(3U, chain.size());
    target_ = chain[0];
    oldintermediate_ = chain[1];
    oldroot_ = chain[2];
    ASSERT_TRUE(target_);
    ASSERT_TRUE(oldintermediate_);
    ASSERT_TRUE(oldroot_);

    ReadCertChainFromFile(
        "net/data/verify_certificate_chain_unittest/"
        "key-rollover/longrolloverchain.pem",
        &chain);

    ASSERT_EQ(5U, chain.size());
    newintermediate_ = chain[1];
    newroot_ = chain[2];
    newrootrollover_ = chain[3];
    ASSERT_TRUE(newintermediate_);
    ASSERT_TRUE(newroot_);
    ASSERT_TRUE(newrootrollover_);

    trust_store_nss_.reset(new TrustStoreNSS(trustSSL));
  }

  std::string GetUniqueNickname() {
    return "trust_store_nss_unittest" + base::UintToString(nickname_counter_++);
  }

  void AddCertToNSS(const ParsedCertificate* cert) {
    ScopedCERTCertificate nss_cert(x509_util::CreateCERTCertificateFromBytes(
        cert->der_cert().UnsafeData(), cert->der_cert().Length()));
    ASSERT_TRUE(nss_cert);
    SECStatus srv = PK11_ImportCert(
        test_nssdb_.slot(), nss_cert.get(), CK_INVALID_HANDLE,
        GetUniqueNickname().c_str(), PR_FALSE /* includeTrust (unused) */);
    ASSERT_EQ(SECSuccess, srv);
  }

  void AddCertsToNSS() {
    AddCertToNSS(target_.get());
    AddCertToNSS(oldintermediate_.get());
    AddCertToNSS(newintermediate_.get());
    AddCertToNSS(oldroot_.get());
    AddCertToNSS(newroot_.get());
    AddCertToNSS(newrootrollover_.get());

    // Check that the certificates can be retrieved as expected.
    EXPECT_TRUE(
        TrustStoreContains(target_, {newintermediate_, oldintermediate_}));

    EXPECT_TRUE(TrustStoreContains(newintermediate_,
                                   {newroot_, newrootrollover_, oldroot_}));
    EXPECT_TRUE(TrustStoreContains(oldintermediate_,
                                   {newroot_, newrootrollover_, oldroot_}));
    EXPECT_TRUE(TrustStoreContains(newrootrollover_,
                                   {newroot_, newrootrollover_, oldroot_}));
    EXPECT_TRUE(
        TrustStoreContains(oldroot_, {newroot_, newrootrollover_, oldroot_}));
    EXPECT_TRUE(
        TrustStoreContains(newroot_, {newroot_, newrootrollover_, oldroot_}));
  }

  // Trusts |cert|. Assumes the cert was already imported into NSS.
  void TrustCert(const ParsedCertificate* cert) {
    ChangeCertTrust(cert, CERTDB_TRUSTED_CA | CERTDB_VALID_CA);
  }

  // Trusts |cert| as a server, but not as a CA. Assumes the cert was already
  // imported into NSS.
  void TrustServerCert(const ParsedCertificate* cert) {
    ChangeCertTrust(cert, CERTDB_TERMINAL_RECORD | CERTDB_TRUSTED);
  }

  // Distrusts |cert|. Assumes the cert was already imported into NSS.
  void DistrustCert(const ParsedCertificate* cert) {
    ChangeCertTrust(cert, CERTDB_TERMINAL_RECORD);
  }

  void ChangeCertTrust(const ParsedCertificate* cert, int flags) {
    SECItem der_cert;
    der_cert.data = const_cast<uint8_t*>(cert->der_cert().UnsafeData());
    der_cert.len = base::checked_cast<unsigned>(cert->der_cert().Length());
    der_cert.type = siDERCertBuffer;

    ScopedCERTCertificate nss_cert(
        CERT_FindCertByDERCert(CERT_GetDefaultCertDB(), &der_cert));
    ASSERT_TRUE(nss_cert);

    CERTCertTrust trust = {0};
    trust.sslFlags = flags;
    SECStatus srv =
        CERT_ChangeCertTrust(CERT_GetDefaultCertDB(), nss_cert.get(), &trust);
    ASSERT_EQ(SECSuccess, srv);
  }

 protected:
  bool TrustStoreContains(scoped_refptr<ParsedCertificate> cert,
                          ParsedCertificateList expected_matches) {
    ParsedCertificateList matches;
    trust_store_nss_->SyncGetIssuersOf(cert.get(), &matches);

    std::vector<std::string> name_result_matches;
    for (const auto& it : matches)
      name_result_matches.push_back(GetCertString(it));
    std::sort(name_result_matches.begin(), name_result_matches.end());

    std::vector<std::string> name_expected_matches;
    for (const auto& it : expected_matches)
      name_expected_matches.push_back(GetCertString(it));
    std::sort(name_expected_matches.begin(), name_expected_matches.end());

    if (name_expected_matches == name_result_matches)
      return true;

    // Print some extra information for debugging.
    EXPECT_EQ(name_expected_matches, name_result_matches);
    return false;
  }

  // Give simpler names to certificate DER (for identifying them in tests by
  // their symbolic name).
  std::string GetCertString(
      const scoped_refptr<ParsedCertificate>& cert) const {
    if (cert->der_cert() == oldroot_->der_cert())
      return "oldroot_";
    if (cert->der_cert() == newroot_->der_cert())
      return "newroot_";
    if (cert->der_cert() == target_->der_cert())
      return "target_";
    if (cert->der_cert() == oldintermediate_->der_cert())
      return "oldintermediate_";
    if (cert->der_cert() == newintermediate_->der_cert())
      return "newintermediate_";
    if (cert->der_cert() == newrootrollover_->der_cert())
      return "newrootrollover_";
    return cert->der_cert().AsString();
  }

  bool HasTrust(const ParsedCertificateList& certs,
                CertificateTrustType expected_trust) {
    bool success = true;
    for (const scoped_refptr<ParsedCertificate>& cert : certs) {
      CertificateTrust trust;
      trust_store_nss_->GetTrust(cert.get(), &trust);
      if (trust.type != expected_trust) {
        EXPECT_EQ(expected_trust, trust.type) << GetCertString(cert);
        success = false;
      }
    }

    return success;
  }

  scoped_refptr<ParsedCertificate> oldroot_;
  scoped_refptr<ParsedCertificate> newroot_;

  scoped_refptr<ParsedCertificate> target_;
  scoped_refptr<ParsedCertificate> oldintermediate_;
  scoped_refptr<ParsedCertificate> newintermediate_;
  scoped_refptr<ParsedCertificate> newrootrollover_;
  crypto::ScopedTestNSSDB test_nssdb_;
  std::unique_ptr<TrustStoreNSS> trust_store_nss_;
  unsigned nickname_counter_ = 0;
};

// Without adding any certs to the NSS DB, should get no anchor results for any
// of the test certs.
TEST_F(TrustStoreNSSTest, CertsNotPresent) {
  EXPECT_TRUE(TrustStoreContains(target_, ParsedCertificateList()));
  EXPECT_TRUE(TrustStoreContains(newintermediate_, ParsedCertificateList()));
  EXPECT_TRUE(TrustStoreContains(newroot_, ParsedCertificateList()));
}

// If certs are present in NSS DB but aren't marked as trusted, should get no
// anchor results for any of the test certs.
TEST_F(TrustStoreNSSTest, CertsPresentButNotTrusted) {
  AddCertsToNSS();

  // None of the certificates are trusted.
  EXPECT_TRUE(HasTrust({oldroot_, newroot_, target_, oldintermediate_,
                        newintermediate_, newrootrollover_},
                       CertificateTrustType::UNSPECIFIED));
}

// Trust a single self-signed CA certificate.
TEST_F(TrustStoreNSSTest, TrustedCA) {
  AddCertsToNSS();
  TrustCert(newroot_.get());

  // Only one of the certificates are trusted.
  EXPECT_TRUE(HasTrust(
      {oldroot_, target_, oldintermediate_, newintermediate_, newrootrollover_},
      CertificateTrustType::UNSPECIFIED));

  EXPECT_TRUE(HasTrust({newroot_}, CertificateTrustType::TRUSTED_ANCHOR));
}

// Distrust a single self-signed CA certificate.
TEST_F(TrustStoreNSSTest, DistrustedCA) {
  AddCertsToNSS();
  DistrustCert(newroot_.get());

  // Only one of the certificates are trusted.
  EXPECT_TRUE(HasTrust(
      {oldroot_, target_, oldintermediate_, newintermediate_, newrootrollover_},
      CertificateTrustType::UNSPECIFIED));

  EXPECT_TRUE(HasTrust({newroot_}, CertificateTrustType::DISTRUSTED));
}

// Trust a single intermediate certificate.
TEST_F(TrustStoreNSSTest, TrustedIntermediate) {
  AddCertsToNSS();
  TrustCert(newintermediate_.get());

  EXPECT_TRUE(HasTrust(
      {oldroot_, newroot_, target_, oldintermediate_, newrootrollover_},
      CertificateTrustType::UNSPECIFIED));
  EXPECT_TRUE(
      HasTrust({newintermediate_}, CertificateTrustType::TRUSTED_ANCHOR));
}

// Distrust a single intermediate certificate.
TEST_F(TrustStoreNSSTest, DistrustedIntermediate) {
  AddCertsToNSS();
  DistrustCert(newintermediate_.get());

  EXPECT_TRUE(HasTrust(
      {oldroot_, newroot_, target_, oldintermediate_, newrootrollover_},
      CertificateTrustType::UNSPECIFIED));
  EXPECT_TRUE(HasTrust({newintermediate_}, CertificateTrustType::DISTRUSTED));
}

// Trust a single server certificate.
TEST_F(TrustStoreNSSTest, TrustedServer) {
  AddCertsToNSS();
  TrustServerCert(target_.get());

  // Server-trusted certificates are handled as UNSPECIFIED since we don't
  // support the notion of explictly trusted server certs. See
  // https://crbug.com/814994.
  EXPECT_TRUE(HasTrust({oldroot_, newroot_, target_, oldintermediate_,
                        newintermediate_, newrootrollover_},
                       CertificateTrustType::UNSPECIFIED));
}

// Trust multiple self-signed CA certificates with the same name.
TEST_F(TrustStoreNSSTest, MultipleTrustedCAWithSameSubject) {
  AddCertsToNSS();
  TrustCert(oldroot_.get());
  TrustCert(newroot_.get());

  EXPECT_TRUE(
      HasTrust({target_, oldintermediate_, newintermediate_, newrootrollover_},
               CertificateTrustType::UNSPECIFIED));
  EXPECT_TRUE(
      HasTrust({oldroot_, newroot_}, CertificateTrustType::TRUSTED_ANCHOR));
}

// Different trust settings for multiple self-signed CA certificates with the
// same name.
TEST_F(TrustStoreNSSTest, DifferingTrustCAWithSameSubject) {
  AddCertsToNSS();
  DistrustCert(oldroot_.get());
  TrustCert(newroot_.get());

  EXPECT_TRUE(
      HasTrust({target_, oldintermediate_, newintermediate_, newrootrollover_},
               CertificateTrustType::UNSPECIFIED));
  EXPECT_TRUE(HasTrust({oldroot_}, CertificateTrustType::DISTRUSTED));
  EXPECT_TRUE(HasTrust({newroot_}, CertificateTrustType::TRUSTED_ANCHOR));
}

class TrustStoreNSSTestDelegate {
 public:
  TrustStoreNSSTestDelegate() : trust_store_nss_(trustSSL) {}

  void AddCert(scoped_refptr<ParsedCertificate> cert) {
    ASSERT_TRUE(test_nssdb_.is_open());
    ScopedCERTCertificate nss_cert(x509_util::CreateCERTCertificateFromBytes(
        cert->der_cert().UnsafeData(), cert->der_cert().Length()));
    ASSERT_TRUE(nss_cert);
    SECStatus srv = PK11_ImportCert(
        test_nssdb_.slot(), nss_cert.get(), CK_INVALID_HANDLE,
        GetUniqueNickname().c_str(), PR_FALSE /* includeTrust (unused) */);
    ASSERT_EQ(SECSuccess, srv);
  }

  CertIssuerSource& source() { return trust_store_nss_; }

 protected:
  std::string GetUniqueNickname() {
    return "cert_issuer_source_nss_unittest" +
           base::UintToString(nickname_counter_++);
  }

  crypto::ScopedTestNSSDB test_nssdb_;
  TrustStoreNSS trust_store_nss_;
  unsigned int nickname_counter_ = 0;
};

INSTANTIATE_TYPED_TEST_CASE_P(TrustStoreNSSTest2,
                              CertIssuerSourceSyncTest,
                              TrustStoreNSSTestDelegate);

// NSS doesn't normalize UTF8String values, so use the not-normalized version of
// those tests.
INSTANTIATE_TYPED_TEST_CASE_P(TrustStoreNSSNotNormalizedTest,
                              CertIssuerSourceSyncNotNormalizedTest,
                              TrustStoreNSSTestDelegate);

}  // namespace

}  // namespace net

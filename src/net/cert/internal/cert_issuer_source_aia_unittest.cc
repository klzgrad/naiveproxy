// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/internal/cert_issuer_source_aia.h"

#include <memory>

#include "net/base/network_isolation_key.h"
#include "net/cert/cert_net_fetcher.h"
#include "net/cert/internal/cert_errors.h"
#include "net/cert/internal/parsed_certificate.h"
#include "net/cert/internal/test_helpers.h"
#include "net/cert/x509_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace net {

namespace {

using ::testing::ByMove;
using ::testing::Mock;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::_;

::testing::AssertionResult ReadTestPem(const std::string& file_name,
                                       const std::string& block_name,
                                       std::string* result) {
  const PemBlockMapping mappings[] = {
      {block_name.c_str(), result},
  };

  return ReadTestDataFromPemFile(file_name, mappings);
}

::testing::AssertionResult ReadTestCert(
    const std::string& file_name,
    scoped_refptr<ParsedCertificate>* result) {
  std::string der;
  ::testing::AssertionResult r =
      ReadTestPem("net/data/cert_issuer_source_aia_unittest/" + file_name,
                  "CERTIFICATE", &der);
  if (!r)
    return r;
  CertErrors errors;
  *result = ParsedCertificate::Create(x509_util::CreateCryptoBuffer(der), {},
                                      &errors);
  if (!*result) {
    return ::testing::AssertionFailure()
           << "ParsedCertificate::Create() failed:\n"
           << errors.ToDebugString();
  }
  return ::testing::AssertionSuccess();
}

std::vector<uint8_t> CertDataVector(const ParsedCertificate* cert) {
  std::vector<uint8_t> data(
      cert->der_cert().UnsafeData(),
      cert->der_cert().UnsafeData() + cert->der_cert().Length());
  return data;
}

// MockCertNetFetcher is an implementation of CertNetFetcher for testing.
class MockCertNetFetcher : public CertNetFetcher {
 public:
  MockCertNetFetcher() = default;
  MOCK_METHOD0(Shutdown, void());
  MOCK_METHOD4(
      FetchCaIssuers,
      std::unique_ptr<Request>(const GURL& url,
                               const NetworkIsolationKey& network_isolation_key,
                               int timeout_milliseconds,
                               int max_response_bytes));
  MOCK_METHOD4(
      FetchCrl,
      std::unique_ptr<Request>(const GURL& url,
                               const NetworkIsolationKey& network_isolation_key,
                               int timeout_milliseconds,
                               int max_response_bytes));

  MOCK_METHOD4(
      FetchOcsp,
      std::unique_ptr<Request>(const GURL& url,
                               const NetworkIsolationKey& network_isolation_key,
                               int timeout_milliseconds,
                               int max_response_bytes));

 protected:
  ~MockCertNetFetcher() override = default;
};

// MockCertNetFetcherRequest gives back the indicated error and bytes.
class MockCertNetFetcherRequest : public CertNetFetcher::Request {
 public:
  MockCertNetFetcherRequest(Error error, std::vector<uint8_t> bytes)
      : error_(error), bytes_(std::move(bytes)) {}

  void WaitForResult(Error* error, std::vector<uint8_t>* bytes) override {
    DCHECK(!did_consume_result_);
    *error = error_;
    *bytes = std::move(bytes_);
    did_consume_result_ = true;
  }

 private:
  Error error_;
  std::vector<uint8_t> bytes_;
  bool did_consume_result_ = false;
};

// Creates a CertNetFetcher::Request that completes with an error.
std::unique_ptr<CertNetFetcher::Request> CreateMockRequest(Error error) {
  return std::make_unique<MockCertNetFetcherRequest>(error,
                                                     std::vector<uint8_t>());
}

// Creates a CertNetFetcher::Request that completes with the specified error
// code and bytes.
std::unique_ptr<CertNetFetcher::Request> CreateMockRequest(
    const std::vector<uint8_t>& bytes) {
  return std::make_unique<MockCertNetFetcherRequest>(OK, bytes);
}

// CertIssuerSourceAia does not return results for SyncGetIssuersOf.
TEST(CertIssuerSourceAiaTest, NoSyncResults) {
  scoped_refptr<ParsedCertificate> cert;
  ASSERT_TRUE(ReadTestCert("target_two_aia.pem", &cert));

  // No methods on |mock_fetcher| should be called.
  auto mock_fetcher = base::MakeRefCounted<StrictMock<MockCertNetFetcher>>();
  CertIssuerSourceAia aia_source(mock_fetcher);
  ParsedCertificateList issuers;
  aia_source.SyncGetIssuersOf(cert.get(), &issuers);
  EXPECT_EQ(0U, issuers.size());
}

// If the AuthorityInfoAccess extension is not present, AsyncGetIssuersOf should
// synchronously indicate no results.
TEST(CertIssuerSourceAiaTest, NoAia) {
  scoped_refptr<ParsedCertificate> cert;
  ASSERT_TRUE(ReadTestCert("target_no_aia.pem", &cert));

  // No methods on |mock_fetcher| should be called.
  auto mock_fetcher = base::MakeRefCounted<StrictMock<MockCertNetFetcher>>();
  CertIssuerSourceAia aia_source(mock_fetcher);
  std::unique_ptr<CertIssuerSource::Request> request;
  aia_source.AsyncGetIssuersOf(cert.get(), &request);
  EXPECT_EQ(nullptr, request);
}

// If the AuthorityInfoAccess extension only contains non-HTTP URIs,
// AsyncGetIssuersOf should create a Request object. The URL scheme check is
// part of the specific CertNetFetcher implementation, this tests that we handle
// ERR_DISALLOWED_URL_SCHEME properly. If FetchCaIssuers is modified to fail
// synchronously in that case, this test will be more interesting.
TEST(CertIssuerSourceAiaTest, FileAia) {
  scoped_refptr<ParsedCertificate> cert;
  ASSERT_TRUE(ReadTestCert("target_file_aia.pem", &cert));

  auto mock_fetcher = base::MakeRefCounted<StrictMock<MockCertNetFetcher>>();
  EXPECT_CALL(*mock_fetcher, FetchCaIssuers(GURL("file:///dev/null"), _, _, _))
      .WillOnce(Return(ByMove(CreateMockRequest(ERR_DISALLOWED_URL_SCHEME))));

  CertIssuerSourceAia aia_source(mock_fetcher);
  std::unique_ptr<CertIssuerSource::Request> cert_source_request;
  aia_source.AsyncGetIssuersOf(cert.get(), &cert_source_request);
  ASSERT_NE(nullptr, cert_source_request);

  // No results.
  ParsedCertificateList result_certs;
  cert_source_request->GetNext(&result_certs);
  EXPECT_TRUE(result_certs.empty());
}

// If the AuthorityInfoAccess extension contains an invalid URL,
// AsyncGetIssuersOf should synchronously indicate no results.
TEST(CertIssuerSourceAiaTest, OneInvalidURL) {
  scoped_refptr<ParsedCertificate> cert;
  ASSERT_TRUE(ReadTestCert("target_invalid_url_aia.pem", &cert));

  auto mock_fetcher = base::MakeRefCounted<StrictMock<MockCertNetFetcher>>();
  CertIssuerSourceAia aia_source(mock_fetcher);
  std::unique_ptr<CertIssuerSource::Request> request;
  aia_source.AsyncGetIssuersOf(cert.get(), &request);
  EXPECT_EQ(nullptr, request);
}

// AuthorityInfoAccess with a single HTTP url pointing to a single DER cert.
TEST(CertIssuerSourceAiaTest, OneAia) {
  scoped_refptr<ParsedCertificate> cert;
  ASSERT_TRUE(ReadTestCert("target_one_aia.pem", &cert));
  scoped_refptr<ParsedCertificate> intermediate_cert;
  ASSERT_TRUE(ReadTestCert("i.pem", &intermediate_cert));

  auto mock_fetcher = base::MakeRefCounted<StrictMock<MockCertNetFetcher>>();

  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia/I.cer"), _, _, _))
      .WillOnce(Return(
          ByMove(CreateMockRequest(CertDataVector(intermediate_cert.get())))));

  CertIssuerSourceAia aia_source(mock_fetcher);
  std::unique_ptr<CertIssuerSource::Request> cert_source_request;
  aia_source.AsyncGetIssuersOf(cert.get(), &cert_source_request);
  ASSERT_NE(nullptr, cert_source_request);

  ParsedCertificateList result_certs;
  cert_source_request->GetNext(&result_certs);
  ASSERT_EQ(1u, result_certs.size());
  ASSERT_EQ(result_certs.front()->der_cert(), intermediate_cert->der_cert());

  result_certs.clear();
  cert_source_request->GetNext(&result_certs);
  EXPECT_TRUE(result_certs.empty());
}

// AuthorityInfoAccess with two URIs, one a FILE, the other a HTTP.
// Simulate a ERR_DISALLOWED_URL_SCHEME for the file URL. If FetchCaIssuers is
// modified to synchronously reject disallowed schemes, this test will be more
// interesting.
TEST(CertIssuerSourceAiaTest, OneFileOneHttpAia) {
  scoped_refptr<ParsedCertificate> cert;
  ASSERT_TRUE(ReadTestCert("target_file_and_http_aia.pem", &cert));
  scoped_refptr<ParsedCertificate> intermediate_cert;
  ASSERT_TRUE(ReadTestCert("i2.pem", &intermediate_cert));

  auto mock_fetcher = base::MakeRefCounted<StrictMock<MockCertNetFetcher>>();

  EXPECT_CALL(*mock_fetcher, FetchCaIssuers(GURL("file:///dev/null"), _, _, _))
      .WillOnce(Return(ByMove(CreateMockRequest(ERR_DISALLOWED_URL_SCHEME))));

  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia2/I2.foo"), _, _, _))
      .WillOnce(Return(
          ByMove(CreateMockRequest(CertDataVector(intermediate_cert.get())))));

  CertIssuerSourceAia aia_source(mock_fetcher);
  std::unique_ptr<CertIssuerSource::Request> cert_source_request;
  aia_source.AsyncGetIssuersOf(cert.get(), &cert_source_request);
  ASSERT_NE(nullptr, cert_source_request);

  ParsedCertificateList result_certs;
  cert_source_request->GetNext(&result_certs);
  ASSERT_EQ(1u, result_certs.size());
  ASSERT_EQ(result_certs.front()->der_cert(), intermediate_cert->der_cert());

  cert_source_request->GetNext(&result_certs);
  EXPECT_EQ(1u, result_certs.size());
}

// AuthorityInfoAccess with two URIs, one is invalid, the other HTTP.
TEST(CertIssuerSourceAiaTest, OneInvalidOneHttpAia) {
  scoped_refptr<ParsedCertificate> cert;
  ASSERT_TRUE(ReadTestCert("target_invalid_and_http_aia.pem", &cert));
  scoped_refptr<ParsedCertificate> intermediate_cert;
  ASSERT_TRUE(ReadTestCert("i2.pem", &intermediate_cert));

  scoped_refptr<StrictMock<MockCertNetFetcher>> mock_fetcher(
      new StrictMock<MockCertNetFetcher>());

  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia2/I2.foo"), _, _, _))
      .WillOnce(Return(
          ByMove(CreateMockRequest(CertDataVector(intermediate_cert.get())))));

  CertIssuerSourceAia aia_source(mock_fetcher);
  std::unique_ptr<CertIssuerSource::Request> cert_source_request;
  aia_source.AsyncGetIssuersOf(cert.get(), &cert_source_request);
  ASSERT_NE(nullptr, cert_source_request);

  ParsedCertificateList result_certs;
  cert_source_request->GetNext(&result_certs);
  ASSERT_EQ(1u, result_certs.size());
  EXPECT_EQ(result_certs.front()->der_cert(), intermediate_cert->der_cert());

  // No more results.
  result_certs.clear();
  cert_source_request->GetNext(&result_certs);
  EXPECT_EQ(0u, result_certs.size());
}

// AuthorityInfoAccess with two HTTP urls, each pointing to a single DER cert.
// One request completes, results are retrieved, then the next request completes
// and the results are retrieved.
TEST(CertIssuerSourceAiaTest, TwoAiaCompletedInSeries) {
  scoped_refptr<ParsedCertificate> cert;
  ASSERT_TRUE(ReadTestCert("target_two_aia.pem", &cert));
  scoped_refptr<ParsedCertificate> intermediate_cert;
  ASSERT_TRUE(ReadTestCert("i.pem", &intermediate_cert));
  scoped_refptr<ParsedCertificate> intermediate_cert2;
  ASSERT_TRUE(ReadTestCert("i2.pem", &intermediate_cert2));

  scoped_refptr<StrictMock<MockCertNetFetcher>> mock_fetcher(
      new StrictMock<MockCertNetFetcher>());

  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia/I.cer"), _, _, _))
      .WillOnce(Return(
          ByMove(CreateMockRequest(CertDataVector(intermediate_cert.get())))));

  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia2/I2.foo"), _, _, _))
      .WillOnce(Return(
          ByMove(CreateMockRequest(CertDataVector(intermediate_cert2.get())))));

  CertIssuerSourceAia aia_source(mock_fetcher);
  std::unique_ptr<CertIssuerSource::Request> cert_source_request;
  aia_source.AsyncGetIssuersOf(cert.get(), &cert_source_request);
  ASSERT_NE(nullptr, cert_source_request);

  // GetNext() should return intermediate_cert followed by intermediate_cert2.
  // They are returned in two separate batches.
  ParsedCertificateList result_certs;
  cert_source_request->GetNext(&result_certs);
  ASSERT_EQ(1u, result_certs.size());
  EXPECT_EQ(result_certs.front()->der_cert(), intermediate_cert->der_cert());

  result_certs.clear();
  cert_source_request->GetNext(&result_certs);
  ASSERT_EQ(1u, result_certs.size());
  EXPECT_EQ(result_certs.front()->der_cert(), intermediate_cert2->der_cert());

  // No more results.
  result_certs.clear();
  cert_source_request->GetNext(&result_certs);
  EXPECT_EQ(0u, result_certs.size());
}

// AuthorityInfoAccess with a single HTTP url pointing to a single DER cert,
// CertNetFetcher request fails.
TEST(CertIssuerSourceAiaTest, OneAiaHttpError) {
  scoped_refptr<ParsedCertificate> cert;
  ASSERT_TRUE(ReadTestCert("target_one_aia.pem", &cert));

  scoped_refptr<StrictMock<MockCertNetFetcher>> mock_fetcher(
      new StrictMock<MockCertNetFetcher>());

  // HTTP request returns with an error.
  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia/I.cer"), _, _, _))
      .WillOnce(Return(ByMove(CreateMockRequest(ERR_FAILED))));

  CertIssuerSourceAia aia_source(mock_fetcher);
  std::unique_ptr<CertIssuerSource::Request> cert_source_request;
  aia_source.AsyncGetIssuersOf(cert.get(), &cert_source_request);
  ASSERT_NE(nullptr, cert_source_request);

  // No results.
  ParsedCertificateList result_certs;
  cert_source_request->GetNext(&result_certs);
  ASSERT_EQ(0u, result_certs.size());
}

// AuthorityInfoAccess with a single HTTP url pointing to a single DER cert,
// CertNetFetcher request completes, but the DER cert fails to parse.
TEST(CertIssuerSourceAiaTest, OneAiaParseError) {
  scoped_refptr<ParsedCertificate> cert;
  ASSERT_TRUE(ReadTestCert("target_one_aia.pem", &cert));

  scoped_refptr<StrictMock<MockCertNetFetcher>> mock_fetcher(
      new StrictMock<MockCertNetFetcher>());

  // HTTP request returns invalid certificate data.
  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia/I.cer"), _, _, _))
      .WillOnce(Return(
          ByMove(CreateMockRequest(std::vector<uint8_t>({1, 2, 3, 4, 5})))));

  CertIssuerSourceAia aia_source(mock_fetcher);
  std::unique_ptr<CertIssuerSource::Request> cert_source_request;
  aia_source.AsyncGetIssuersOf(cert.get(), &cert_source_request);
  ASSERT_NE(nullptr, cert_source_request);

  // No results.
  ParsedCertificateList result_certs;
  cert_source_request->GetNext(&result_certs);
  ASSERT_EQ(0u, result_certs.size());
}

// AuthorityInfoAccess with two HTTP urls, each pointing to a single DER cert.
// One request fails.
TEST(CertIssuerSourceAiaTest, TwoAiaCompletedInSeriesFirstFails) {
  scoped_refptr<ParsedCertificate> cert;
  ASSERT_TRUE(ReadTestCert("target_two_aia.pem", &cert));
  scoped_refptr<ParsedCertificate> intermediate_cert2;
  ASSERT_TRUE(ReadTestCert("i2.pem", &intermediate_cert2));

  scoped_refptr<StrictMock<MockCertNetFetcher>> mock_fetcher(
      new StrictMock<MockCertNetFetcher>());

  // Request for I.cer completes first, but fails.
  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia/I.cer"), _, _, _))
      .WillOnce(Return(ByMove(CreateMockRequest(ERR_INVALID_RESPONSE))));

  // Request for I2.foo succeeds.
  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia2/I2.foo"), _, _, _))
      .WillOnce(Return(
          ByMove(CreateMockRequest(CertDataVector(intermediate_cert2.get())))));

  CertIssuerSourceAia aia_source(mock_fetcher);
  std::unique_ptr<CertIssuerSource::Request> cert_source_request;
  aia_source.AsyncGetIssuersOf(cert.get(), &cert_source_request);
  ASSERT_NE(nullptr, cert_source_request);

  // GetNext() should return intermediate_cert2.
  ParsedCertificateList result_certs;
  cert_source_request->GetNext(&result_certs);
  ASSERT_EQ(1u, result_certs.size());
  EXPECT_EQ(result_certs.front()->der_cert(), intermediate_cert2->der_cert());

  // No more results.
  result_certs.clear();
  cert_source_request->GetNext(&result_certs);
  EXPECT_EQ(0u, result_certs.size());
}

// AuthorityInfoAccess with two HTTP urls, each pointing to a single DER cert.
// First request completes, result is retrieved, then the second request fails.
TEST(CertIssuerSourceAiaTest, TwoAiaCompletedInSeriesSecondFails) {
  scoped_refptr<ParsedCertificate> cert;
  ASSERT_TRUE(ReadTestCert("target_two_aia.pem", &cert));
  scoped_refptr<ParsedCertificate> intermediate_cert;
  ASSERT_TRUE(ReadTestCert("i.pem", &intermediate_cert));

  scoped_refptr<StrictMock<MockCertNetFetcher>> mock_fetcher(
      new StrictMock<MockCertNetFetcher>());

  // Request for I.cer completes first.
  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia/I.cer"), _, _, _))
      .WillOnce(Return(
          ByMove(CreateMockRequest(CertDataVector(intermediate_cert.get())))));

  // Request for I2.foo fails.
  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia2/I2.foo"), _, _, _))
      .WillOnce(Return(ByMove(CreateMockRequest(ERR_INVALID_RESPONSE))));

  CertIssuerSourceAia aia_source(mock_fetcher);
  std::unique_ptr<CertIssuerSource::Request> cert_source_request;
  aia_source.AsyncGetIssuersOf(cert.get(), &cert_source_request);
  ASSERT_NE(nullptr, cert_source_request);

  // GetNext() should return intermediate_cert.
  ParsedCertificateList result_certs;
  cert_source_request->GetNext(&result_certs);
  ASSERT_EQ(1u, result_certs.size());
  EXPECT_EQ(result_certs.front()->der_cert(), intermediate_cert->der_cert());

  // No more results.
  result_certs.clear();
  cert_source_request->GetNext(&result_certs);
  EXPECT_EQ(0u, result_certs.size());
}

// AuthorityInfoAccess with six HTTP URLs.  kMaxFetchesPerCert is 5, so the
// sixth URL should be ignored.
TEST(CertIssuerSourceAiaTest, MaxFetchesPerCert) {
  scoped_refptr<ParsedCertificate> cert;
  ASSERT_TRUE(ReadTestCert("target_six_aia.pem", &cert));

  scoped_refptr<StrictMock<MockCertNetFetcher>> mock_fetcher(
      new StrictMock<MockCertNetFetcher>());

  std::vector<uint8_t> bad_der({1, 2, 3, 4, 5});

  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia/I.cer"), _, _, _))
      .WillOnce(Return(ByMove(CreateMockRequest(bad_der))));

  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia2/I2.foo"), _, _, _))
      .WillOnce(Return(ByMove(CreateMockRequest(bad_der))));

  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia3/I3.foo"), _, _, _))
      .WillOnce(Return(ByMove(CreateMockRequest(bad_der))));

  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia4/I4.foo"), _, _, _))
      .WillOnce(Return(ByMove(CreateMockRequest(bad_der))));

  EXPECT_CALL(*mock_fetcher,
              FetchCaIssuers(GURL("http://url-for-aia5/I5.foo"), _, _, _))
      .WillOnce(Return(ByMove(CreateMockRequest(bad_der))));

  // Note that the sixth URL (http://url-for-aia6/I6.foo) will not be requested.

  CertIssuerSourceAia aia_source(mock_fetcher);
  std::unique_ptr<CertIssuerSource::Request> cert_source_request;
  aia_source.AsyncGetIssuersOf(cert.get(), &cert_source_request);
  ASSERT_NE(nullptr, cert_source_request);

  // GetNext() will not get any certificates (since the first 5 fail to be
  // parsed, and the sixth URL is not attempted).
  ParsedCertificateList result_certs;
  cert_source_request->GetNext(&result_certs);
  ASSERT_EQ(0u, result_certs.size());
}

}  // namespace

}  // namespace net

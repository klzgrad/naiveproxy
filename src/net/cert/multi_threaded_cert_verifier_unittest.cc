// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/multi_threaded_cert_verifier.h"

#include <memory>

#include "base/bind.h"
#include "base/debug/leak_annotations.h"
#include "base/files/file_path.h"
#include "base/format_macros.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "net/cert/cert_verify_proc.h"
#include "net/cert/cert_verify_result.h"
#include "net/cert/x509_certificate.h"
#include "net/log/net_log_with_source.h"
#include "net/test/cert_test_util.h"
#include "net/test/gtest_util.h"
#include "net/test/test_data_directory.h"
#include "net/test/test_with_scoped_task_environment.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using net::test::IsError;
using net::test::IsOk;
using testing::_;
using testing::DoAll;
using testing::Return;

namespace net {

namespace {

void FailTest(int /* result */) {
  FAIL();
}

class MockCertVerifyProc : public CertVerifyProc {
 public:
  MOCK_METHOD7(VerifyInternal,
               int(X509Certificate*,
                   const std::string&,
                   const std::string&,
                   int,
                   CRLSet*,
                   const CertificateList&,
                   CertVerifyResult*));
  MOCK_CONST_METHOD0(SupportsAdditionalTrustAnchors, bool());

 private:
  ~MockCertVerifyProc() override = default;
};

ACTION(SetCertVerifyResult) {
  X509Certificate* cert = arg0;
  CertVerifyResult* result = arg6;
  result->Reset();
  result->verified_cert = cert;
  result->cert_status = CERT_STATUS_COMMON_NAME_INVALID;
}

ACTION(SetCertVerifyRevokedResult) {
  X509Certificate* cert = arg0;
  CertVerifyResult* result = arg6;
  result->Reset();
  result->verified_cert = cert;
  result->cert_status = CERT_STATUS_REVOKED;
}

}  // namespace

class MultiThreadedCertVerifierTest : public TestWithScopedTaskEnvironment {
 public:
  MultiThreadedCertVerifierTest()
      : mock_verify_proc_(base::MakeRefCounted<MockCertVerifyProc>()),
        verifier_(mock_verify_proc_) {
    EXPECT_CALL(*mock_verify_proc_, SupportsAdditionalTrustAnchors())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_verify_proc_, VerifyInternal(_, _, _, _, _, _, _))
        .WillRepeatedly(
            DoAll(SetCertVerifyResult(), Return(ERR_CERT_COMMON_NAME_INVALID)));
  }
  ~MultiThreadedCertVerifierTest() override = default;

 protected:
  scoped_refptr<MockCertVerifyProc> mock_verify_proc_;
  MultiThreadedCertVerifier verifier_;
};

// Tests an inflight join.
TEST_F(MultiThreadedCertVerifierTest, InflightJoin) {
  base::FilePath certs_dir = GetTestCertsDirectory();
  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "ok_cert.pem"));
  ASSERT_NE(static_cast<X509Certificate*>(NULL), test_cert.get());

  int error;
  CertVerifyResult verify_result;
  TestCompletionCallback callback;
  std::unique_ptr<CertVerifier::Request> request;
  CertVerifyResult verify_result2;
  TestCompletionCallback callback2;
  std::unique_ptr<CertVerifier::Request> request2;

  error = verifier_.Verify(CertVerifier::RequestParams(
                               test_cert, "www.example.com", 0, std::string()),
                           &verify_result, callback.callback(), &request,
                           NetLogWithSource());
  ASSERT_THAT(error, IsError(ERR_IO_PENDING));
  EXPECT_TRUE(request);
  error = verifier_.Verify(CertVerifier::RequestParams(
                               test_cert, "www.example.com", 0, std::string()),
                           &verify_result2, callback2.callback(), &request2,
                           NetLogWithSource());
  EXPECT_THAT(error, IsError(ERR_IO_PENDING));
  EXPECT_TRUE(request2);
  error = callback.WaitForResult();
  EXPECT_TRUE(IsCertificateError(error));
  error = callback2.WaitForResult();
  ASSERT_TRUE(IsCertificateError(error));
  ASSERT_EQ(2u, verifier_.requests());
  ASSERT_EQ(1u, verifier_.inflight_joins());
}

// Tests that the callback of a canceled request is never made.
TEST_F(MultiThreadedCertVerifierTest, CancelRequest) {
  base::FilePath certs_dir = GetTestCertsDirectory();
  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "ok_cert.pem"));
  ASSERT_NE(static_cast<X509Certificate*>(NULL), test_cert.get());

  int error;
  CertVerifyResult verify_result;
  std::unique_ptr<CertVerifier::Request> request;

  error = verifier_.Verify(CertVerifier::RequestParams(
                               test_cert, "www.example.com", 0, std::string()),
                           &verify_result, base::BindOnce(&FailTest), &request,
                           NetLogWithSource());
  ASSERT_THAT(error, IsError(ERR_IO_PENDING));
  ASSERT_TRUE(request);
  request.reset();

  // Issue a few more requests to the worker pool and wait for their
  // completion, so that the task of the canceled request (which runs on a
  // worker thread) is likely to complete by the end of this test.
  TestCompletionCallback callback;
  for (int i = 0; i < 5; ++i) {
    error = verifier_.Verify(
        CertVerifier::RequestParams(test_cert, "www2.example.com", 0,
                                    std::string()),
        &verify_result, callback.callback(), &request, NetLogWithSource());
    ASSERT_THAT(error, IsError(ERR_IO_PENDING));
    EXPECT_TRUE(request);
    error = callback.WaitForResult();
  }
}

// Tests that a canceled request is not leaked.
TEST_F(MultiThreadedCertVerifierTest, CancelRequestThenQuit) {
  base::FilePath certs_dir = GetTestCertsDirectory();
  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "ok_cert.pem"));
  ASSERT_NE(static_cast<X509Certificate*>(NULL), test_cert.get());

  int error;
  CertVerifyResult verify_result;
  TestCompletionCallback callback;
  std::unique_ptr<CertVerifier::Request> request;

  {
    // Because shutdown intentionally doesn't join worker threads, memory may
    // be leaked if the main thread shuts down before the worker thread
    // completes. In particular MultiThreadedCertVerifier calls
    // base::WorkerPool::PostTaskAndReply(), which leaks its "relay" when it
    // can't post the reply back to the origin thread. See
    // https://crbug.com/522514
    ANNOTATE_SCOPED_MEMORY_LEAK;
    error = verifier_.Verify(
        CertVerifier::RequestParams(test_cert, "www.example.com", 0,
                                    std::string()),
        &verify_result, callback.callback(), &request, NetLogWithSource());
  }
  ASSERT_THAT(error, IsError(ERR_IO_PENDING));
  EXPECT_TRUE(request);
  request.reset();
  // Destroy |verifier| by going out of scope.
}

// Tests de-duplication of requests.
// Starts up 5 requests, of which 3 are unique.
TEST_F(MultiThreadedCertVerifierTest, MultipleInflightJoin) {
  base::FilePath certs_dir = GetTestCertsDirectory();
  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "ok_cert.pem"));
  ASSERT_NE(static_cast<X509Certificate*>(nullptr), test_cert.get());

  int error;
  CertVerifyResult verify_result1;
  TestCompletionCallback callback1;
  std::unique_ptr<CertVerifier::Request> request1;
  CertVerifyResult verify_result2;
  TestCompletionCallback callback2;
  std::unique_ptr<CertVerifier::Request> request2;
  CertVerifyResult verify_result3;
  TestCompletionCallback callback3;
  std::unique_ptr<CertVerifier::Request> request3;
  CertVerifyResult verify_result4;
  TestCompletionCallback callback4;
  std::unique_ptr<CertVerifier::Request> request4;
  CertVerifyResult verify_result5;
  TestCompletionCallback callback5;
  std::unique_ptr<CertVerifier::Request> request5;

  const char domain1[] = "www.example1.com";
  const char domain2[] = "www.exampleB.com";
  const char domain3[] = "www.example3.com";

  // Start 3 unique requests.
  error = verifier_.Verify(
      CertVerifier::RequestParams(test_cert, domain2, 0, std::string()),
      &verify_result1, callback1.callback(), &request1, NetLogWithSource());
  ASSERT_THAT(error, IsError(ERR_IO_PENDING));
  EXPECT_TRUE(request1);

  error = verifier_.Verify(
      CertVerifier::RequestParams(test_cert, domain2, 0, std::string()),
      &verify_result2, callback2.callback(), &request2, NetLogWithSource());
  EXPECT_THAT(error, IsError(ERR_IO_PENDING));
  EXPECT_TRUE(request2);

  error = verifier_.Verify(
      CertVerifier::RequestParams(test_cert, domain3, 0, std::string()),
      &verify_result3, callback3.callback(), &request3, NetLogWithSource());
  EXPECT_THAT(error, IsError(ERR_IO_PENDING));
  EXPECT_TRUE(request3);

  // Start duplicate requests (which should join to existing jobs).
  error = verifier_.Verify(
      CertVerifier::RequestParams(test_cert, domain1, 0, std::string()),
      &verify_result4, callback4.callback(), &request4, NetLogWithSource());
  EXPECT_THAT(error, IsError(ERR_IO_PENDING));
  EXPECT_TRUE(request4);

  error = verifier_.Verify(
      CertVerifier::RequestParams(test_cert, domain2, 0, std::string()),
      &verify_result5, callback5.callback(), &request5, NetLogWithSource());
  EXPECT_THAT(error, IsError(ERR_IO_PENDING));
  EXPECT_TRUE(request5);

  error = callback1.WaitForResult();
  EXPECT_TRUE(IsCertificateError(error));
  error = callback2.WaitForResult();
  ASSERT_TRUE(IsCertificateError(error));
  error = callback4.WaitForResult();
  ASSERT_TRUE(IsCertificateError(error));

  // Let the other requests automatically cancel.
  ASSERT_EQ(5u, verifier_.requests());
  ASSERT_EQ(2u, verifier_.inflight_joins());
}

// Tests propagation of configuration options into CertVerifyProc flags
TEST_F(MultiThreadedCertVerifierTest, ConvertsConfigToFlags) {
  base::FilePath certs_dir = GetTestCertsDirectory();
  scoped_refptr<X509Certificate> test_cert(
      ImportCertFromFile(certs_dir, "ok_cert.pem"));
  ASSERT_TRUE(test_cert);

  const struct TestConfig {
    bool CertVerifier::Config::*config_ptr;
    int expected_flag;
  } kTestConfig[] = {
      {&CertVerifier::Config::enable_rev_checking,
       CertVerifyProc::VERIFY_REV_CHECKING_ENABLED},
      {&CertVerifier::Config::require_rev_checking_local_anchors,
       CertVerifyProc::VERIFY_REV_CHECKING_REQUIRED_LOCAL_ANCHORS},
      {&CertVerifier::Config::enable_sha1_local_anchors,
       CertVerifyProc::VERIFY_ENABLE_SHA1_LOCAL_ANCHORS},
      {&CertVerifier::Config::disable_symantec_enforcement,
       CertVerifyProc::VERIFY_DISABLE_SYMANTEC_ENFORCEMENT},
  };
  for (const auto& test_config : kTestConfig) {
    CertVerifier::Config config;
    config.*test_config.config_ptr = true;

    verifier_.SetConfig(config);

    EXPECT_CALL(*mock_verify_proc_,
                VerifyInternal(_, _, _, test_config.expected_flag, _, _, _))
        .WillRepeatedly(
            DoAll(SetCertVerifyRevokedResult(), Return(ERR_CERT_REVOKED)));

    CertVerifyResult verify_result;
    TestCompletionCallback callback;
    std::unique_ptr<CertVerifier::Request> request;
    int error = verifier_.Verify(
        CertVerifier::RequestParams(test_cert, "www.example.com", 0,
                                    std::string()),
        &verify_result, callback.callback(), &request, NetLogWithSource());
    ASSERT_THAT(error, IsError(ERR_IO_PENDING));
    EXPECT_TRUE(request);
    error = callback.WaitForResult();
    EXPECT_TRUE(IsCertificateError(error));
    EXPECT_THAT(error, IsError(ERR_CERT_REVOKED));

    testing::Mock::VerifyAndClearExpectations(mock_verify_proc_.get());
  }
}

}  // namespace net

// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/url_request_simple_job.h"

#include <memory>
#include <utility>

#include "base/bind_helpers.h"
#include "base/run_loop.h"
#include "base/sequenced_task_runner.h"
#include "base/stl_util.h"
#include "base/strings/string_piece.h"
#include "base/strings/stringprintf.h"
#include "net/base/request_priority.h"
#include "net/test/gtest_util.h"
#include "net/test/test_with_scoped_task_environment.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/url_request_job.h"
#include "net/url_request/url_request_job_factory.h"
#include "net/url_request/url_request_job_factory_impl.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using net::test::IsError;
using net::test::IsOk;

namespace net {

namespace {

const char kTestData[] = "Huge data array";
const int kRangeFirstPosition = 5;
const int kRangeLastPosition = 8;
static_assert(kRangeFirstPosition > 0 &&
                  kRangeFirstPosition < kRangeLastPosition &&
                  kRangeLastPosition <
                      static_cast<int>(base::size(kTestData) - 1),
              "invalid range");

class MockSimpleJob : public URLRequestSimpleJob {
 public:
  MockSimpleJob(URLRequest* request,
                NetworkDelegate* network_delegate,
                base::StringPiece data)
      : URLRequestSimpleJob(request, network_delegate),
        data_(data.as_string()) {}

 protected:
  // URLRequestSimpleJob implementation:
  int GetData(std::string* mime_type,
              std::string* charset,
              std::string* data,
              CompletionOnceCallback callback) const override {
    mime_type->assign("text/plain");
    charset->assign("US-ASCII");
    data->assign(data_);
    return OK;
  }

 private:
  ~MockSimpleJob() override = default;

  const std::string data_;

  DISALLOW_COPY_AND_ASSIGN(MockSimpleJob);
};

class CancelAfterFirstReadURLRequestDelegate : public TestDelegate {
 public:
  CancelAfterFirstReadURLRequestDelegate() : run_loop_(new base::RunLoop) {}

  ~CancelAfterFirstReadURLRequestDelegate() override = default;

  void OnResponseStarted(URLRequest* request, int net_error) override {
    DCHECK_NE(ERR_IO_PENDING, net_error);
    // net::TestDelegate will start the first read.
    TestDelegate::OnResponseStarted(request, net_error);
    request->Cancel();
    run_loop_->Quit();
  }

  void WaitUntilHeadersReceived() const { run_loop_->Run(); }

 private:
  std::unique_ptr<base::RunLoop> run_loop_;

  DISALLOW_COPY_AND_ASSIGN(CancelAfterFirstReadURLRequestDelegate);
};

class SimpleJobProtocolHandler :
    public URLRequestJobFactory::ProtocolHandler {
 public:
  SimpleJobProtocolHandler() = default;
  URLRequestJob* MaybeCreateJob(
      URLRequest* request,
      NetworkDelegate* network_delegate) const override {
    if (request->url().spec() == "data:empty")
      return new MockSimpleJob(request, network_delegate, "");
    return new MockSimpleJob(request, network_delegate, kTestData);
  }

  ~SimpleJobProtocolHandler() override = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(SimpleJobProtocolHandler);
};

class URLRequestSimpleJobTest : public TestWithScopedTaskEnvironment {
 public:
  URLRequestSimpleJobTest() : context_(true) {
    job_factory_.SetProtocolHandler(
        "data", std::make_unique<SimpleJobProtocolHandler>());
    context_.set_job_factory(&job_factory_);
    context_.Init();

    request_ = context_.CreateRequest(GURL("data:test"), DEFAULT_PRIORITY,
                                      &delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
  }

  void StartRequest(const HttpRequestHeaders* headers) {
    if (headers)
      request_->SetExtraRequestHeaders(*headers);
    request_->Start();

    EXPECT_TRUE(request_->is_pending());
    delegate_.RunUntilComplete();
    EXPECT_FALSE(request_->is_pending());
  }

 protected:
  TestURLRequestContext context_;
  URLRequestJobFactoryImpl job_factory_;
  TestDelegate delegate_;
  std::unique_ptr<URLRequest> request_;

 private:
  DISALLOW_COPY_AND_ASSIGN(URLRequestSimpleJobTest);
};

}  // namespace

TEST_F(URLRequestSimpleJobTest, SimpleRequest) {
  StartRequest(nullptr);
  EXPECT_THAT(delegate_.request_status(), IsOk());
  EXPECT_EQ(kTestData, delegate_.data_received());
}

TEST_F(URLRequestSimpleJobTest, RangeRequest) {
  const std::string kExpectedBody = std::string(
      kTestData + kRangeFirstPosition, kTestData + kRangeLastPosition + 1);
  HttpRequestHeaders headers;
  headers.SetHeader(
      HttpRequestHeaders::kRange,
      HttpByteRange::Bounded(kRangeFirstPosition, kRangeLastPosition)
          .GetHeaderValue());

  StartRequest(&headers);

  EXPECT_THAT(delegate_.request_status(), IsOk());
  EXPECT_EQ(kExpectedBody, delegate_.data_received());
}

TEST_F(URLRequestSimpleJobTest, MultipleRangeRequest) {
  HttpRequestHeaders headers;
  int middle_pos = (kRangeFirstPosition + kRangeLastPosition)/2;
  std::string range = base::StringPrintf("bytes=%d-%d,%d-%d",
                                         kRangeFirstPosition,
                                         middle_pos,
                                         middle_pos + 1,
                                         kRangeLastPosition);
  headers.SetHeader(HttpRequestHeaders::kRange, range);

  StartRequest(&headers);

  EXPECT_TRUE(delegate_.request_failed());
  EXPECT_EQ(ERR_REQUEST_RANGE_NOT_SATISFIABLE, delegate_.request_status());
}

TEST_F(URLRequestSimpleJobTest, InvalidRangeRequest) {
  HttpRequestHeaders headers;
  std::string range = base::StringPrintf(
      "bytes=%d-%d", kRangeLastPosition, kRangeFirstPosition);
  headers.SetHeader(HttpRequestHeaders::kRange, range);

  StartRequest(&headers);

  EXPECT_THAT(delegate_.request_status(), IsOk());
  EXPECT_EQ(kTestData, delegate_.data_received());
}

TEST_F(URLRequestSimpleJobTest, EmptyDataRequest) {
  request_ = context_.CreateRequest(GURL("data:empty"), DEFAULT_PRIORITY,
                                    &delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
  StartRequest(nullptr);
  EXPECT_THAT(delegate_.request_status(), IsOk());
  EXPECT_EQ("", delegate_.data_received());
}

TEST_F(URLRequestSimpleJobTest, CancelBeforeResponseStarts) {
  request_ = context_.CreateRequest(GURL("data:cancel"), DEFAULT_PRIORITY,
                                    &delegate_, TRAFFIC_ANNOTATION_FOR_TESTS);
  request_->Start();
  request_->Cancel();

  base::RunLoop().RunUntilIdle();
  EXPECT_THAT(delegate_.request_status(), IsError(ERR_ABORTED));
  EXPECT_EQ(1, delegate_.response_started_count());
}

TEST_F(URLRequestSimpleJobTest, CancelAfterFirstReadStarted) {
  CancelAfterFirstReadURLRequestDelegate cancel_delegate;
  request_ =
      context_.CreateRequest(GURL("data:cancel"), DEFAULT_PRIORITY,
                             &cancel_delegate, TRAFFIC_ANNOTATION_FOR_TESTS);
  request_->Start();
  cancel_delegate.WaitUntilHeadersReceived();
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(cancel_delegate.request_status(), IsError(ERR_ABORTED));
  EXPECT_EQ(1, cancel_delegate.response_started_count());
  EXPECT_EQ("", cancel_delegate.data_received());
  // Destroy the request so it doesn't outlive its delegate.
  request_.reset();
}

}  // namespace net

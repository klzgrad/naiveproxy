// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/connect_job_test_util.h"

#include "base/logging.h"
#include "base/run_loop.h"
#include "net/socket/stream_socket.h"
#include "net/test/gtest_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

TestConnectJobDelegate::TestConnectJobDelegate() = default;
TestConnectJobDelegate::~TestConnectJobDelegate() = default;

void TestConnectJobDelegate::OnConnectJobComplete(int result, ConnectJob* job) {
  EXPECT_FALSE(has_result_);
  result_ = result;
  socket_ = job->PassSocket();
  // socket_.get() should be nullptr iff result != OK.
  EXPECT_EQ(socket_.get() == nullptr, result != OK);
  has_result_ = true;
  run_loop_.Quit();
}

int TestConnectJobDelegate::WaitForResult() {
  run_loop_.Run();
  DCHECK(has_result_);
  return result_;
}

void TestConnectJobDelegate::StartJobExpectingResult(ConnectJob* connect_job,
                                                     net::Error expected_result,
                                                     bool expect_sync_result) {
  int rv = connect_job->Connect();
  if (expect_sync_result) {
    EXPECT_THAT(rv, test::IsError(expected_result));
    // The callback should not be invoked when a synchronous result is returned.
    base::RunLoop().RunUntilIdle();
    EXPECT_FALSE(has_result_);
  } else {
    EXPECT_FALSE(has_result_);
    EXPECT_THAT(WaitForResult(), test::IsError(expected_result));
    // Make sure the callback isn't invoked again.
    base::RunLoop().RunUntilIdle();
  }
}

}  // namespace net

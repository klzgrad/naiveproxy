// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_CONNECT_JOB_TEST_UTIL_H_
#define NET_SOCKET_CONNECT_JOB_TEST_UTIL_H_

#include <memory>

#include "base/macros.h"
#include "base/run_loop.h"
#include "net/base/net_errors.h"
#include "net/socket/connect_job.h"

namespace net {

class StreamSocket;

class TestConnectJobDelegate : public ConnectJob::Delegate {
 public:
  TestConnectJobDelegate();
  ~TestConnectJobDelegate() override;

  // ConnectJob::Delegate implementation.
  void OnConnectJobComplete(int result, ConnectJob* job) override;

  // Waits for the ConnectJob to complete if it hasn't already and returns the
  // resulting network error code.
  int WaitForResult();

  // Returns true if the ConnectJob has a result.
  bool has_result() const { return has_result_; }

  void StartJobExpectingResult(ConnectJob* connect_job,
                               net::Error expected_result,
                               bool expect_sync_result);

  StreamSocket* socket() { return socket_.get(); }

 private:
  bool has_result_ = false;
  int result_ = ERR_IO_PENDING;
  std::unique_ptr<StreamSocket> socket_;

  base::RunLoop run_loop_;

  DISALLOW_COPY_AND_ASSIGN(TestConnectJobDelegate);
};

}  // namespace net

#endif  // NET_SOCKET_CONNECT_JOB_TEST_UTIL_H_

// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/test/embedded_test_server/simple_connection_listener.h"

#include "base/location.h"
#include "base/sequenced_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace test_server {

SimpleConnectionListener::SimpleConnectionListener(
    int expected_connections,
    AllowAdditionalConnections allow_additional_connections)
    : expected_connections_(expected_connections),
      allow_additional_connections_(allow_additional_connections),
      run_loop_task_runner_(base::ThreadTaskRunnerHandle::Get()) {}

SimpleConnectionListener::~SimpleConnectionListener() = default;

void SimpleConnectionListener::AcceptedSocket(const StreamSocket& socket) {
  ++seen_connections_;
  if (allow_additional_connections_ != ALLOW_ADDITIONAL_CONNECTIONS)
    EXPECT_LE(seen_connections_, expected_connections_);
  if (seen_connections_ == expected_connections_)
    run_loop_task_runner_->PostTask(FROM_HERE, run_loop_.QuitClosure());
}

void SimpleConnectionListener::ReadFromSocket(const StreamSocket& socket,
                                              int rv) {}

void SimpleConnectionListener::WaitForConnections() {
  EXPECT_TRUE(run_loop_task_runner_->RunsTasksInCurrentSequence());
  run_loop_.Run();
}

}  // namespace test_server
}  // namespace net

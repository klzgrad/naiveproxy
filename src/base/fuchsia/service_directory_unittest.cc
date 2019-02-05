// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/service_directory.h"

#include <lib/fdio/util.h>
#include <lib/zx/channel.h>
#include <utility>

#include "base/bind.h"
#include "base/fuchsia/service_directory_test_base.h"
#include "base/location.h"
#include "base/run_loop.h"
#include "base/task_runner.h"
#include "base/test/test_timeouts.h"
#include "base/threading/thread_task_runner_handle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace fuchsia {

class ServiceDirectoryTest : public ServiceDirectoryTestBase {};

// Verifies that ComponentContext can consume a public service in
// ServiceDirectory and that connection is disconnected when the client stub is
// destroyed.
TEST_F(ServiceDirectoryTest, ConnectDisconnect) {
  auto stub = client_context_->ConnectToService<testfidl::TestInterface>();
  VerifyTestInterface(&stub, ZX_OK);

  base::RunLoop run_loop;
  service_binding_->SetOnLastClientCallback(run_loop.QuitClosure());

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](base::RunLoop* run_loop) {
            ADD_FAILURE();
            run_loop->Quit();
          },
          &run_loop),
      TestTimeouts::action_timeout());

  stub.Unbind();
  run_loop.Run();
}

// Verifies that we can connect to the service service more than once.
TEST_F(ServiceDirectoryTest, ConnectMulti) {
  auto stub = client_context_->ConnectToService<testfidl::TestInterface>();
  auto stub2 = client_context_->ConnectToService<testfidl::TestInterface>();
  VerifyTestInterface(&stub, ZX_OK);
  VerifyTestInterface(&stub2, ZX_OK);
}

// Verify that services are also exported to the legacy flat service namespace.
TEST_F(ServiceDirectoryTest, ConnectLegacy) {
  ConnectClientContextToDirectory(".");
  auto stub = client_context_->ConnectToService<testfidl::TestInterface>();
  VerifyTestInterface(&stub, ZX_OK);
}

// Verify that ComponentContext can handle the case when the service directory
// connection is disconnected.
TEST_F(ServiceDirectoryTest, DirectoryGone) {
  service_binding_.reset();
  service_directory_.reset();

  fidl::InterfacePtr<testfidl::TestInterface> stub;
  zx_status_t status =
      client_context_->ConnectToService(FidlInterfaceRequest(&stub));
  EXPECT_EQ(status, ZX_ERR_PEER_CLOSED);

  VerifyTestInterface(&stub, ZX_ERR_PEER_CLOSED);
}

// Verify that the case when the service doesn't exist is handled properly.
TEST_F(ServiceDirectoryTest, NoService) {
  service_binding_.reset();
  auto stub = client_context_->ConnectToService<testfidl::TestInterface>();
  VerifyTestInterface(&stub, ZX_ERR_PEER_CLOSED);
}

}  // namespace fuchsia
}  // namespace base

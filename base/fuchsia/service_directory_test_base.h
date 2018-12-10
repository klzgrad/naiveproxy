// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FUCHSIA_SERVICE_DIRECTORY_TEST_BASE_H_
#define BASE_FUCHSIA_SERVICE_DIRECTORY_TEST_BASE_H_

#include <lib/zx/channel.h>

#include "base/fuchsia/component_context.h"
#include "base/fuchsia/scoped_service_binding.h"
#include "base/fuchsia/testfidl/cpp/fidl.h"
#include "base/message_loop/message_loop.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace fuchsia {

class TestInterfaceImpl : public testfidl::TestInterface {
 public:
  TestInterfaceImpl();
  ~TestInterfaceImpl() override;

  // TestInterface implementation.
  void Add(int32_t a, int32_t b, AddCallback callback) override;
};

class ServiceDirectoryTestBase : public testing::Test {
 public:
  ServiceDirectoryTestBase();
  ~ServiceDirectoryTestBase() override;

  void ConnectClientContextToDirectory(const char* path);
  void VerifyTestInterface(fidl::InterfacePtr<testfidl::TestInterface>* stub,
                           bool expect_error);

 protected:
  MessageLoopForIO message_loop_;
  std::unique_ptr<ServiceDirectory> service_directory_;
  zx::channel service_directory_client_channel_;
  TestInterfaceImpl test_service_;
  std::unique_ptr<ScopedServiceBinding<testfidl::TestInterface>>
      service_binding_;
  std::unique_ptr<ComponentContext> client_context_;
};

}  // namespace fuchsia
}  // namespace base

#endif  // BASE_FUCHSIA_SERVICE_DIRECTORY_TEST_BASE_H_
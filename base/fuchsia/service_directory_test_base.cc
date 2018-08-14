// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/service_directory_test_base.h"

#include <lib/fdio/util.h>

namespace base {
namespace fuchsia {

TestInterfaceImpl::TestInterfaceImpl() = default;
TestInterfaceImpl::~TestInterfaceImpl() = default;

// TestInterface implementation.
void TestInterfaceImpl::Add(int32_t a, int32_t b, AddCallback callback) {
  callback(a + b);
}

ServiceDirectoryTestBase::ServiceDirectoryTestBase() {
  zx::channel service_directory_channel;
  EXPECT_EQ(zx::channel::create(0, &service_directory_channel,
                                &service_directory_client_channel_),
            ZX_OK);

  // Mount service dir and publish the service.
  service_directory_ =
      std::make_unique<ServiceDirectory>(std::move(service_directory_channel));
  service_binding_ =
      std::make_unique<ScopedServiceBinding<testfidl::TestInterface>>(
          service_directory_.get(), &test_service_);

  ConnectClientContextToDirectory("public");
}

ServiceDirectoryTestBase::~ServiceDirectoryTestBase() = default;

void ServiceDirectoryTestBase::ConnectClientContextToDirectory(
    const char* path) {
  // Open directory |path| from the service directory.
  zx::channel public_directory_channel;
  zx::channel public_directory_client_channel;
  EXPECT_EQ(zx::channel::create(0, &public_directory_channel,
                                &public_directory_client_channel),
            ZX_OK);
  EXPECT_EQ(fdio_open_at(service_directory_client_channel_.get(), path, 0,
                         public_directory_channel.release()),
            ZX_OK);

  // Create ComponentContext and connect to the test service.
  client_context_ = std::make_unique<ComponentContext>(
      std::move(public_directory_client_channel));
}

void ServiceDirectoryTestBase::VerifyTestInterface(
    fidl::InterfacePtr<testfidl::TestInterface>* stub,
    bool expect_error) {
  // Call the service and wait for response.
  base::RunLoop run_loop;
  bool error = false;

  stub->set_error_handler([&run_loop, &error]() {
    error = true;
    run_loop.Quit();
  });

  (*stub)->Add(2, 2, [&run_loop](int32_t result) {
    EXPECT_EQ(result, 4);
    run_loop.Quit();
  });

  run_loop.Run();

  EXPECT_EQ(error, expect_error);

  // Reset error handler because the current one captures |run_loop| and
  // |error| references which are about to be destroyed.
  stub->set_error_handler([]() {});
}

}  // namespace fuchsia
}  // namespace base

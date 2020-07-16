// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/test_component_context_for_process.h"

#include <fuchsia/intl/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include "base/fuchsia/default_context.h"
#include "base/fuchsia/fuchsia_logging.h"
#include "base/fuchsia/scoped_service_binding.h"
#include "base/fuchsia/testfidl/cpp/fidl.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

class TestComponentContextForProcessTest
    : public testing::Test,
      public fuchsia::testfidl::TestInterface {
 public:
  TestComponentContextForProcessTest()
      : task_environment_(base::test::TaskEnvironment::MainThreadType::IO) {}

  bool HasTestInterface() {
    return VerifyTestInterface(
        fuchsia::ComponentContextForCurrentProcess()
            ->svc()
            ->Connect<fuchsia::testfidl::TestInterface>());
  }

  bool HasPublishedTestInterface() {
    return VerifyTestInterface(
        test_context_.published_services()
            ->Connect<fuchsia::testfidl::TestInterface>());
  }

  // fuchsia::testfidl::TestInterface implementation.
  void Add(int32_t a, int32_t b, AddCallback callback) override {
    callback(a + b);
  }

 protected:
  bool VerifyTestInterface(fuchsia::testfidl::TestInterfacePtr test_interface) {
    bool have_interface = false;
    RunLoop wait_loop;
    test_interface.set_error_handler([quit_loop = wait_loop.QuitClosure(),
                                      &have_interface](zx_status_t status) {
      ZX_CHECK(status == ZX_ERR_PEER_CLOSED, status);
      have_interface = false;
      quit_loop.Run();
    });
    test_interface->Add(
        45, 6,
        [quit_loop = wait_loop.QuitClosure(), &have_interface](int32_t result) {
          EXPECT_EQ(result, 45 + 6);
          have_interface = true;
          quit_loop.Run();
        });
    wait_loop.Run();
    return have_interface;
  }

  const base::test::SingleThreadTaskEnvironment task_environment_;

  base::TestComponentContextForProcess test_context_;
};

TEST_F(TestComponentContextForProcessTest, NoServices) {
  // No services should be available.
  EXPECT_FALSE(HasTestInterface());
}

TEST_F(TestComponentContextForProcessTest, InjectTestInterface) {
  // Publish a fake TestInterface for the process' ComponentContext to expose.
  base::fuchsia::ScopedServiceBinding<fuchsia::testfidl::TestInterface>
      service_binding(test_context_.additional_services(), this);

  // Verify that the TestInterface is accessible & usable.
  EXPECT_TRUE(HasTestInterface());
}

TEST_F(TestComponentContextForProcessTest, PublishTestInterface) {
  // Publish TestInterface to the process' outgoing-directory.
  base::fuchsia::ScopedServiceBinding<fuchsia::testfidl::TestInterface>
      service_binding(
          fuchsia::ComponentContextForCurrentProcess()->outgoing().get(), this);

  // Attempt to use the TestInterface from the outgoing-directory.
  EXPECT_TRUE(HasPublishedTestInterface());
}

TEST_F(TestComponentContextForProcessTest, ProvideSystemService) {
  // Expose fuchsia.device.NameProvider through the ComponentContext.
  const base::StringPiece kServiceNames[] = {
      ::fuchsia::intl::PropertyProvider::Name_};
  test_context_.AddServices(kServiceNames);

  // Attempt to use the PropertyProvider via the process ComponentContext.
  RunLoop wait_loop;
  auto property_provider = fuchsia::ComponentContextForCurrentProcess()
                               ->svc()
                               ->Connect<::fuchsia::intl::PropertyProvider>();
  property_provider.set_error_handler(
      [quit_loop = wait_loop.QuitClosure()](zx_status_t status) {
        if (status == ZX_ERR_PEER_CLOSED) {
          ADD_FAILURE() << "PropertyProvider disconnected; probably not found.";
        } else {
          ZX_LOG(FATAL, status);
        }
        quit_loop.Run();
      });
  property_provider->GetProfile(
      [quit_loop = wait_loop.QuitClosure()](::fuchsia::intl::Profile profile) {
        quit_loop.Run();
      });
  wait_loop.Run();
}

}  // namespace base

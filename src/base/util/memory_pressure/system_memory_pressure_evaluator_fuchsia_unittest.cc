// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/util/memory_pressure/system_memory_pressure_evaluator_fuchsia.h"

#include <fuchsia/memorypressure/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl_test_base.h>

#include "base/fuchsia/scoped_service_binding.h"
#include "base/fuchsia/test_component_context_for_process.h"
#include "base/run_loop.h"
#include "base/test/task_environment.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace util {

namespace {
class MockMemoryPressureVoter : public MemoryPressureVoter {
 public:
  MOCK_METHOD2(SetVote,
               void(base::MemoryPressureListener::MemoryPressureLevel, bool));
};
}  // namespace

class SystemMemoryPressureEvaluatorFuchsiaTest
    : public testing::Test,
      public fuchsia::memorypressure::testing::Provider_TestBase {
 public:
  SystemMemoryPressureEvaluatorFuchsiaTest()
      : task_environment_(base::test::TaskEnvironment::MainThreadType::IO) {}

  void SendPressureLevel(fuchsia::memorypressure::Level level) {
    base::RunLoop wait_loop;
    watcher_->OnLevelChanged(
        level, [quit_loop = wait_loop.QuitClosure()]() { quit_loop.Run(); });
    wait_loop.Run();
  }

  bool have_watcher() const { return watcher_.is_bound(); }

  // fuchsia::memorypressure::Provider implementation.
  void RegisterWatcher(fidl::InterfaceHandle<fuchsia::memorypressure::Watcher>
                           watcher) override {
    watcher_.Bind(std::move(watcher));
    SendPressureLevel(fuchsia::memorypressure::Level::NORMAL);
  }

 protected:
  // fuchsia::memorypressure::testing::Provider_TestBase implementation.
  void NotImplemented_(const std::string& name) override {
    ADD_FAILURE() << "Unexpected call to method: " << name;
  }

  const base::test::SingleThreadTaskEnvironment task_environment_;

  base::TestComponentContextForProcess test_context_;

  fuchsia::memorypressure::WatcherPtr watcher_;
};

TEST_F(SystemMemoryPressureEvaluatorFuchsiaTest, ProviderUnavailable) {
  auto voter = std::make_unique<MockMemoryPressureVoter>();
  SystemMemoryPressureEvaluatorFuchsia evaluator(std::move(voter));

  // Spin the loop to allow the evaluator to notice that the Provider is not
  // available, to verify that that doesn't trigger a fatal failure.
  base::RunLoop().RunUntilIdle();
}

TEST_F(SystemMemoryPressureEvaluatorFuchsiaTest, Basic) {
  base::fuchsia::ScopedServiceBinding<::fuchsia::memorypressure::Provider>
      publish_provider(test_context_.additional_services(), this);

  auto voter = std::make_unique<MockMemoryPressureVoter>();
  // NONE pressure will be reported once the first Watch() call returns, and
  // then again when the fakes system level transitions from CRITICAL->NORMAL.
  EXPECT_CALL(
      *voter,
      SetVote(base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE, false))
      .Times(2);
  EXPECT_CALL(
      *voter,
      SetVote(base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL,
              true));
  EXPECT_CALL(
      *voter,
      SetVote(base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE,
              true));

  SystemMemoryPressureEvaluatorFuchsia evaluator(std::move(voter));

  // Spin the loop to ensure that RegisterWatcher() is processed.
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(have_watcher());

  SendPressureLevel(fuchsia::memorypressure::Level::CRITICAL);
  EXPECT_EQ(evaluator.current_vote(),
            base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_CRITICAL);

  SendPressureLevel(fuchsia::memorypressure::Level::NORMAL);
  EXPECT_EQ(evaluator.current_vote(),
            base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_NONE);

  SendPressureLevel(fuchsia::memorypressure::Level::WARNING);
  EXPECT_EQ(evaluator.current_vote(),
            base::MemoryPressureListener::MEMORY_PRESSURE_LEVEL_MODERATE);
}

}  // namespace util

// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/mac/mach_port_broker.h"

#include "base/command_line.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/multiprocess_test.h"
#include "base/test/test_timeouts.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/multiprocess_func_list.h"

namespace base {

namespace {
const char kBootstrapPortName[] = "thisisatest";
}

class MachPortBrokerTest : public testing::Test,
                           public base::PortProvider::Observer {
 public:
  MachPortBrokerTest()
      : broker_(kBootstrapPortName),
        event_(base::WaitableEvent::ResetPolicy::MANUAL,
               base::WaitableEvent::InitialState::NOT_SIGNALED),
        received_process_(kNullProcessHandle) {
    broker_.AddObserver(this);
  }
  ~MachPortBrokerTest() override {
    broker_.RemoveObserver(this);
  }

  // Helper function to acquire/release locks and call |PlaceholderForPid()|.
  void AddPlaceholderForPid(base::ProcessHandle pid) {
    base::AutoLock lock(broker_.GetLock());
    broker_.AddPlaceholderForPid(pid);
  }

  // Helper function to acquire/release locks and call |FinalizePid()|.
  void FinalizePid(base::ProcessHandle pid,
                   mach_port_t task_port) {
    base::AutoLock lock(broker_.GetLock());
    broker_.FinalizePid(pid, task_port);
  }

  void WaitForTaskPort() {
    event_.Wait();
  }

  // base::PortProvider::Observer:
  void OnReceivedTaskPort(ProcessHandle process) override {
    received_process_ = process;
    event_.Signal();
  }

 protected:
  MachPortBroker broker_;
  WaitableEvent event_;
  ProcessHandle received_process_;
};

TEST_F(MachPortBrokerTest, Locks) {
  // Acquire and release the locks.  Nothing bad should happen.
  base::AutoLock lock(broker_.GetLock());
}

TEST_F(MachPortBrokerTest, AddPlaceholderAndFinalize) {
  // Add a placeholder for PID 1.
  AddPlaceholderForPid(1);
  EXPECT_EQ(0u, broker_.TaskForPid(1));

  // Finalize PID 1.
  FinalizePid(1, 100u);
  EXPECT_EQ(100u, broker_.TaskForPid(1));

  // Should be no entry for PID 2.
  EXPECT_EQ(0u, broker_.TaskForPid(2));
}

TEST_F(MachPortBrokerTest, FinalizeUnknownPid) {
  // Finalizing an entry for an unknown pid should not add it to the map.
  FinalizePid(1u, 100u);
  EXPECT_EQ(0u, broker_.TaskForPid(1u));
}

MULTIPROCESS_TEST_MAIN(MachPortBrokerTestChild) {
  CHECK(base::MachPortBroker::ChildSendTaskPortToParent(kBootstrapPortName));
  return 0;
}

TEST_F(MachPortBrokerTest, ReceivePortFromChild) {
  ASSERT_TRUE(broker_.Init());
  CommandLine command_line(
      base::GetMultiProcessTestChildBaseCommandLine());
  broker_.GetLock().Acquire();
  base::Process test_child_process = base::SpawnMultiProcessTestChild(
      "MachPortBrokerTestChild", command_line, LaunchOptions());
  broker_.AddPlaceholderForPid(test_child_process.Handle());
  broker_.GetLock().Release();

  WaitForTaskPort();
  EXPECT_EQ(test_child_process.Handle(), received_process_);

  int rv = -1;
  ASSERT_TRUE(test_child_process.WaitForExitWithTimeout(
      TestTimeouts::action_timeout(), &rv));
  EXPECT_EQ(0, rv);

  EXPECT_NE(static_cast<mach_port_t>(MACH_PORT_NULL),
            broker_.TaskForPid(test_child_process.Handle()));
}

TEST_F(MachPortBrokerTest, ReceivePortFromChildWithoutAdding) {
  ASSERT_TRUE(broker_.Init());
  CommandLine command_line(
      base::GetMultiProcessTestChildBaseCommandLine());
  broker_.GetLock().Acquire();
  base::Process test_child_process = base::SpawnMultiProcessTestChild(
      "MachPortBrokerTestChild", command_line, LaunchOptions());
  broker_.GetLock().Release();

  int rv = -1;
  ASSERT_TRUE(test_child_process.WaitForExitWithTimeout(
      TestTimeouts::action_timeout(), &rv));
  EXPECT_EQ(0, rv);

  EXPECT_EQ(static_cast<mach_port_t>(MACH_PORT_NULL),
            broker_.TaskForPid(test_child_process.Handle()));
}

}  // namespace base

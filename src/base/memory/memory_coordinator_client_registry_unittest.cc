// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/memory_coordinator_client_registry.h"

#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

namespace {

class TestMemoryCoordinatorClient : public MemoryCoordinatorClient {
 public:
  void OnMemoryStateChange(MemoryState state) override { state_ = state; }

  void OnPurgeMemory() override { ++purge_count_; }

  MemoryState state() const { return state_; }
  size_t purge_count() const { return purge_count_; }

 private:
  MemoryState state_ = MemoryState::UNKNOWN;
  size_t purge_count_ = 0;
};

void RunUntilIdle() {
  base::RunLoop loop;
  loop.RunUntilIdle();
}

TEST(MemoryCoordinatorClientRegistryTest, NotifyStateChange) {
  MessageLoop loop;
  auto* registry = MemoryCoordinatorClientRegistry::GetInstance();
  TestMemoryCoordinatorClient client;
  registry->Register(&client);
  registry->Notify(MemoryState::THROTTLED);
  RunUntilIdle();
  ASSERT_EQ(MemoryState::THROTTLED, client.state());
  registry->Unregister(&client);
}

TEST(MemoryCoordinatorClientRegistryTest, PurgeMemory) {
  MessageLoop loop;
  auto* registry = MemoryCoordinatorClientRegistry::GetInstance();
  TestMemoryCoordinatorClient client;
  registry->Register(&client);
  registry->PurgeMemory();
  RunUntilIdle();
  ASSERT_EQ(1u, client.purge_count());
  registry->Unregister(&client);
}

}  // namespace

}  // namespace base

// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/allocator/partition_allocator/spin_lock.h"

#include <memory>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/threading/thread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace subtle {

static constexpr size_t kBufferSize = 16;

static void FillBuffer(volatile char* buffer, char fill_pattern) {
  for (size_t i = 0; i < kBufferSize; ++i)
    buffer[i] = fill_pattern;
}

static void ChangeAndCheckBuffer(volatile char* buffer) {
  FillBuffer(buffer, '\0');
  int total = 0;
  for (size_t i = 0; i < kBufferSize; ++i)
    total += buffer[i];

  EXPECT_EQ(0, total);

  // This will mess with the other thread's calculation if we accidentally get
  // concurrency.
  FillBuffer(buffer, '!');
}

static void ThreadMain(SpinLock* lock, volatile char* buffer) {
  for (int i = 0; i < 500000; ++i) {
    SpinLock::Guard guard(*lock);
    ChangeAndCheckBuffer(buffer);
  }
}

TEST(SpinLockTest, Torture) {
  SpinLock lock;
  char shared_buffer[kBufferSize];

  Thread thread1("thread1");
  Thread thread2("thread2");

  thread1.StartAndWaitForTesting();
  thread2.StartAndWaitForTesting();

  thread1.task_runner()->PostTask(
      FROM_HERE, BindOnce(&ThreadMain, Unretained(&lock),
                          Unretained(static_cast<char*>(shared_buffer))));
  thread2.task_runner()->PostTask(
      FROM_HERE, BindOnce(&ThreadMain, Unretained(&lock),
                          Unretained(static_cast<char*>(shared_buffer))));
}
}  // namespace subtle
}  // namespace base

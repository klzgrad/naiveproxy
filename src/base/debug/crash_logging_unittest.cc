// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/debug/crash_logging.h"

#include "testing/gtest/include/gtest/gtest.h"

TEST(CrashLoggingTest, UninitializedCrashKeyStringSupport) {
  auto* crash_key = base::debug::AllocateCrashKeyString(
      "test", base::debug::CrashKeySize::Size32);
  EXPECT_FALSE(crash_key);

  base::debug::SetCrashKeyString(crash_key, "value");

  base::debug::ClearCrashKeyString(crash_key);
}

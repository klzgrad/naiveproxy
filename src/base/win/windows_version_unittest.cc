// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/windows_version.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace win {
namespace {

TEST(WindowsVersion, GetVersionExAndKernelVersionMatch) {
  // If this fails, we're running in compatibility mode, or need to update the
  // application manifest.
  EXPECT_EQ(OSInfo::GetInstance()->version(),
            OSInfo::GetInstance()->Kernel32Version());
}

}  // namespace
}  // namespace win
}  // namespace base

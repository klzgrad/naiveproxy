// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_info.h"

#include "base/time/time.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

// See https://crbug.com/726484 for Fuchsia.
// Cannot read boot time on Android O, crbug.com/788870.
#if !defined(OS_IOS) && !defined(OS_FUCHSIA) && !defined(OS_ANDROID)
TEST(ProcessInfoTest, CreationTime) {
  Time creation_time = CurrentProcessInfo::CreationTime();
  ASSERT_FALSE(creation_time.is_null());
}
#endif  // !defined(OS_IOS) && !defined(OS_FUCHSIA) && !defined(OS_ANDROID)

}  // namespace base

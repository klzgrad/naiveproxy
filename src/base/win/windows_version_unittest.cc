// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/windows_version.h"

#include "base/logging.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace win {

TEST(WindowsVersion, GetVersionExAndKernelVersionMatch) {
  // If this fails, we're running in compatibility mode, or need to update the
  // application manifest.
  EXPECT_EQ(OSInfo::GetInstance()->version(),
            OSInfo::GetInstance()->Kernel32Version());
}

TEST(OSInfo, MajorMinorBuildToVersion) {
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(10, 0, 32767), VERSION_WIN10_RS5);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(10, 0, 17763), VERSION_WIN10_RS5);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(10, 0, 17134), VERSION_WIN10_RS4);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(10, 0, 16299), VERSION_WIN10_RS3);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(10, 0, 15063), VERSION_WIN10_RS2);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(10, 0, 14393), VERSION_WIN10_RS1);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(10, 0, 10586), VERSION_WIN10_TH2);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(10, 0, 10240), VERSION_WIN10);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(10, 0, 0), VERSION_WIN10);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(6, 3, 0), VERSION_WIN8_1);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(6, 2, 0), VERSION_WIN8);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(6, 1, 0), VERSION_WIN7);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(6, 0, 0), VERSION_VISTA);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(5, 3, 0), VERSION_SERVER_2003);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(5, 2, 0), VERSION_SERVER_2003);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(5, 1, 0), VERSION_XP);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(5, 0, 0), VERSION_PRE_XP);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(4, 0, 0), VERSION_PRE_XP);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(3, 0, 0), VERSION_PRE_XP);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(2, 0, 0), VERSION_PRE_XP);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(1, 0, 0), VERSION_PRE_XP);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(0, 0, 0), VERSION_PRE_XP);

#if !DCHECK_IS_ON()
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(11, 0, 0), VERSION_WIN_LAST);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(9, 0, 0), VERSION_WIN_LAST);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(8, 0, 0), VERSION_WIN_LAST);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(7, 0, 0), VERSION_WIN_LAST);
  EXPECT_EQ(OSInfo::MajorMinorBuildToVersion(6, 4, 0), VERSION_WIN8_1);
#endif  // !DCHECK_IS_ON()
}

}  // namespace win
}  // namespace base

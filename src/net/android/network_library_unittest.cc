// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/android/network_library.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace android {

TEST(NetworkLibraryTest, CaptivePortal) {
  EXPECT_FALSE(android::GetIsCaptivePortal());
}

}  // namespace android

}  // namespace net

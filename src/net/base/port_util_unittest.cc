// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/port_util.h"

#include <string>

#include "base/stl_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

TEST(NetUtilTest, SetExplicitlyAllowedPortsTest) {
  std::string invalid[] = {"1,2,a", "'1','2'", "1, 2, 3", "1 0,11,12"};
  std::string valid[] = {"", "1", "1,2", "1,2,3", "10,11,12,13"};

  for (size_t i = 0; i < base::size(invalid); ++i) {
    SetExplicitlyAllowedPorts(invalid[i]);
    EXPECT_EQ(0, static_cast<int>(GetCountOfExplicitlyAllowedPorts()));
  }

  for (size_t i = 0; i < base::size(valid); ++i) {
    SetExplicitlyAllowedPorts(valid[i]);
    EXPECT_EQ(i, GetCountOfExplicitlyAllowedPorts());
  }
}

}  // namespace net

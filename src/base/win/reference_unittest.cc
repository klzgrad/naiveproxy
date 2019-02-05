// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/win/reference.h"

#include <windows.foundation.h>
#include <wrl/client.h>

#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace win {

namespace {

using Microsoft::WRL::Make;

}  // namespace

TEST(ReferenceTest, Value) {
  auto ref = Make<Reference<int>>(123);
  int value = 0;
  HRESULT hr = ref->get_Value(&value);
  EXPECT_TRUE(SUCCEEDED(hr));
  EXPECT_EQ(123, value);
}

TEST(ReferenceTest, ValueAggregate) {
  auto ref = Make<Reference<bool>>(true);
  boolean value = false;
  HRESULT hr = ref->get_Value(&value);
  EXPECT_TRUE(SUCCEEDED(hr));
  EXPECT_TRUE(value);
}

}  // namespace win
}  // namespace base

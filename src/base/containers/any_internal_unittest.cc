// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/containers/any_internal.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace internal {

TEST(AnyInternalTest, InlineOrOutlineStorage) {
  static_assert(AnyInternal::InlineStorageHelper<int>::kUseInlineStorage,
                "int should be stored inline");
  static_assert(AnyInternal::InlineStorageHelper<int*>::kUseInlineStorage,
                "int* should be stored inline");
  static_assert(
      AnyInternal::InlineStorageHelper<std::unique_ptr<int>>::kUseInlineStorage,
      "std::unique_ptr<int> should be stored inline");
  static_assert(
      !AnyInternal::InlineStorageHelper<std::string>::kUseInlineStorage,
      "std::string should be stored out of line");
}

}  // namespace internal
}  // namespace base

// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_WITH_FEATURE_OVERRIDE_H_
#define BASE_TEST_WITH_FEATURE_OVERRIDE_H_

#include "base/feature_list.h"
#include "base/macros.h"
#include "base/test/scoped_feature_list.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace test {

#define INSTANTIATE_FEATURE_OVERRIDE_TEST_SUITE(test_name) \
  INSTANTIATE_TEST_SUITE_P(All, test_name, testing::Values(false, true))

// Base class for a test fixture that must run with a feature enabled and
// disabled. Must be the first base class of the test fixture to take effect
// during the construction of the test fixture itself.
//
// Example usage:
//
//  class MyTest : public base::WithFeatureOverride, public testing::Test {
//   public:
//    MyTest() : WithFeatureOverride(kMyFeature){}
//  };
//
//  TEST_P(MyTest, FooBar) {
//    This will run with both the kMyFeature enabled and disabled.
//  }
//
//  INSTANTIATE_FEATURE_OVERRIDE_TEST_SUITE(MyTest);

class WithFeatureOverride : public testing::WithParamInterface<bool> {
 public:
  explicit WithFeatureOverride(const base::Feature& feature);
  ~WithFeatureOverride();

  WithFeatureOverride(const WithFeatureOverride&) = delete;
  WithFeatureOverride& operator=(const WithFeatureOverride&) = delete;

  // Use to know if the configured feature provided in the ctor is enabled or
  // not.
  bool IsParamFeatureEnabled();

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

}  // namespace test
}  // namespace base

#endif  // BASE_TEST_WITH_FEATURE_OVERRIDE_H_

// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_GTEST_PROD_UTIL_H_
#define BASE_GTEST_PROD_UTIL_H_

// TODO: Remove me.
#define FRIEND_TEST_ALL_PREFIXES(test_case_name, test_name) \
  friend class test_case_name##test_name

// TODO: Remove me.
#define FORWARD_DECLARE_TEST(test_case_name, test_name) \
  class test_case_name##test_name

#endif  // BASE_GTEST_PROD_UTIL_H_

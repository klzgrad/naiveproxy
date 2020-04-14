// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/metrics/metrics_hashes.h"

#include <stddef.h>
#include <stdint.h>

#include "base/format_macros.h"
#include "base/stl_util.h"
#include "base/strings/stringprintf.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

// Make sure our ID hashes are the same as what we see on the server side.
TEST(MetricsUtilTest, HashMetricName) {
  static const struct {
    std::string input;
    std::string output;
  } cases[] = {
    {"Back", "0x0557fa923dcee4d0"},
    {"Forward", "0x67d2f6740a8eaebf"},
    {"NewTab", "0x290eb683f96572f1"},
  };

  for (size_t i = 0; i < base::size(cases); ++i) {
    uint64_t hash = HashMetricName(cases[i].input);
    std::string hash_hex = base::StringPrintf("0x%016" PRIx64, hash);
    EXPECT_EQ(cases[i].output, hash_hex);
  }
}

}  // namespace metrics

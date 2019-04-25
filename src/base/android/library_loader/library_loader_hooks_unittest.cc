// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/library_loader/library_loader_hooks.h"

#include "base/command_line.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace android {

TEST(LibraryLoaderHooksTest, TestTrialSelection) {
  CommandLine command_line(
      {"_", "--first=on", "--second=off", "--third=maybe"});

  EXPECT_TRUE(internal::GetRandomizedTrial("first", &command_line));
  EXPECT_FALSE(internal::GetRandomizedTrial("second", &command_line));
  EXPECT_TRUE(internal::GetRandomizedTrial("third", &command_line));
}

TEST(LibraryLoaderHooksTest, TestFlagNotSpecified) {
  int enabled_count = 0;
  const int kTrials = 100;
  for (int i = 0; i < kTrials; i++) {
    CommandLine command_line({"_", "--flag"});
    if (internal::GetRandomizedTrial("flag", &command_line)) {
      ++enabled_count;
      EXPECT_EQ("on", command_line.GetSwitchValueASCII("flag"));
    } else {
      EXPECT_EQ("off", command_line.GetSwitchValueASCII("flag"));
    }
  }
  // If the flag is not specified, enabling the trial is chosen randomly. There
  // should be at least 1 enabled trial and at least one disabled trial.
  EXPECT_GT(enabled_count, 0);
  EXPECT_LT(enabled_count, kTrials);
}

TEST(LibraryLoaderHooksTest, TestFlagNotPresent) {
  bool saw_enabled = false, saw_disabled = false;
  while (!saw_enabled || !saw_disabled) {
    CommandLine command_line({"_", "--unused"});
    // TrialSelection should add the flag to the command line.
    if (internal::GetRandomizedTrial("missing", &command_line)) {
      saw_enabled = true;
      EXPECT_EQ("on", command_line.GetSwitchValueASCII("missing"));
    } else {
      saw_disabled = true;
      EXPECT_EQ("off", command_line.GetSwitchValueASCII("missing"));
    }
  }
  // If this test times out, then there is a bug with either enabling or
  // disabling a flag not present.
}

}  // namespace android
}  // namespace base

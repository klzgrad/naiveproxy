// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/i18n/icu_util.h"

#include "base/environment.h"
#include "base/files/file_util.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/icu/source/common/unicode/uclean.h"
#include "third_party/icu/source/i18n/unicode/timezone.h"

namespace base {
namespace i18n {

class TimeZoneDataTest : public testing::Test {
 protected:
  TimeZoneDataTest() : env_(base::Environment::Create()) {}

  void TearDown() override { u_cleanup(); }

  void GetActualRevision(std::string* icu_version) {
    UErrorCode err = U_ZERO_ERROR;
    *icu_version = icu::TimeZone::getTZDataVersion(err);
    ASSERT_EQ(U_ZERO_ERROR, err) << u_errorName(err);
  }

  std::unique_ptr<base::Environment> env_;
};

TEST_F(TimeZoneDataTest, RevisionFromConfig) {
  // Config data is not available on Chromium test runners, so don't actually
  // run this test there.  A marker for this is the presence of revision.txt.
  if (!base::PathExists(base::FilePath("/config/data/tzdata/revision.txt")))
    return;

  // Ensure that timezone data is loaded from the default location.
  ASSERT_TRUE(env_->UnSetVar("ICU_TIMEZONE_FILES_DIR"));

  InitializeICU();
  std::string expected;
  ASSERT_TRUE(base::ReadFileToString(
      base::FilePath("/config/data/tzdata/revision.txt"), &expected));
  std::string actual;
  GetActualRevision(&actual);
  EXPECT_EQ(expected, actual);
}

TEST_F(TimeZoneDataTest, RevisionFromTestData) {
  // Ensure that timezone data is loaded from test data.
  ASSERT_TRUE(env_->SetVar("ICU_TIMEZONE_FILES_DIR",
                           "/pkg/base/test/data/tzdata/2019a/44/le"));

  InitializeICU();
  std::string actual;
  GetActualRevision(&actual);
  EXPECT_EQ("2019a", actual);
}

}  // namespace i18n
}  // namespace base

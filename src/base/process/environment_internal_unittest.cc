// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/environment_internal.h"

#include <memory>
#include <vector>

#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

using EnvironmentInternalTest = PlatformTest;

namespace base {
namespace internal {

#if defined(OS_WIN)

namespace {
void ExpectEnvironmentBlock(const std::vector<string16>& vars,
                            const string16& block) {
  string16 expected;
  for (const auto& var : vars) {
    expected += var;
    expected.push_back('\0');
  }
  expected.push_back('\0');
  EXPECT_EQ(expected, block);
}
}  // namespace

TEST_F(EnvironmentInternalTest, AlterEnvironment) {
  const char16 empty[] = {'\0'};
  const char16 a2[] = {'A', '=', '2', '\0', '\0'};
  const char16 a2b3[] = {'A', '=', '2', '\0', 'B', '=', '3', '\0', '\0'};
  EnvironmentMap changes;
  NativeEnvironmentString e;

  e = AlterEnvironment(empty, changes);
  ExpectEnvironmentBlock({}, e);

  changes[STRING16_LITERAL("A")] = STRING16_LITERAL("1");
  e = AlterEnvironment(empty, changes);
  ExpectEnvironmentBlock({STRING16_LITERAL("A=1")}, e);

  changes.clear();
  changes[STRING16_LITERAL("A")] = string16();
  e = AlterEnvironment(empty, changes);
  ExpectEnvironmentBlock({}, e);

  changes.clear();
  e = AlterEnvironment(a2, changes);
  ExpectEnvironmentBlock({STRING16_LITERAL("A=2")}, e);

  changes.clear();
  changes[STRING16_LITERAL("A")] = STRING16_LITERAL("1");
  e = AlterEnvironment(a2, changes);
  ExpectEnvironmentBlock({STRING16_LITERAL("A=1")}, e);

  changes.clear();
  changes[STRING16_LITERAL("A")] = string16();
  e = AlterEnvironment(a2, changes);
  ExpectEnvironmentBlock({}, e);

  changes.clear();
  changes[STRING16_LITERAL("A")] = string16();
  changes[STRING16_LITERAL("B")] = string16();
  e = AlterEnvironment(a2b3, changes);
  ExpectEnvironmentBlock({}, e);

  changes.clear();
  changes[STRING16_LITERAL("A")] = string16();
  e = AlterEnvironment(a2b3, changes);
  ExpectEnvironmentBlock({STRING16_LITERAL("B=3")}, e);

  changes.clear();
  changes[STRING16_LITERAL("B")] = string16();
  e = AlterEnvironment(a2b3, changes);
  ExpectEnvironmentBlock({STRING16_LITERAL("A=2")}, e);

  changes.clear();
  changes[STRING16_LITERAL("A")] = STRING16_LITERAL("1");
  changes[STRING16_LITERAL("C")] = STRING16_LITERAL("4");
  e = AlterEnvironment(a2b3, changes);
  // AlterEnvironment() currently always puts changed entries at the end.
  ExpectEnvironmentBlock({STRING16_LITERAL("B=3"), STRING16_LITERAL("A=1"),
                          STRING16_LITERAL("C=4")},
                         e);
}

#else  // !OS_WIN

TEST_F(EnvironmentInternalTest, AlterEnvironment) {
  const char* const empty[] = {nullptr};
  const char* const a2[] = {"A=2", nullptr};
  const char* const a2b3[] = {"A=2", "B=3", nullptr};
  EnvironmentMap changes;
  std::unique_ptr<char*[]> e;

  e = AlterEnvironment(empty, changes);
  EXPECT_TRUE(e[0] == nullptr);

  changes["A"] = "1";
  e = AlterEnvironment(empty, changes);
  EXPECT_EQ(std::string("A=1"), e[0]);
  EXPECT_TRUE(e[1] == nullptr);

  changes.clear();
  changes["A"] = std::string();
  e = AlterEnvironment(empty, changes);
  EXPECT_TRUE(e[0] == nullptr);

  changes.clear();
  e = AlterEnvironment(a2, changes);
  EXPECT_EQ(std::string("A=2"), e[0]);
  EXPECT_TRUE(e[1] == nullptr);

  changes.clear();
  changes["A"] = "1";
  e = AlterEnvironment(a2, changes);
  EXPECT_EQ(std::string("A=1"), e[0]);
  EXPECT_TRUE(e[1] == nullptr);

  changes.clear();
  changes["A"] = std::string();
  e = AlterEnvironment(a2, changes);
  EXPECT_TRUE(e[0] == nullptr);

  changes.clear();
  changes["A"] = std::string();
  changes["B"] = std::string();
  e = AlterEnvironment(a2b3, changes);
  EXPECT_TRUE(e[0] == nullptr);

  changes.clear();
  changes["A"] = std::string();
  e = AlterEnvironment(a2b3, changes);
  EXPECT_EQ(std::string("B=3"), e[0]);
  EXPECT_TRUE(e[1] == nullptr);

  changes.clear();
  changes["B"] = std::string();
  e = AlterEnvironment(a2b3, changes);
  EXPECT_EQ(std::string("A=2"), e[0]);
  EXPECT_TRUE(e[1] == nullptr);

  changes.clear();
  changes["A"] = "1";
  changes["C"] = "4";
  e = AlterEnvironment(a2b3, changes);
  EXPECT_EQ(std::string("B=3"), e[0]);
  // AlterEnvironment() currently always puts changed entries at the end.
  EXPECT_EQ(std::string("A=1"), e[1]);
  EXPECT_EQ(std::string("C=4"), e[2]);
  EXPECT_TRUE(e[3] == nullptr);
}

#endif  // OS_WIN

}  // namespace internal
}  // namespace base

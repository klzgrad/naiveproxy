// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "base/util/type_safety/id_type.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace util {

namespace {

class Foo;
using FooId = IdType<Foo, int, 0>;

}  // namespace

TEST(IdType, DefaultValueIsInvalid) {
  FooId foo_id;
  EXPECT_TRUE(foo_id.is_null());
}

TEST(IdType, NormalValueIsValid) {
  FooId foo_id = FooId::FromUnsafeValue(123);
  EXPECT_FALSE(foo_id.is_null());
}

TEST(IdType, Generator) {
  FooId::Generator foo_id_generator;
  for (int i = 1; i < 10; i++)
    EXPECT_EQ(foo_id_generator.GenerateNextId(), FooId::FromUnsafeValue(i));
}

TEST(IdType, GeneratorWithNonZeroInvalidValue) {
  using TestId = IdType<class TestIdTag, int, -1>;

  TestId::Generator test_id_generator;
  for (int i = 0; i < 10; i++)
    EXPECT_EQ(test_id_generator.GenerateNextId(), TestId::FromUnsafeValue(i));
}

class IdTypeSpecificValueTest : public ::testing::TestWithParam<int> {
 protected:
  FooId test_id() { return FooId::FromUnsafeValue(GetParam()); }

  FooId other_id() {
    if (GetParam() != std::numeric_limits<int>::max())
      return FooId::FromUnsafeValue(GetParam() + 1);
    else
      return FooId::FromUnsafeValue(std::numeric_limits<int>::min());
  }
};

TEST_P(IdTypeSpecificValueTest, UnsafeValueRoundtrips) {
  int original_value = GetParam();
  FooId id = FooId::FromUnsafeValue(original_value);
  int final_value = id.GetUnsafeValue();
  EXPECT_EQ(original_value, final_value);
}

INSTANTIATE_TEST_SUITE_P(All,
                         IdTypeSpecificValueTest,
                         ::testing::Values(std::numeric_limits<int>::min(),
                                           -1,
                                           0,
                                           1,
                                           123,
                                           std::numeric_limits<int>::max()));

}  // namespace util

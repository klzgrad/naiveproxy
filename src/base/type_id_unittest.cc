// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/type_id.h"

#include <memory>

#include "base/test/type_id_test_support_a.h"
#include "base/test/type_id_test_support_b.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace {

struct T {};

struct U {};

}  // namespace

TEST(TypeId, Basic) {
  EXPECT_EQ(TypeId::From<int>(), TypeId::From<int>());
  EXPECT_NE(TypeId::From<int>(), TypeId::From<void>());
  EXPECT_NE(TypeId::From<int>(), TypeId::From<float>());
  EXPECT_NE(TypeId::From<int>(), TypeId::From<std::unique_ptr<T>>());
  EXPECT_NE(TypeId::From<int>(), TypeId::From<std::unique_ptr<U>>());

  EXPECT_NE(TypeId::From<void>(), TypeId::From<int>());
  EXPECT_EQ(TypeId::From<void>(), TypeId::From<void>());
  EXPECT_NE(TypeId::From<void>(), TypeId::From<float>());
  EXPECT_NE(TypeId::From<void>(), TypeId::From<std::unique_ptr<T>>());
  EXPECT_NE(TypeId::From<void>(), TypeId::From<std::unique_ptr<U>>());

  EXPECT_NE(TypeId::From<float>(), TypeId::From<int>());
  EXPECT_NE(TypeId::From<float>(), TypeId::From<void>());
  EXPECT_EQ(TypeId::From<float>(), TypeId::From<float>());
  EXPECT_NE(TypeId::From<float>(), TypeId::From<std::unique_ptr<T>>());
  EXPECT_NE(TypeId::From<float>(), TypeId::From<std::unique_ptr<U>>());

  EXPECT_NE(TypeId::From<std::unique_ptr<T>>(), TypeId::From<int>());
  EXPECT_NE(TypeId::From<std::unique_ptr<T>>(), TypeId::From<void>());
  EXPECT_NE(TypeId::From<std::unique_ptr<T>>(), TypeId::From<float>());
  EXPECT_EQ(TypeId::From<std::unique_ptr<T>>(),
            TypeId::From<std::unique_ptr<T>>());
  EXPECT_NE(TypeId::From<std::unique_ptr<T>>(),
            TypeId::From<std::unique_ptr<U>>());

  EXPECT_NE(TypeId::From<std::unique_ptr<U>>(), TypeId::From<int>());
  EXPECT_NE(TypeId::From<std::unique_ptr<U>>(), TypeId::From<void>());
  EXPECT_NE(TypeId::From<std::unique_ptr<U>>(), TypeId::From<float>());
  EXPECT_NE(TypeId::From<std::unique_ptr<U>>(),
            TypeId::From<std::unique_ptr<T>>());
  EXPECT_EQ(TypeId::From<std::unique_ptr<U>>(),
            TypeId::From<std::unique_ptr<U>>());
}

TEST(TypeId, TypesInAnonymousNameSpacesDontCollide) {
  EXPECT_NE(TypeIdTestSupportA::GetTypeIdForTypeInAnonymousNameSpace(),
            TypeIdTestSupportB::GetTypeIdForTypeInAnonymousNameSpace());
}

TEST(TypeId, TemplateTypesfromDifferentSo) {
  EXPECT_EQ(TypeIdTestSupportA::GetTypeIdForUniquePtrTestType(),
            TypeId::From<std::unique_ptr<TestType>>());
}

// See http://crbug.com/914734
#if defined(ADDRESS_SANITIZER)
TEST(TypeId, DISABLED_IdenticalTypesFromDifferentCompilationUnitsMatch) {
#else
TEST(TypeId, IdenticalTypesFromDifferentCompilationUnitsMatch) {
#endif
  EXPECT_EQ(TypeIdTestSupportA::GetTypeIdForUniquePtrInt(),
            TypeIdTestSupportB::GetTypeIdForUniquePtrInt());
}

// TODO(crbug.com/928806): Failing consistently on Android and GCC
#if defined(OS_ANDROID) || (defined(COMPILER_GCC) && !defined(__clang__))
TEST(TypeId, DISABLED_IdenticalTypesFromComponentAndStaticLibrary) {
#else
TEST(TypeId, IdenticalTypesFromComponentAndStaticLibrary) {
#endif
  // Code generated for the test itself is statically linked. Make sure it works
  // with components
  TypeId static_linked_type = TypeId::From<std::unique_ptr<int>>();
  EXPECT_EQ(static_linked_type, TypeIdTestSupportA::GetTypeIdForUniquePtrInt());
}

}  // namespace base

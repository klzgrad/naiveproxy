// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/type_id.h"

#include "base/test/type_id_test_support_a.h"
#include "base/test/type_id_test_support_b.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {
namespace experimental {
namespace {

struct T {};

struct U {};

}  // namespace

TEST(TypeId, Basic) {
  static_assert(TypeId::Create<int>() == TypeId::Create<int>(), "");
  static_assert(TypeId::Create<int>() != TypeId::Create<void>(), "");
  static_assert(TypeId::Create<int>() != TypeId::Create<float>(), "");
  static_assert(TypeId::Create<int>() != TypeId::Create<std::unique_ptr<T>>(),
                "");
  static_assert(TypeId::Create<int>() != TypeId::Create<std::unique_ptr<U>>(),
                "");

  static_assert(TypeId::Create<void>() != TypeId::Create<int>(), "");
  static_assert(TypeId::Create<void>() == TypeId::Create<void>(), "");
  static_assert(TypeId::Create<void>() != TypeId::Create<float>(), "");
  static_assert(TypeId::Create<void>() != TypeId::Create<std::unique_ptr<T>>(),
                "");
  static_assert(TypeId::Create<void>() != TypeId::Create<std::unique_ptr<U>>(),
                "");

  static_assert(TypeId::Create<float>() != TypeId::Create<int>(), "");
  static_assert(TypeId::Create<float>() != TypeId::Create<void>(), "");
  static_assert(TypeId::Create<float>() == TypeId::Create<float>(), "");
  static_assert(TypeId::Create<float>() != TypeId::Create<std::unique_ptr<T>>(),
                "");
  static_assert(TypeId::Create<float>() != TypeId::Create<std::unique_ptr<U>>(),
                "");

  static_assert(TypeId::Create<std::unique_ptr<T>>() != TypeId::Create<int>(),
                "");
  static_assert(TypeId::Create<std::unique_ptr<T>>() != TypeId::Create<void>(),
                "");
  static_assert(TypeId::Create<std::unique_ptr<T>>() != TypeId::Create<float>(),
                "");
  static_assert(TypeId::Create<std::unique_ptr<T>>() ==
                    TypeId::Create<std::unique_ptr<T>>(),
                "");
  static_assert(TypeId::Create<std::unique_ptr<T>>() !=
                    TypeId::Create<std::unique_ptr<U>>(),
                "");

  static_assert(TypeId::Create<std::unique_ptr<U>>() != TypeId::Create<int>(),
                "");
  static_assert(TypeId::Create<std::unique_ptr<U>>() != TypeId::Create<void>(),
                "");
  static_assert(TypeId::Create<std::unique_ptr<U>>() != TypeId::Create<float>(),
                "");
  static_assert(TypeId::Create<std::unique_ptr<U>>() !=
                    TypeId::Create<std::unique_ptr<T>>(),
                "");
  static_assert(TypeId::Create<std::unique_ptr<U>>() ==
                    TypeId::Create<std::unique_ptr<U>>(),
                "");
}

TEST(TypeId, TypesInAnonymousNameSpacesDontCollide) {
  EXPECT_NE(TypeIdTestSupportA::GetTypeIdForTypeInAnonymousNameSpace(),
            TypeIdTestSupportB::GetTypeIdForTypeInAnonymousNameSpace());
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

TEST(TypeId, DISABLED_IdenticalTypesFromComponentAndStaticLibrary) {
  // Code generated for the test itself is statically linked. Make sure it works
  // with components
  constexpr TypeId static_linked_type = TypeId::Create<std::unique_ptr<int>>();
  EXPECT_EQ(static_linked_type, TypeIdTestSupportA::GetTypeIdForUniquePtrInt());
}

}  // namespace experimental
}  // namespace base

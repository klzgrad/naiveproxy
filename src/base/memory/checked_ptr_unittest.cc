// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/checked_ptr.h"

#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "testing/gtest/include/gtest/gtest.h"

static_assert(sizeof(CheckedPtr<void>) == sizeof(void*),
              "CheckedPtr shouldn't add memory overhead");
static_assert(sizeof(CheckedPtr<int>) == sizeof(int*),
              "CheckedPtr shouldn't add memory overhead");
static_assert(sizeof(CheckedPtr<std::string>) == sizeof(std::string*),
              "CheckedPtr shouldn't add memory overhead");

// |is_trivially_copyable| assertion means that arrays/vectors of CheckedPtr can
// be copied by memcpy.
static_assert(std::is_trivially_copyable<CheckedPtr<void>>::value,
              "CheckedPtr should be trivially copyable");
static_assert(std::is_trivially_copyable<CheckedPtr<int>>::value,
              "CheckedPtr should be trivially copyable");
static_assert(std::is_trivially_copyable<CheckedPtr<std::string>>::value,
              "CheckedPtr should be trivially copyable");

// |is_trivially_default_constructible| assertion helps retain implicit default
// constructors when CheckedPtr is used as a union field.  Example of an error
// if this assertion didn't hold:
//
//     ../../base/trace_event/trace_arguments.h:249:16: error: call to
//     implicitly-deleted default constructor of 'base::trace_event::TraceValue'
//         TraceValue ret;
//                    ^
//     ../../base/trace_event/trace_arguments.h:211:26: note: default
//     constructor of 'TraceValue' is implicitly deleted because variant field
//     'as_pointer' has a non-trivial default constructor
//       CheckedPtr<const void> as_pointer;
static_assert(std::is_trivially_default_constructible<CheckedPtr<void>>::value,
              "CheckedPtr should be trivially default constructible");
static_assert(std::is_trivially_default_constructible<CheckedPtr<int>>::value,
              "CheckedPtr should be trivially default constructible");
static_assert(
    std::is_trivially_default_constructible<CheckedPtr<std::string>>::value,
    "CheckedPtr should be trivially default constructible");

namespace {

static int g_get_for_dereference_cnt = INT_MIN;
static int g_get_for_extraction_cnt = INT_MIN;
static int g_get_for_comparison_cnt = INT_MIN;
static int g_checked_ptr_swap_cnt = INT_MIN;

static void ClearCounters() {
  g_get_for_dereference_cnt = 0;
  g_get_for_extraction_cnt = 0;
  g_get_for_comparison_cnt = 0;
  g_checked_ptr_swap_cnt = 0;
}

struct CheckedPtrCountingNoOpImpl : base::internal::CheckedPtrNoOpImpl {
  using Super = base::internal::CheckedPtrNoOpImpl;

  static ALWAYS_INLINE void* SafelyUnwrapPtrForDereference(
      uintptr_t wrapped_ptr) {
    ++g_get_for_dereference_cnt;
    return Super::SafelyUnwrapPtrForDereference(wrapped_ptr);
  }

  static ALWAYS_INLINE void* SafelyUnwrapPtrForExtraction(
      uintptr_t wrapped_ptr) {
    ++g_get_for_extraction_cnt;
    return Super::SafelyUnwrapPtrForExtraction(wrapped_ptr);
  }

  static ALWAYS_INLINE void* UnsafelyUnwrapPtrForComparison(
      uintptr_t wrapped_ptr) {
    ++g_get_for_comparison_cnt;
    return Super::UnsafelyUnwrapPtrForComparison(wrapped_ptr);
  }

  static ALWAYS_INLINE void IncrementSwapCountForTest() {
    ++g_checked_ptr_swap_cnt;
  }
};

template <typename T>
using CountingCheckedPtr = CheckedPtr<T, CheckedPtrCountingNoOpImpl>;

struct MyStruct {
  int x;
};

struct Base1 {
  explicit Base1(int b1) : b1(b1) {}
  int b1;
};

struct Base2 {
  explicit Base2(int b2) : b2(b2) {}
  int b2;
};

struct Derived : Base1, Base2 {
  Derived(int b1, int b2, int d) : Base1(b1), Base2(b2), d(d) {}
  int d;
};

TEST(CheckedPtr, NullStarDereference) {
  CheckedPtr<int> ptr = nullptr;
  EXPECT_DEATH_IF_SUPPORTED(if (*ptr == 42) return, "");
}

TEST(CheckedPtr, NullArrowDereference) {
  CheckedPtr<MyStruct> ptr = nullptr;
  EXPECT_DEATH_IF_SUPPORTED(if (ptr->x == 42) return, "");
}

TEST(CheckedPtr, NullExtractNoDereference) {
  CheckedPtr<int> ptr = nullptr;
  int* raw = ptr;
  std::ignore = raw;
}

TEST(CheckedPtr, StarDereference) {
  int foo = 42;
  CheckedPtr<int> ptr = &foo;
  EXPECT_EQ(*ptr, 42);
}

TEST(CheckedPtr, ArrowDereference) {
  MyStruct foo = {42};
  CheckedPtr<MyStruct> ptr = &foo;
  EXPECT_EQ(ptr->x, 42);
}

TEST(CheckedPtr, ConstVoidPtr) {
  int32_t foo[] = {1234567890};
  CheckedPtr<const void> ptr = foo;
  EXPECT_EQ(*static_cast<const int32_t*>(ptr), 1234567890);
}

TEST(CheckedPtr, VoidPtr) {
  int32_t foo[] = {1234567890};
  CheckedPtr<void> ptr = foo;
  EXPECT_EQ(*static_cast<int32_t*>(ptr), 1234567890);
}

TEST(CheckedPtr, OperatorEQ) {
  int foo;
  CheckedPtr<int> ptr1 = nullptr;
  EXPECT_TRUE(ptr1 == ptr1);

  CheckedPtr<int> ptr2 = nullptr;
  EXPECT_TRUE(ptr1 == ptr2);

  CheckedPtr<int> ptr3 = &foo;
  EXPECT_TRUE(&foo == ptr3);
  EXPECT_TRUE(ptr3 == &foo);
  EXPECT_FALSE(ptr1 == ptr3);

  ptr1 = &foo;
  EXPECT_TRUE(ptr1 == ptr3);
  EXPECT_TRUE(ptr3 == ptr1);
}

TEST(CheckedPtr, OperatorNE) {
  int foo;
  CheckedPtr<int> ptr1 = nullptr;
  EXPECT_FALSE(ptr1 != ptr1);

  CheckedPtr<int> ptr2 = nullptr;
  EXPECT_FALSE(ptr1 != ptr2);

  CheckedPtr<int> ptr3 = &foo;
  EXPECT_FALSE(&foo != ptr3);
  EXPECT_FALSE(ptr3 != &foo);
  EXPECT_TRUE(ptr1 != ptr3);

  ptr1 = &foo;
  EXPECT_FALSE(ptr1 != ptr3);
  EXPECT_FALSE(ptr3 != ptr1);
}

TEST(CheckedPtr, OperatorEQCast) {
  ClearCounters();
  int foo = 42;
  const int* raw_int_ptr = &foo;
  void* raw_void_ptr = &foo;
  CountingCheckedPtr<int> checked_int_ptr = &foo;
  CountingCheckedPtr<const void> checked_void_ptr = &foo;
  EXPECT_TRUE(checked_int_ptr == checked_int_ptr);
  EXPECT_TRUE(checked_int_ptr == raw_int_ptr);
  EXPECT_TRUE(raw_int_ptr == checked_int_ptr);
  EXPECT_TRUE(checked_void_ptr == checked_void_ptr);
  EXPECT_TRUE(checked_void_ptr == raw_void_ptr);
  EXPECT_TRUE(raw_void_ptr == checked_void_ptr);
  EXPECT_TRUE(checked_int_ptr == checked_void_ptr);
  EXPECT_TRUE(checked_int_ptr == raw_void_ptr);
  EXPECT_TRUE(raw_int_ptr == checked_void_ptr);
  EXPECT_TRUE(checked_void_ptr == checked_int_ptr);
  EXPECT_TRUE(checked_void_ptr == raw_int_ptr);
  EXPECT_TRUE(raw_void_ptr == checked_int_ptr);
  // Make sure that all cases are handled by operator== (faster) and none by the
  // cast operator (slower).
  EXPECT_EQ(g_get_for_comparison_cnt, 16);
  EXPECT_EQ(g_get_for_extraction_cnt, 0);
  EXPECT_EQ(g_get_for_dereference_cnt, 0);

  ClearCounters();
  Derived derived_val(42, 84, 1024);
  Derived* raw_derived_ptr = &derived_val;
  const Base1* raw_base1_ptr = &derived_val;
  Base2* raw_base2_ptr = &derived_val;
  CountingCheckedPtr<const Derived> checked_derived_ptr = &derived_val;
  CountingCheckedPtr<Base1> checked_base1_ptr = &derived_val;
  CountingCheckedPtr<const Base2> checked_base2_ptr = &derived_val;
  EXPECT_TRUE(checked_derived_ptr == checked_derived_ptr);
  EXPECT_TRUE(checked_derived_ptr == raw_derived_ptr);
  EXPECT_TRUE(raw_derived_ptr == checked_derived_ptr);
  EXPECT_TRUE(checked_derived_ptr == checked_base1_ptr);
  EXPECT_TRUE(checked_derived_ptr == raw_base1_ptr);
  EXPECT_TRUE(raw_derived_ptr == checked_base1_ptr);
  EXPECT_TRUE(checked_base1_ptr == checked_derived_ptr);
  EXPECT_TRUE(checked_base1_ptr == raw_derived_ptr);
  EXPECT_TRUE(raw_base1_ptr == checked_derived_ptr);
  // |base2_ptr| points to the second base class of |derived|, so will be
  // located at an offset. While the stored raw uinptr_t values shouldn't match,
  // ensure that the internal pointer manipulation correctly offsets when
  // casting up and down the class hierarchy.
  EXPECT_NE(reinterpret_cast<uintptr_t>(checked_base2_ptr.get()),
            reinterpret_cast<uintptr_t>(checked_derived_ptr.get()));
  EXPECT_NE(reinterpret_cast<uintptr_t>(raw_base2_ptr),
            reinterpret_cast<uintptr_t>(checked_derived_ptr.get()));
  EXPECT_NE(reinterpret_cast<uintptr_t>(checked_base2_ptr.get()),
            reinterpret_cast<uintptr_t>(raw_derived_ptr));
  EXPECT_TRUE(checked_derived_ptr == checked_base2_ptr);
  EXPECT_TRUE(checked_derived_ptr == raw_base2_ptr);
  EXPECT_TRUE(raw_derived_ptr == checked_base2_ptr);
  EXPECT_TRUE(checked_base2_ptr == checked_derived_ptr);
  EXPECT_TRUE(checked_base2_ptr == raw_derived_ptr);
  EXPECT_TRUE(raw_base2_ptr == checked_derived_ptr);
  // Make sure that all cases are handled by operator== (faster) and none by the
  // cast operator (slower).
  // The 4 extractions come from .get() checks, that compare raw addresses.
  EXPECT_EQ(g_get_for_comparison_cnt, 20);
  EXPECT_EQ(g_get_for_extraction_cnt, 4);
  EXPECT_EQ(g_get_for_dereference_cnt, 0);
}

TEST(CheckedPtr, OperatorNECast) {
  ClearCounters();
  int foo = 42;
  int* raw_int_ptr = &foo;
  const void* raw_void_ptr = &foo;
  CountingCheckedPtr<const int> checked_int_ptr = &foo;
  CountingCheckedPtr<void> checked_void_ptr = &foo;
  EXPECT_FALSE(checked_int_ptr != checked_int_ptr);
  EXPECT_FALSE(checked_int_ptr != raw_int_ptr);
  EXPECT_FALSE(raw_int_ptr != checked_int_ptr);
  EXPECT_FALSE(checked_void_ptr != checked_void_ptr);
  EXPECT_FALSE(checked_void_ptr != raw_void_ptr);
  EXPECT_FALSE(raw_void_ptr != checked_void_ptr);
  EXPECT_FALSE(checked_int_ptr != checked_void_ptr);
  EXPECT_FALSE(checked_int_ptr != raw_void_ptr);
  EXPECT_FALSE(raw_int_ptr != checked_void_ptr);
  EXPECT_FALSE(checked_void_ptr != checked_int_ptr);
  EXPECT_FALSE(checked_void_ptr != raw_int_ptr);
  EXPECT_FALSE(raw_void_ptr != checked_int_ptr);
  // Make sure that all cases are handled by operator== (faster) and none by the
  // cast operator (slower).
  EXPECT_EQ(g_get_for_comparison_cnt, 16);
  EXPECT_EQ(g_get_for_extraction_cnt, 0);
  EXPECT_EQ(g_get_for_dereference_cnt, 0);

  ClearCounters();
  Derived derived_val(42, 84, 1024);
  const Derived* raw_derived_ptr = &derived_val;
  Base1* raw_base1_ptr = &derived_val;
  const Base2* raw_base2_ptr = &derived_val;
  CountingCheckedPtr<Derived> checked_derived_ptr = &derived_val;
  CountingCheckedPtr<const Base1> checked_base1_ptr = &derived_val;
  CountingCheckedPtr<Base2> checked_base2_ptr = &derived_val;
  EXPECT_FALSE(checked_derived_ptr != checked_derived_ptr);
  EXPECT_FALSE(checked_derived_ptr != raw_derived_ptr);
  EXPECT_FALSE(raw_derived_ptr != checked_derived_ptr);
  EXPECT_FALSE(checked_derived_ptr != checked_base1_ptr);
  EXPECT_FALSE(checked_derived_ptr != raw_base1_ptr);
  EXPECT_FALSE(raw_derived_ptr != checked_base1_ptr);
  EXPECT_FALSE(checked_base1_ptr != checked_derived_ptr);
  EXPECT_FALSE(checked_base1_ptr != raw_derived_ptr);
  EXPECT_FALSE(raw_base1_ptr != checked_derived_ptr);
  // |base2_ptr| points to the second base class of |derived|, so will be
  // located at an offset. While the stored raw uinptr_t values shouldn't match,
  // ensure that the internal pointer manipulation correctly offsets when
  // casting up and down the class hierarchy.
  EXPECT_NE(reinterpret_cast<uintptr_t>(checked_base2_ptr.get()),
            reinterpret_cast<uintptr_t>(checked_derived_ptr.get()));
  EXPECT_NE(reinterpret_cast<uintptr_t>(raw_base2_ptr),
            reinterpret_cast<uintptr_t>(checked_derived_ptr.get()));
  EXPECT_NE(reinterpret_cast<uintptr_t>(checked_base2_ptr.get()),
            reinterpret_cast<uintptr_t>(raw_derived_ptr));
  EXPECT_FALSE(checked_derived_ptr != checked_base2_ptr);
  EXPECT_FALSE(checked_derived_ptr != raw_base2_ptr);
  EXPECT_FALSE(raw_derived_ptr != checked_base2_ptr);
  EXPECT_FALSE(checked_base2_ptr != checked_derived_ptr);
  EXPECT_FALSE(checked_base2_ptr != raw_derived_ptr);
  EXPECT_FALSE(raw_base2_ptr != checked_derived_ptr);
  // Make sure that all cases are handled by operator== (faster) and none by the
  // cast operator (slower).
  // The 4 extractions come from .get() checks, that compare raw addresses.
  EXPECT_EQ(g_get_for_comparison_cnt, 20);
  EXPECT_EQ(g_get_for_extraction_cnt, 4);
  EXPECT_EQ(g_get_for_dereference_cnt, 0);
}

TEST(CheckedPtr, Cast) {
  Derived derived_val(42, 84, 1024);
  CheckedPtr<Derived> checked_derived_ptr = &derived_val;
  Base1* raw_base1_ptr = checked_derived_ptr;
  EXPECT_EQ(raw_base1_ptr->b1, 42);
  Base2* raw_base2_ptr = checked_derived_ptr;
  EXPECT_EQ(raw_base2_ptr->b2, 84);

  Derived* raw_derived_ptr = static_cast<Derived*>(raw_base1_ptr);
  EXPECT_EQ(raw_derived_ptr->b1, 42);
  EXPECT_EQ(raw_derived_ptr->b2, 84);
  EXPECT_EQ(raw_derived_ptr->d, 1024);
  raw_derived_ptr = static_cast<Derived*>(raw_base2_ptr);
  EXPECT_EQ(raw_derived_ptr->b1, 42);
  EXPECT_EQ(raw_derived_ptr->b2, 84);
  EXPECT_EQ(raw_derived_ptr->d, 1024);

  CheckedPtr<Base1> checked_base1_ptr = raw_derived_ptr;
  EXPECT_EQ(checked_base1_ptr->b1, 42);
  CheckedPtr<Base2> checked_base2_ptr = raw_derived_ptr;
  EXPECT_EQ(checked_base2_ptr->b2, 84);

  CheckedPtr<Derived> checked_derived_ptr2 =
      static_cast<Derived*>(checked_base1_ptr);
  EXPECT_EQ(checked_derived_ptr2->b1, 42);
  EXPECT_EQ(checked_derived_ptr2->b2, 84);
  EXPECT_EQ(checked_derived_ptr2->d, 1024);
  checked_derived_ptr2 = static_cast<Derived*>(checked_base2_ptr);
  EXPECT_EQ(checked_derived_ptr2->b1, 42);
  EXPECT_EQ(checked_derived_ptr2->b2, 84);
  EXPECT_EQ(checked_derived_ptr2->d, 1024);

  const Derived* raw_const_derived_ptr = checked_derived_ptr2;
  EXPECT_EQ(raw_const_derived_ptr->b1, 42);
  EXPECT_EQ(raw_const_derived_ptr->b2, 84);
  EXPECT_EQ(raw_const_derived_ptr->d, 1024);

  CheckedPtr<const Derived> checked_const_derived_ptr = raw_const_derived_ptr;
  EXPECT_EQ(checked_const_derived_ptr->b1, 42);
  EXPECT_EQ(checked_const_derived_ptr->b2, 84);
  EXPECT_EQ(checked_const_derived_ptr->d, 1024);

  void* raw_void_ptr = checked_derived_ptr;
  CheckedPtr<void> checked_void_ptr = raw_derived_ptr;
  CheckedPtr<Derived> checked_derived_ptr3 =
      static_cast<Derived*>(raw_void_ptr);
  CheckedPtr<Derived> checked_derived_ptr4 =
      static_cast<Derived*>(checked_void_ptr);
  EXPECT_EQ(checked_derived_ptr3->b1, 42);
  EXPECT_EQ(checked_derived_ptr3->b2, 84);
  EXPECT_EQ(checked_derived_ptr3->d, 1024);
  EXPECT_EQ(checked_derived_ptr4->b1, 42);
  EXPECT_EQ(checked_derived_ptr4->b2, 84);
  EXPECT_EQ(checked_derived_ptr4->d, 1024);
}

TEST(CheckedPtr, CustomSwap) {
  ClearCounters();
  int foo1, foo2;
  CountingCheckedPtr<int> ptr1(&foo1);
  CountingCheckedPtr<int> ptr2(&foo2);
  // Recommended use pattern.
  using std::swap;
  swap(ptr1, ptr2);
  EXPECT_EQ(ptr1.get(), &foo2);
  EXPECT_EQ(ptr2.get(), &foo1);
  EXPECT_EQ(g_checked_ptr_swap_cnt, 1);
}

TEST(CheckedPtr, StdSwap) {
  ClearCounters();
  int foo1, foo2;
  CountingCheckedPtr<int> ptr1(&foo1);
  CountingCheckedPtr<int> ptr2(&foo2);
  std::swap(ptr1, ptr2);
  EXPECT_EQ(ptr1.get(), &foo2);
  EXPECT_EQ(ptr2.get(), &foo1);
  EXPECT_EQ(g_checked_ptr_swap_cnt, 0);
}

TEST(CheckedPtr, AdvanceIntArray) {
  // operator++
  int foo[] = {42, 43, 44, 45};
  CheckedPtr<int> ptr = foo;
  for (int i = 0; i < 4; ++i, ++ptr) {
    ASSERT_EQ(*ptr, 42 + i);
  }
  ptr = &foo[1];
  for (int i = 1; i < 4; ++i, ++ptr) {
    ASSERT_EQ(*ptr, 42 + i);
  }

  // operator--
  ptr = &foo[3];
  for (int i = 3; i >= 0; --i, --ptr) {
    ASSERT_EQ(*ptr, 42 + i);
  }

  // operator+=
  ptr = foo;
  for (int i = 0; i < 4; i += 2, ptr += 2) {
    ASSERT_EQ(*ptr, 42 + i);
  }

  // operator-=
  ptr = &foo[3];
  for (int i = 3; i >= 0; i -= 2, ptr -= 2) {
    ASSERT_EQ(*ptr, 42 + i);
  }
}

TEST(CheckedPtr, AdvanceString) {
  const char kChars[] = "Hello";
  std::string str = kChars;
  CheckedPtr<const char> ptr = str.c_str();
  for (size_t i = 0; i < str.size(); ++i, ++ptr) {
    ASSERT_EQ(*ptr, kChars[i]);
  }
}

}  // namespace

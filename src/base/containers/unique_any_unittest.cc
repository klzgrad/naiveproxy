// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/containers/unique_any.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/test/copy_only_int.h"
#include "base/test/gtest_util.h"
#include "base/test/move_only_int.h"
#include "testing/gtest/include/gtest/gtest.h"

// The first section of tests are imported from absl::any with modifications for
// move only type support. Note tests dealing with exception have been omitted
// because Chromium does not use exceptions.
// See third_party/abseil-cpp/absl/types/any_test.cc
namespace base {

namespace {
template <typename T>
const T& AsConst(const T& t) {
  return t;
}

struct MoveOnlyWithListConstructor {
  MoveOnlyWithListConstructor() = default;
  explicit MoveOnlyWithListConstructor(std::initializer_list<int> /*ilist*/,
                                       int value)
      : value(value) {}
  MoveOnlyWithListConstructor(MoveOnlyWithListConstructor&&) = default;
  MoveOnlyWithListConstructor& operator=(MoveOnlyWithListConstructor&&) =
      default;

  int value = 0;
};

struct IntMoveOnlyCopyOnlyInt {
  IntMoveOnlyCopyOnlyInt(int value,
                         MoveOnlyInt /*move_only*/,
                         CopyOnlyInt /*copy_only*/)
      : value(value) {}

  int value;
};

struct ListMoveOnlyCopyOnlyInt {
  ListMoveOnlyCopyOnlyInt(std::initializer_list<int> ilist,
                          MoveOnlyInt /*move_only*/,
                          CopyOnlyInt /*copy_only*/)
      : values(ilist) {}

  std::vector<int> values;
};

using FunctionType = void();
void FunctionToEmplace() {}

using ArrayType = int[2];
using DecayedArray = std::decay_t<ArrayType>;

struct Value {};

}  // namespace

TEST(UniqueAnyTest, Noexcept) {
  static_assert(std::is_nothrow_default_constructible<unique_any>(), "");
  static_assert(std::is_nothrow_move_constructible<unique_any>(), "");
  static_assert(std::is_nothrow_move_assignable<unique_any>(), "");
  static_assert(noexcept(std::declval<unique_any&>().has_value()), "");
  static_assert(noexcept(std::declval<unique_any&>().type()), "");
  static_assert(noexcept(unique_any_cast<int>(std::declval<unique_any*>())),
                "");
  static_assert(
      noexcept(std::declval<unique_any&>().swap(std::declval<unique_any&>())),
      "");

  using std::swap;
  static_assert(
      noexcept(swap(std::declval<unique_any&>(), std::declval<unique_any&>())),
      "");
}

TEST(UniqueAnyTest, HasValue) {
  unique_any o;
  EXPECT_FALSE(o.has_value());
  o.emplace<int>();
  EXPECT_TRUE(o.has_value());
  o.reset();
  EXPECT_FALSE(o.has_value());
}

TEST(UniqueAnyTest, TypeId) {
  unique_any a;
  EXPECT_EQ(a.type(), TypeId::From<void>());

  a = 123;
  EXPECT_EQ(a.type(), TypeId::From<int>());

  a = 123.0f;
  EXPECT_EQ(a.type(), TypeId::From<float>());

  a = true;
  EXPECT_EQ(a.type(), TypeId::From<bool>());

  a = std::string("test");
  EXPECT_EQ(a.type(), TypeId::From<std::string>());

  a.reset();
  EXPECT_EQ(a.type(), TypeId::From<void>());
}

TEST(UniqueAnyTest, EmptyPointerCast) {
  // pointer-to-unqualified overload
  {
    unique_any o;
    EXPECT_EQ(nullptr, unique_any_cast<int>(&o));
    o.emplace<int>();
    EXPECT_NE(nullptr, unique_any_cast<int>(&o));
    o.reset();
    EXPECT_EQ(nullptr, unique_any_cast<int>(&o));
  }

  // pointer-to-const overload
  {
    unique_any o;
    EXPECT_EQ(nullptr, unique_any_cast<int>(&AsConst(o)));
    o.emplace<int>();
    EXPECT_NE(nullptr, unique_any_cast<int>(&AsConst(o)));
    o.reset();
    EXPECT_EQ(nullptr, unique_any_cast<int>(&AsConst(o)));
  }
}

TEST(UniqueAnyTest, InPlaceConstruction) {
  const CopyOnlyInt copy_only{};
  unique_any o(in_place_type_t<IntMoveOnlyCopyOnlyInt>(), 5, MoveOnlyInt(),
               copy_only);
  IntMoveOnlyCopyOnlyInt& v = unique_any_cast<IntMoveOnlyCopyOnlyInt&>(o);
  EXPECT_EQ(5, v.value);
}

TEST(UniqueAnyTest, InPlaceConstructionWithCV) {
  const CopyOnlyInt copy_only{};
  unique_any o(in_place_type_t<const volatile IntMoveOnlyCopyOnlyInt>(), 5,
               MoveOnlyInt(), copy_only);
  IntMoveOnlyCopyOnlyInt& v = unique_any_cast<IntMoveOnlyCopyOnlyInt&>(o);
  EXPECT_EQ(5, v.value);
}

TEST(UniqueAnyTest, InPlaceConstructionWithFunction) {
  unique_any o(in_place_type_t<FunctionType>(), FunctionToEmplace);
  FunctionType*& construction_result = unique_any_cast<FunctionType*&>(o);
  EXPECT_EQ(&FunctionToEmplace, construction_result);
}

TEST(UniqueAnyTest, InPlaceConstructionWithArray) {
  ArrayType ar = {5, 42};
  unique_any o(in_place_type_t<ArrayType>(), ar);
  DecayedArray& construction_result = unique_any_cast<DecayedArray&>(o);
  EXPECT_EQ(&ar[0], construction_result);
}

TEST(UniqueAnyTest, InPlaceConstructionIlist) {
  const CopyOnlyInt copy_only{};
  unique_any o(in_place_type_t<ListMoveOnlyCopyOnlyInt>(), {1, 2, 3, 4},
               MoveOnlyInt(), copy_only);
  ListMoveOnlyCopyOnlyInt& v = unique_any_cast<ListMoveOnlyCopyOnlyInt&>(o);
  std::vector<int> expected_values = {1, 2, 3, 4};
  EXPECT_EQ(expected_values, v.values);
}

TEST(UniqueAnyTest, InPlaceConstructionIlistWithCV) {
  const CopyOnlyInt copy_only{};
  unique_any o(in_place_type_t<const volatile ListMoveOnlyCopyOnlyInt>(),
               {1, 2, 3, 4}, MoveOnlyInt(), copy_only);
  ListMoveOnlyCopyOnlyInt& v = unique_any_cast<ListMoveOnlyCopyOnlyInt&>(o);
  std::vector<int> expected_values = {1, 2, 3, 4};
  EXPECT_EQ(expected_values, v.values);
}

TEST(UniqueAnyTest, InPlaceNoArgs) {
  unique_any o(in_place_type_t<int>{});
  EXPECT_EQ(0, unique_any_cast<int&>(o));
}

template <typename Enabler, typename T, typename... Args>
struct CanEmplaceAnyImpl : std::false_type {};

template <typename T, typename... Args>
struct CanEmplaceAnyImpl<void_t<decltype(std::declval<unique_any&>().emplace<T>(
                             std::declval<Args>()...))>,
                         T,
                         Args...> : std::true_type {};

template <typename T, typename... Args>
using CanEmplaceAny = CanEmplaceAnyImpl<void, T, Args...>;

TEST(UniqueAnyTest, Emplace) {
  const CopyOnlyInt copy_only{};
  unique_any o;
  EXPECT_TRUE((std::is_same<decltype(o.emplace<IntMoveOnlyCopyOnlyInt>(
                                5, MoveOnlyInt(), copy_only)),
                            IntMoveOnlyCopyOnlyInt&>::value));
  IntMoveOnlyCopyOnlyInt& emplace_result =
      o.emplace<IntMoveOnlyCopyOnlyInt>(5, MoveOnlyInt(), copy_only);
  EXPECT_EQ(5, emplace_result.value);
  IntMoveOnlyCopyOnlyInt& v = unique_any_cast<IntMoveOnlyCopyOnlyInt&>(o);
  EXPECT_EQ(5, v.value);
  EXPECT_EQ(&emplace_result, &v);

  static_assert(!CanEmplaceAny<int, int, int>::value, "Too many parameters");
  static_assert(CanEmplaceAny<MoveOnlyInt, MoveOnlyInt>::value,
                "Can't emplace move only type");
}

TEST(UniqueAnyTest, EmplaceWithCV) {
  const CopyOnlyInt copy_only{};
  unique_any o;
  EXPECT_TRUE(
      (std::is_same<decltype(o.emplace<const volatile IntMoveOnlyCopyOnlyInt>(
                        5, MoveOnlyInt(), copy_only)),
                    IntMoveOnlyCopyOnlyInt&>::value));
  IntMoveOnlyCopyOnlyInt& emplace_result =
      o.emplace<const volatile IntMoveOnlyCopyOnlyInt>(5, MoveOnlyInt(),
                                                       copy_only);
  EXPECT_EQ(5, emplace_result.value);
  IntMoveOnlyCopyOnlyInt& v = unique_any_cast<IntMoveOnlyCopyOnlyInt&>(o);
  EXPECT_EQ(5, v.value);
  EXPECT_EQ(&emplace_result, &v);
}

TEST(UniqueAnyTest, EmplaceWithFunction) {
  unique_any o;
  EXPECT_TRUE(
      (std::is_same<decltype(o.emplace<FunctionType>(FunctionToEmplace)),
                    FunctionType*&>::value));
  FunctionType*& emplace_result = o.emplace<FunctionType>(FunctionToEmplace);
  EXPECT_EQ(&FunctionToEmplace, emplace_result);
}

TEST(UniqueAnyTest, EmplaceWithArray) {
  unique_any o;
  ArrayType ar = {5, 42};
  EXPECT_TRUE(
      (std::is_same<decltype(o.emplace<ArrayType>(ar)), DecayedArray&>::value));
  DecayedArray& emplace_result = o.emplace<ArrayType>(ar);
  EXPECT_EQ(&ar[0], emplace_result);
}

TEST(UniqueAnyTest, EmplaceIlist) {
  const CopyOnlyInt copy_only{};
  unique_any o;
  EXPECT_TRUE((std::is_same<decltype(o.emplace<ListMoveOnlyCopyOnlyInt>(
                                {1, 2, 3, 4}, MoveOnlyInt(), copy_only)),
                            ListMoveOnlyCopyOnlyInt&>::value));
  ListMoveOnlyCopyOnlyInt& emplace_result = o.emplace<ListMoveOnlyCopyOnlyInt>(
      {1, 2, 3, 4}, MoveOnlyInt(), copy_only);
  ListMoveOnlyCopyOnlyInt& v = unique_any_cast<ListMoveOnlyCopyOnlyInt&>(o);
  EXPECT_EQ(&v, &emplace_result);
  std::vector<int> expected_values = {1, 2, 3, 4};
  EXPECT_EQ(expected_values, v.values);

  static_assert(!CanEmplaceAny<int, std::initializer_list<int>>::value,
                "Too many parameters");
  static_assert(CanEmplaceAny<MoveOnlyWithListConstructor,
                              std::initializer_list<int>, int>::value,
                "Can emplace move only type");
}

TEST(UniqueAnyTest, EmplaceIlistWithCV) {
  const CopyOnlyInt copy_only{};
  unique_any o;
  EXPECT_TRUE(
      (std::is_same<decltype(o.emplace<const volatile ListMoveOnlyCopyOnlyInt>(
                        {1, 2, 3, 4}, MoveOnlyInt(), copy_only)),
                    ListMoveOnlyCopyOnlyInt&>::value));
  ListMoveOnlyCopyOnlyInt& emplace_result =
      o.emplace<const volatile ListMoveOnlyCopyOnlyInt>(
          {1, 2, 3, 4}, MoveOnlyInt(), copy_only);
  ListMoveOnlyCopyOnlyInt& v = unique_any_cast<ListMoveOnlyCopyOnlyInt&>(o);
  EXPECT_EQ(&v, &emplace_result);
  std::vector<int> expected_values = {1, 2, 3, 4};
  EXPECT_EQ(expected_values, v.values);
}

TEST(UniqueAnyTest, EmplaceNoArgs) {
  unique_any o;
  o.emplace<int>();
  EXPECT_EQ(0, unique_any_cast<int>(o));
}

TEST(UniqueAnyTest, ConversionConstruction) {
  {
    unique_any o = 5;
    EXPECT_EQ(5, unique_any_cast<int>(o));
  }

  {
    const CopyOnlyInt copy_only(5);
    unique_any o = copy_only;
    EXPECT_EQ(5, unique_any_cast<CopyOnlyInt&>(o).data());
  }

  {
    MoveOnlyInt i{123};
    unique_any o(std::move(i));
    EXPECT_EQ(123, unique_any_cast<MoveOnlyInt&>(o).data());
  }
}

TEST(UniqueAnyTest, ConversionAssignment) {
  {
    unique_any o;
    o = 5;
    EXPECT_EQ(5, unique_any_cast<int>(o));
  }

  {
    const CopyOnlyInt copy_only(5);
    unique_any o;
    o = copy_only;
    EXPECT_EQ(5, unique_any_cast<CopyOnlyInt&>(o).data());
  }

  {
    unique_any o;
    MoveOnlyInt i{123};
    o = std::move(i);
    EXPECT_EQ(123, unique_any_cast<MoveOnlyInt&>(o).data());
  }
}

// Suppress MSVC warnings.
// 4521: multiple copy constructors specified
// We wrote multiple of them to test that the correct overloads are selected.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4521)
#endif

// Weird type for testing, only used to make sure we "properly" perfect-forward
// when being placed into an unique_any (use the l-value constructor if given
// an l-value rather than use the copy constructor).
struct WeirdConstructor42 {
  explicit WeirdConstructor42(int value) : value(value) {}

  // Copy-constructor
  WeirdConstructor42(const WeirdConstructor42& other) : value(other.value) {}

  // L-value "weird" constructor (used when given an l-value)
  WeirdConstructor42(
      WeirdConstructor42& /*other*/)  // NOLINT(runtime/references)
      : value(42) {}

  int value;
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

TEST(UniqueAnyTest, WeirdConversionConstruction) {
  {
    const WeirdConstructor42 source(5);
    unique_any o = source;  // Actual copy
    EXPECT_EQ(5, unique_any_cast<WeirdConstructor42&>(o).value);
  }

  {
    WeirdConstructor42 source(5);
    unique_any o = source;  // Weird "conversion"
    EXPECT_EQ(42, unique_any_cast<WeirdConstructor42&>(o).value);
  }
}

TEST(UniqueAnyTest, WeirdConversionAssignment) {
  {
    const WeirdConstructor42 source(5);
    unique_any o;
    o = source;  // Actual copy
    EXPECT_EQ(5, unique_any_cast<WeirdConstructor42&>(o).value);
  }

  {
    WeirdConstructor42 source(5);
    unique_any o;
    o = source;  // Weird "conversion"
    EXPECT_EQ(42, unique_any_cast<WeirdConstructor42&>(o).value);
  }
}

TEST(UniqueAnyTest, AnyCastValue) {
  {
    unique_any o;
    o.emplace<int>(5);
    EXPECT_EQ(5, unique_any_cast<int>(o));
    EXPECT_EQ(5, unique_any_cast<int>(AsConst(o)));
    static_assert(
        std::is_same<decltype(unique_any_cast<Value>(o)), Value>::value, "");
  }

  {
    unique_any o;
    o.emplace<int>(5);
    EXPECT_EQ(5, unique_any_cast<const int>(o));
    EXPECT_EQ(5, unique_any_cast<const int>(AsConst(o)));
    static_assert(std::is_same<decltype(unique_any_cast<const Value>(o)),
                               const Value>::value,
                  "");
  }

  {
    unique_any a = std::make_unique<int>(1234);
    std::unique_ptr<int> b =
        unique_any_cast<std::unique_ptr<int>>(std::move(a));
    EXPECT_EQ(1234, *b);
  }
}

TEST(UniqueAnyTest, AnyCastReference) {
  {
    unique_any o;
    o.emplace<int>(5);
    EXPECT_EQ(5, unique_any_cast<int&>(o));
    EXPECT_EQ(5, unique_any_cast<const int&>(AsConst(o)));
    static_assert(
        std::is_same<decltype(unique_any_cast<Value&>(o)), Value&>::value, "");
  }

  {
    unique_any o;
    o.emplace<int>(5);
    EXPECT_EQ(5, unique_any_cast<const int>(o));
    EXPECT_EQ(5, unique_any_cast<const int>(AsConst(o)));
    static_assert(std::is_same<decltype(unique_any_cast<const Value&>(o)),
                               const Value&>::value,
                  "");
  }

  {
    unique_any o;
    o.emplace<int>(5);
    EXPECT_EQ(5, unique_any_cast<int&&>(std::move(o)));
    static_assert(std::is_same<decltype(unique_any_cast<Value&&>(std::move(o))),
                               Value&&>::value,
                  "");
  }

  {
    unique_any o;
    o.emplace<int>(5);
    EXPECT_EQ(5, unique_any_cast<const int>(std::move(o)));
    static_assert(
        std::is_same<decltype(unique_any_cast<const Value&&>(std::move(o))),
                     const Value&&>::value,
        "");
  }
}

TEST(UniqueAnyTest, AnyCastPointer) {
  {
    unique_any o;
    EXPECT_EQ(nullptr, unique_any_cast<char>(&o));
    EXPECT_EQ(nullptr, unique_any_cast<char>(&o));
    o.emplace<char>('a');
    EXPECT_EQ('a', *unique_any_cast<char>(&o));
    static_assert(
        std::is_same<decltype(unique_any_cast<Value>(&o)), Value*>::value, "");
  }

  {
    unique_any o;
    EXPECT_EQ(nullptr, unique_any_cast<const char>(&o));
    o.emplace<int>(5);
    EXPECT_EQ(nullptr, unique_any_cast<const char>(&o));
    o.emplace<char>('a');
    EXPECT_EQ('a', *unique_any_cast<const char>(&o));
    static_assert(std::is_same<decltype(unique_any_cast<const Value>(&o)),
                               const Value*>::value,
                  "");
  }
}

TEST(UniqueAnyTest, MakeAny) {
  const CopyOnlyInt copy_only{};
  auto o = base::make_unique_any<IntMoveOnlyCopyOnlyInt>(5, MoveOnlyInt(),
                                                         copy_only);
  static_assert(std::is_same<decltype(o), unique_any>::value, "");
  EXPECT_EQ(5, unique_any_cast<IntMoveOnlyCopyOnlyInt&>(o).value);
}

TEST(UniqueAnyTest, MakeAnyIList) {
  const CopyOnlyInt copy_only{};
  auto o = base::make_unique_any<ListMoveOnlyCopyOnlyInt>(
      {1, 2, 3}, MoveOnlyInt(), copy_only);
  static_assert(std::is_same<decltype(o), unique_any>::value, "");
  ListMoveOnlyCopyOnlyInt& v = unique_any_cast<ListMoveOnlyCopyOnlyInt&>(o);
  std::vector<int> expected_values = {1, 2, 3};
  EXPECT_EQ(expected_values, v.values);

  base::unique_any a = base::make_unique_any<std::vector<int>>({1, 2, 3, 4});
  EXPECT_EQ(4u, unique_any_cast<std::vector<int>>(a).size());
}

TEST(UniqueAnyTest, Reset) {
  unique_any o;
  o.emplace<int>();

  o.reset();
  EXPECT_FALSE(o.has_value());

  o.emplace<char>();
  EXPECT_TRUE(o.has_value());
}

TEST(UniqueAnyTest, ConversionConstructionCausesOneCopy) {
  CopyOnlyInt::reset_num_copies();
  CopyOnlyInt counter(5);
  unique_any o(counter);
  EXPECT_EQ(5, unique_any_cast<CopyOnlyInt&>(o).data());
  EXPECT_EQ(1, CopyOnlyInt::num_copies());
}

// Start of chromium specific tests:
namespace {

class DestructDetector {
 public:
  explicit DestructDetector(bool* destructor_called)
      : destructor_called_(destructor_called) {}

  ~DestructDetector() { *destructor_called_ = true; }

 private:
  bool* destructor_called_;  // NOT OWNED
};

}  // namespace

TEST(UniqueAnyTest, DestructorCalled) {
  bool destructor_called = false;

  {
    unique_any a;
    a.emplace<DestructDetector>(&destructor_called);
    EXPECT_FALSE(destructor_called);
  }

  EXPECT_TRUE(destructor_called);
}

TEST(UniqueAnyTest, DestructorCalledOnAssignment) {
  bool destructor_called = false;

  unique_any a;
  a.emplace<DestructDetector>(&destructor_called);

  EXPECT_FALSE(destructor_called);
  a = 123;
  EXPECT_TRUE(destructor_called);
}

TEST(UniqueAnyTest, MoveAssignment) {
  unique_any a(std::make_unique<int>(1234));
  unique_any b;

  b = std::move(a);

  // The state of |a| is undefined here. The unique_ptr should have been moved
  // from however.
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(nullptr, unique_any_cast<std::unique_ptr<int>&>(a));
  EXPECT_EQ(1234, *unique_any_cast<std::unique_ptr<int>&>(b));
}

TEST(UniqueAnyTest, MoveConstructor) {
  unique_any a(std::make_unique<int>(1234));
  unique_any b(std::move(a));
  // The state of |a| is undefined here. The unique_ptr should have been moved
  // from however.
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(nullptr, unique_any_cast<std::unique_ptr<int>&>(a));
  EXPECT_EQ(1234, *unique_any_cast<std::unique_ptr<int>&>(b));
}

TEST(UniqueAnyTest, MoveOnlyInt) {
  unique_any a;
  a = MoveOnlyInt(1234);

  EXPECT_EQ(1234, unique_any_cast<MoveOnlyInt&>(a).data());

  unique_any b;
  b = std::move(a);
  EXPECT_EQ(1234, unique_any_cast<MoveOnlyInt&>(b).data());
}

TEST(UniqueAnyTest, SwapEmptySmall) {
  unique_any a;
  unique_any b(123);

  swap(a, b);

  EXPECT_TRUE(a.has_value());
  EXPECT_EQ(123, unique_any_cast<int>(a));
  EXPECT_FALSE(b.has_value());

  std::swap(a, b);

  EXPECT_FALSE(a.has_value());
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ(123, unique_any_cast<int>(b));
}

TEST(UniqueAnyTest, SwapEmptyLarge) {
  unique_any a;
  unique_any b(std::string("hello"));

  swap(a, b);

  EXPECT_TRUE(a.has_value());
  EXPECT_EQ("hello", unique_any_cast<std::string>(a));
  EXPECT_FALSE(b.has_value());

  std::swap(a, b);

  EXPECT_FALSE(a.has_value());
  EXPECT_TRUE(b.has_value());
  EXPECT_EQ("hello", unique_any_cast<std::string>(b));
}

}  // namespace base

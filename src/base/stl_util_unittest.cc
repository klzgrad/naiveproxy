// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/stl_util.h"

#include <array>
#include <deque>
#include <forward_list>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/containers/queue.h"
#include "base/strings/string16.h"
#include "base/strings/utf_string_conversions.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// Used as test case to ensure the various base::STLXxx functions don't require
// more than operators "<" and "==" on values stored in containers.
class ComparableValue {
 public:
  explicit ComparableValue(int value) : value_(value) {}

  bool operator==(const ComparableValue& rhs) const {
    return value_ == rhs.value_;
  }

  bool operator<(const ComparableValue& rhs) const {
    return value_ < rhs.value_;
  }

 private:
  int value_;
};

template <typename Container>
void RunEraseTest() {
  const std::pair<Container, Container> test_data[] = {
      {Container(), Container()}, {{1, 2, 3}, {1, 3}}, {{1, 2, 3, 2}, {1, 3}}};

  for (auto test_case : test_data) {
    base::Erase(test_case.first, 2);
    EXPECT_EQ(test_case.second, test_case.first);
  }
}

// This test is written for containers of std::pair<int, int> to support maps.
template <typename Container>
void RunEraseIfTest() {
  struct {
    Container input;
    Container erase_even;
    Container erase_odd;
  } test_data[] = {
      {Container(), Container(), Container()},
      {{{1, 1}, {2, 2}, {3, 3}}, {{1, 1}, {3, 3}}, {{2, 2}}},
      {{{1, 1}, {2, 2}, {3, 3}, {4, 4}}, {{1, 1}, {3, 3}}, {{2, 2}, {4, 4}}},
  };

  for (auto test_case : test_data) {
    base::EraseIf(test_case.input, [](const std::pair<int, int>& elem) {
      return !(elem.first & 1);
    });
    EXPECT_EQ(test_case.erase_even, test_case.input);
  }

  for (auto test_case : test_data) {
    base::EraseIf(test_case.input, [](const std::pair<int, int>& elem) {
      return elem.first & 1;
    });
    EXPECT_EQ(test_case.erase_odd, test_case.input);
  }
}

struct CustomIntHash {
  size_t operator()(int elem) const { return std::hash<int>()(elem) + 1; }
};

struct HashByFirst {
  size_t operator()(const std::pair<int, int>& elem) const {
    return std::hash<int>()(elem.first);
  }
};

}  // namespace

namespace base {
namespace {

TEST(STLUtilTest, Size) {
  {
    std::vector<int> vector = {1, 2, 3, 4, 5};
    static_assert(
        std::is_same<decltype(base::size(vector)),
                     decltype(vector.size())>::value,
        "base::size(vector) should have the same type as vector.size()");
    EXPECT_EQ(vector.size(), base::size(vector));
  }

  {
    std::string empty_str;
    static_assert(
        std::is_same<decltype(base::size(empty_str)),
                     decltype(empty_str.size())>::value,
        "base::size(empty_str) should have the same type as empty_str.size()");
    EXPECT_EQ(0u, base::size(empty_str));
  }

  {
    std::array<int, 4> array = {{1, 2, 3, 4}};
    static_assert(
        std::is_same<decltype(base::size(array)),
                     decltype(array.size())>::value,
        "base::size(array) should have the same type as array.size()");
    static_assert(base::size(array) == array.size(),
                  "base::size(array) should be equal to array.size()");
  }

  {
    int array[] = {1, 2, 3};
    static_assert(std::is_same<size_t, decltype(base::size(array))>::value,
                  "base::size(array) should be of type size_t");
    static_assert(3u == base::size(array), "base::size(array) should be 3");
  }
}

TEST(STLUtilTest, Empty) {
  {
    std::vector<int> vector;
    static_assert(
        std::is_same<decltype(base::empty(vector)),
                     decltype(vector.empty())>::value,
        "base::empty(vector) should have the same type as vector.empty()");
    EXPECT_EQ(vector.empty(), base::empty(vector));
  }

  {
    std::array<int, 4> array = {{1, 2, 3, 4}};
    static_assert(
        std::is_same<decltype(base::empty(array)),
                     decltype(array.empty())>::value,
        "base::empty(array) should have the same type as array.empty()");
    static_assert(base::empty(array) == array.empty(),
                  "base::empty(array) should be equal to array.empty()");
  }

  {
    int array[] = {1, 2, 3};
    static_assert(std::is_same<bool, decltype(base::empty(array))>::value,
                  "base::empty(array) should be of type bool");
    static_assert(!base::empty(array), "base::empty(array) should be false");
  }

  {
    constexpr std::initializer_list<int> il;
    static_assert(std::is_same<bool, decltype(base::empty(il))>::value,
                  "base::empty(il) should be of type bool");
    static_assert(base::empty(il), "base::empty(il) should be true");
  }
}

TEST(STLUtilTest, Data) {
  {
    std::vector<int> vector = {1, 2, 3, 4, 5};
    static_assert(
        std::is_same<decltype(base::data(vector)),
                     decltype(vector.data())>::value,
        "base::data(vector) should have the same type as vector.data()");
    EXPECT_EQ(vector.data(), base::data(vector));
  }

  {
    const std::string cstr = "const string";
    static_assert(
        std::is_same<decltype(base::data(cstr)), decltype(cstr.data())>::value,
        "base::data(cstr) should have the same type as cstr.data()");

    EXPECT_EQ(cstr.data(), base::data(cstr));
  }

  {
    std::string str = "mutable string";
    static_assert(std::is_same<decltype(base::data(str)), char*>::value,
                  "base::data(str) should be of type char*");
    EXPECT_EQ(str.data(), base::data(str));
  }

  {
    std::string empty_str;
    static_assert(std::is_same<decltype(base::data(empty_str)), char*>::value,
                  "base::data(empty_str) should be of type char*");
    EXPECT_EQ(empty_str.data(), base::data(empty_str));
  }

  {
    std::array<int, 4> array = {{1, 2, 3, 4}};
    static_assert(
        std::is_same<decltype(base::data(array)),
                     decltype(array.data())>::value,
        "base::data(array) should have the same type as array.data()");
    // std::array::data() is not constexpr prior to C++17, hence the runtime
    // check.
    EXPECT_EQ(array.data(), base::data(array));
  }

  {
    constexpr int array[] = {1, 2, 3};
    static_assert(std::is_same<const int*, decltype(base::data(array))>::value,
                  "base::data(array) should be of type const int*");
    static_assert(array == base::data(array),
                  "base::data(array) should be array");
  }

  {
    constexpr std::initializer_list<int> il;
    static_assert(
        std::is_same<decltype(il.begin()), decltype(base::data(il))>::value,
        "base::data(il) should have the same type as il.begin()");
    static_assert(il.begin() == base::data(il),
                  "base::data(il) should be equal to il.begin()");
  }
}

TEST(STLUtilTest, GetUnderlyingContainer) {
  {
    std::queue<int> queue({1, 2, 3, 4, 5});
    static_assert(std::is_same<decltype(GetUnderlyingContainer(queue)),
                               const std::deque<int>&>::value,
                  "GetUnderlyingContainer(queue) should be of type deque");
    EXPECT_THAT(GetUnderlyingContainer(queue),
                testing::ElementsAre(1, 2, 3, 4, 5));
  }

  {
    std::queue<int> queue;
    EXPECT_THAT(GetUnderlyingContainer(queue), testing::ElementsAre());
  }

  {
    base::queue<int> queue({1, 2, 3, 4, 5});
    static_assert(
        std::is_same<decltype(GetUnderlyingContainer(queue)),
                     const base::circular_deque<int>&>::value,
        "GetUnderlyingContainer(queue) should be of type circular_deque");
    EXPECT_THAT(GetUnderlyingContainer(queue),
                testing::ElementsAre(1, 2, 3, 4, 5));
  }

  {
    std::vector<int> values = {1, 2, 3, 4, 5};
    std::priority_queue<int> queue(values.begin(), values.end());
    static_assert(std::is_same<decltype(GetUnderlyingContainer(queue)),
                               const std::vector<int>&>::value,
                  "GetUnderlyingContainer(queue) should be of type vector");
    EXPECT_THAT(GetUnderlyingContainer(queue),
                testing::UnorderedElementsAre(1, 2, 3, 4, 5));
  }

  {
    std::stack<int> stack({1, 2, 3, 4, 5});
    static_assert(std::is_same<decltype(GetUnderlyingContainer(stack)),
                               const std::deque<int>&>::value,
                  "GetUnderlyingContainer(stack) should be of type deque");
    EXPECT_THAT(GetUnderlyingContainer(stack),
                testing::ElementsAre(1, 2, 3, 4, 5));
  }
}

TEST(STLUtilTest, STLIsSorted) {
  {
    std::set<int> set;
    set.insert(24);
    set.insert(1);
    set.insert(12);
    EXPECT_TRUE(STLIsSorted(set));
  }

  {
    std::set<ComparableValue> set;
    set.insert(ComparableValue(24));
    set.insert(ComparableValue(1));
    set.insert(ComparableValue(12));
    EXPECT_TRUE(STLIsSorted(set));
  }

  {
    std::vector<int> vector;
    vector.push_back(1);
    vector.push_back(1);
    vector.push_back(4);
    vector.push_back(64);
    vector.push_back(12432);
    EXPECT_TRUE(STLIsSorted(vector));
    vector.back() = 1;
    EXPECT_FALSE(STLIsSorted(vector));
  }

  {
    int array[] = {1, 1, 4, 64, 12432};
    EXPECT_TRUE(STLIsSorted(array));
    array[4] = 1;
    EXPECT_FALSE(STLIsSorted(array));
  }
}

TEST(STLUtilTest, STLSetDifference) {
  std::set<int> a1;
  a1.insert(1);
  a1.insert(2);
  a1.insert(3);
  a1.insert(4);

  std::set<int> a2;
  a2.insert(3);
  a2.insert(4);
  a2.insert(5);
  a2.insert(6);
  a2.insert(7);

  {
    std::set<int> difference;
    difference.insert(1);
    difference.insert(2);
    EXPECT_EQ(difference, STLSetDifference<std::set<int> >(a1, a2));
  }

  {
    std::set<int> difference;
    difference.insert(5);
    difference.insert(6);
    difference.insert(7);
    EXPECT_EQ(difference, STLSetDifference<std::set<int> >(a2, a1));
  }

  {
    std::vector<int> difference;
    difference.push_back(1);
    difference.push_back(2);
    EXPECT_EQ(difference, STLSetDifference<std::vector<int> >(a1, a2));
  }

  {
    std::vector<int> difference;
    difference.push_back(5);
    difference.push_back(6);
    difference.push_back(7);
    EXPECT_EQ(difference, STLSetDifference<std::vector<int> >(a2, a1));
  }
}

TEST(STLUtilTest, STLSetUnion) {
  std::set<int> a1;
  a1.insert(1);
  a1.insert(2);
  a1.insert(3);
  a1.insert(4);

  std::set<int> a2;
  a2.insert(3);
  a2.insert(4);
  a2.insert(5);
  a2.insert(6);
  a2.insert(7);

  {
    std::set<int> result;
    result.insert(1);
    result.insert(2);
    result.insert(3);
    result.insert(4);
    result.insert(5);
    result.insert(6);
    result.insert(7);
    EXPECT_EQ(result, STLSetUnion<std::set<int> >(a1, a2));
  }

  {
    std::set<int> result;
    result.insert(1);
    result.insert(2);
    result.insert(3);
    result.insert(4);
    result.insert(5);
    result.insert(6);
    result.insert(7);
    EXPECT_EQ(result, STLSetUnion<std::set<int> >(a2, a1));
  }

  {
    std::vector<int> result;
    result.push_back(1);
    result.push_back(2);
    result.push_back(3);
    result.push_back(4);
    result.push_back(5);
    result.push_back(6);
    result.push_back(7);
    EXPECT_EQ(result, STLSetUnion<std::vector<int> >(a1, a2));
  }

  {
    std::vector<int> result;
    result.push_back(1);
    result.push_back(2);
    result.push_back(3);
    result.push_back(4);
    result.push_back(5);
    result.push_back(6);
    result.push_back(7);
    EXPECT_EQ(result, STLSetUnion<std::vector<int> >(a2, a1));
  }
}

TEST(STLUtilTest, STLSetIntersection) {
  std::set<int> a1;
  a1.insert(1);
  a1.insert(2);
  a1.insert(3);
  a1.insert(4);

  std::set<int> a2;
  a2.insert(3);
  a2.insert(4);
  a2.insert(5);
  a2.insert(6);
  a2.insert(7);

  {
    std::set<int> result;
    result.insert(3);
    result.insert(4);
    EXPECT_EQ(result, STLSetIntersection<std::set<int> >(a1, a2));
  }

  {
    std::set<int> result;
    result.insert(3);
    result.insert(4);
    EXPECT_EQ(result, STLSetIntersection<std::set<int> >(a2, a1));
  }

  {
    std::vector<int> result;
    result.push_back(3);
    result.push_back(4);
    EXPECT_EQ(result, STLSetIntersection<std::vector<int> >(a1, a2));
  }

  {
    std::vector<int> result;
    result.push_back(3);
    result.push_back(4);
    EXPECT_EQ(result, STLSetIntersection<std::vector<int> >(a2, a1));
  }
}

TEST(STLUtilTest, STLIncludes) {
  std::set<int> a1;
  a1.insert(1);
  a1.insert(2);
  a1.insert(3);
  a1.insert(4);

  std::set<int> a2;
  a2.insert(3);
  a2.insert(4);

  std::set<int> a3;
  a3.insert(3);
  a3.insert(4);
  a3.insert(5);

  EXPECT_TRUE(STLIncludes<std::set<int> >(a1, a2));
  EXPECT_FALSE(STLIncludes<std::set<int> >(a1, a3));
  EXPECT_FALSE(STLIncludes<std::set<int> >(a2, a1));
  EXPECT_FALSE(STLIncludes<std::set<int> >(a2, a3));
  EXPECT_FALSE(STLIncludes<std::set<int> >(a3, a1));
  EXPECT_TRUE(STLIncludes<std::set<int> >(a3, a2));
}

TEST(Erase, String) {
  const std::pair<std::string, std::string> test_data[] = {
      {"", ""}, {"abc", "bc"}, {"abca", "bc"},
  };

  for (auto test_case : test_data) {
    Erase(test_case.first, 'a');
    EXPECT_EQ(test_case.second, test_case.first);
  }

  for (auto test_case : test_data) {
    EraseIf(test_case.first, [](char elem) { return elem < 'b'; });
    EXPECT_EQ(test_case.second, test_case.first);
  }
}

TEST(Erase, String16) {
  std::pair<base::string16, base::string16> test_data[] = {
      {base::string16(), base::string16()},
      {UTF8ToUTF16("abc"), UTF8ToUTF16("bc")},
      {UTF8ToUTF16("abca"), UTF8ToUTF16("bc")},
  };

  const base::string16 letters = UTF8ToUTF16("ab");
  for (auto test_case : test_data) {
    Erase(test_case.first, letters[0]);
    EXPECT_EQ(test_case.second, test_case.first);
  }

  for (auto test_case : test_data) {
    EraseIf(test_case.first, [&](short elem) { return elem < letters[1]; });
    EXPECT_EQ(test_case.second, test_case.first);
  }
}

TEST(Erase, Deque) {
  RunEraseTest<std::deque<int>>();
  RunEraseIfTest<std::deque<std::pair<int, int>>>();
}

TEST(Erase, Vector) {
  RunEraseTest<std::vector<int>>();
  RunEraseIfTest<std::vector<std::pair<int, int>>>();
}

TEST(Erase, ForwardList) {
  RunEraseTest<std::forward_list<int>>();
  RunEraseIfTest<std::forward_list<std::pair<int, int>>>();
}

TEST(Erase, List) {
  RunEraseTest<std::list<int>>();
  RunEraseIfTest<std::list<std::pair<int, int>>>();
}

TEST(Erase, Map) {
  RunEraseIfTest<std::map<int, int>>();
  RunEraseIfTest<std::map<int, int, std::greater<int>>>();
}

TEST(Erase, Multimap) {
  RunEraseIfTest<std::multimap<int, int>>();
  RunEraseIfTest<std::multimap<int, int, std::greater<int>>>();
}

TEST(Erase, Set) {
  RunEraseIfTest<std::set<std::pair<int, int>>>();
  RunEraseIfTest<
      std::set<std::pair<int, int>, std::greater<std::pair<int, int>>>>();
}

TEST(Erase, Multiset) {
  RunEraseIfTest<std::multiset<std::pair<int, int>>>();
  RunEraseIfTest<
      std::multiset<std::pair<int, int>, std::greater<std::pair<int, int>>>>();
}

TEST(Erase, UnorderedMap) {
  RunEraseIfTest<std::unordered_map<int, int>>();
  RunEraseIfTest<std::unordered_map<int, int, CustomIntHash>>();
}

TEST(Erase, UnorderedMultimap) {
  RunEraseIfTest<std::unordered_multimap<int, int>>();
  RunEraseIfTest<std::unordered_multimap<int, int, CustomIntHash>>();
}

TEST(Erase, UnorderedSet) {
  RunEraseIfTest<std::unordered_set<std::pair<int, int>, HashByFirst>>();
}

TEST(Erase, UnorderedMultiset) {
  RunEraseIfTest<std::unordered_multiset<std::pair<int, int>, HashByFirst>>();
}

TEST(Erase, IsNotIn) {
  // Should keep both '2' but only one '4', like std::set_intersection.
  std::vector<int> lhs = {0, 2, 2, 4, 4, 4, 6, 8, 10};
  std::vector<int> rhs = {1, 2, 2, 4, 5, 6, 7};
  std::vector<int> expected = {2, 2, 4, 6};
  EraseIf(lhs, IsNotIn<std::vector<int>>(rhs));
  EXPECT_EQ(expected, lhs);
}

TEST(ContainsValue, OrdinaryArrays) {
  const char allowed_chars[] = {'a', 'b', 'c', 'd'};
  EXPECT_TRUE(ContainsValue(allowed_chars, 'a'));
  EXPECT_FALSE(ContainsValue(allowed_chars, 'z'));
  EXPECT_FALSE(ContainsValue(allowed_chars, 0));

  const char allowed_chars_including_nul[] = "abcd";
  EXPECT_TRUE(ContainsValue(allowed_chars_including_nul, 0));
}

TEST(STLUtilTest, OptionalOrNullptr) {
  Optional<float> optional;
  EXPECT_EQ(nullptr, base::OptionalOrNullptr(optional));

  optional = 0.1f;
  EXPECT_EQ(&optional.value(), base::OptionalOrNullptr(optional));
  EXPECT_NE(nullptr, base::OptionalOrNullptr(optional));
}

}  // namespace
}  // namespace base

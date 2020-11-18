// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests SimpleLinkedHashMap.

#include "net/third_party/quiche/src/common/simple_linked_hash_map.h"

#include <memory>
#include <utility>

#include "net/third_party/quiche/src/common/platform/api/quiche_test.h"

using testing::Pair;
using testing::Pointee;
using testing::UnorderedElementsAre;

namespace quiche {
namespace test {

// Tests that move constructor works.
TEST(LinkedHashMapTest, Move) {
  // Use unique_ptr as an example of a non-copyable type.
  SimpleLinkedHashMap<int, std::unique_ptr<int>> m;
  m[2] = std::make_unique<int>(12);
  m[3] = std::make_unique<int>(13);
  SimpleLinkedHashMap<int, std::unique_ptr<int>> n = std::move(m);
  EXPECT_THAT(n,
              UnorderedElementsAre(Pair(2, Pointee(12)), Pair(3, Pointee(13))));
}

TEST(LinkedHashMapTest, CanEmplaceMoveOnly) {
  SimpleLinkedHashMap<int, std::unique_ptr<int>> m;
  struct Data {
    int k, v;
  };
  const Data data[] = {{1, 123}, {3, 345}, {2, 234}, {4, 456}};
  for (const auto& kv : data) {
    m.emplace(std::piecewise_construct, std::make_tuple(kv.k),
              std::make_tuple(new int{kv.v}));
  }
  EXPECT_TRUE(m.contains(2));
  auto found = m.find(2);
  ASSERT_TRUE(found != m.end());
  EXPECT_EQ(234, *found->second);
}

struct NoCopy {
  explicit NoCopy(int x) : x(x) {}
  NoCopy(const NoCopy&) = delete;
  NoCopy& operator=(const NoCopy&) = delete;
  NoCopy(NoCopy&&) = delete;
  NoCopy& operator=(NoCopy&&) = delete;
  int x;
};

TEST(LinkedHashMapTest, CanEmplaceNoMoveNoCopy) {
  SimpleLinkedHashMap<int, NoCopy> m;
  struct Data {
    int k, v;
  };
  const Data data[] = {{1, 123}, {3, 345}, {2, 234}, {4, 456}};
  for (const auto& kv : data) {
    m.emplace(std::piecewise_construct, std::make_tuple(kv.k),
              std::make_tuple(kv.v));
  }
  EXPECT_TRUE(m.contains(2));
  auto found = m.find(2);
  ASSERT_TRUE(found != m.end());
  EXPECT_EQ(234, found->second.x);
}

TEST(LinkedHashMapTest, ConstKeys) {
  SimpleLinkedHashMap<int, int> m;
  m.insert(std::make_pair(1, 2));
  // Test that keys are const in iteration.
  std::pair<int, int>& p = *m.begin();
  EXPECT_EQ(1, p.first);
}

// Tests that iteration from begin() to end() works
TEST(LinkedHashMapTest, Iteration) {
  SimpleLinkedHashMap<int, int> m;
  EXPECT_TRUE(m.begin() == m.end());

  m.insert(std::make_pair(2, 12));
  m.insert(std::make_pair(1, 11));
  m.insert(std::make_pair(3, 13));

  SimpleLinkedHashMap<int, int>::iterator i = m.begin();
  ASSERT_TRUE(m.begin() == i);
  ASSERT_TRUE(m.end() != i);
  EXPECT_EQ(2, i->first);
  EXPECT_EQ(12, i->second);

  ++i;
  ASSERT_TRUE(m.end() != i);
  EXPECT_EQ(1, i->first);
  EXPECT_EQ(11, i->second);

  ++i;
  ASSERT_TRUE(m.end() != i);
  EXPECT_EQ(3, i->first);
  EXPECT_EQ(13, i->second);

  ++i;  // Should be the end of the line.
  ASSERT_TRUE(m.end() == i);
}

// Tests that reverse iteration from rbegin() to rend() works
TEST(LinkedHashMapTest, ReverseIteration) {
  SimpleLinkedHashMap<int, int> m;
  EXPECT_TRUE(m.rbegin() == m.rend());

  m.insert(std::make_pair(2, 12));
  m.insert(std::make_pair(1, 11));
  m.insert(std::make_pair(3, 13));

  SimpleLinkedHashMap<int, int>::reverse_iterator i = m.rbegin();
  ASSERT_TRUE(m.rbegin() == i);
  ASSERT_TRUE(m.rend() != i);
  EXPECT_EQ(3, i->first);
  EXPECT_EQ(13, i->second);

  ++i;
  ASSERT_TRUE(m.rend() != i);
  EXPECT_EQ(1, i->first);
  EXPECT_EQ(11, i->second);

  ++i;
  ASSERT_TRUE(m.rend() != i);
  EXPECT_EQ(2, i->first);
  EXPECT_EQ(12, i->second);

  ++i;  // Should be the end of the line.
  ASSERT_TRUE(m.rend() == i);
}

// Tests that clear() works
TEST(LinkedHashMapTest, Clear) {
  SimpleLinkedHashMap<int, int> m;
  m.insert(std::make_pair(2, 12));
  m.insert(std::make_pair(1, 11));
  m.insert(std::make_pair(3, 13));

  ASSERT_EQ(3u, m.size());

  m.clear();

  EXPECT_EQ(0u, m.size());

  m.clear();  // Make sure we can call it on an empty map.

  EXPECT_EQ(0u, m.size());
}

// Tests that size() works.
TEST(LinkedHashMapTest, Size) {
  SimpleLinkedHashMap<int, int> m;
  EXPECT_EQ(0u, m.size());
  m.insert(std::make_pair(2, 12));
  EXPECT_EQ(1u, m.size());
  m.insert(std::make_pair(1, 11));
  EXPECT_EQ(2u, m.size());
  m.insert(std::make_pair(3, 13));
  EXPECT_EQ(3u, m.size());
  m.clear();
  EXPECT_EQ(0u, m.size());
}

// Tests empty()
TEST(LinkedHashMapTest, Empty) {
  SimpleLinkedHashMap<int, int> m;
  ASSERT_TRUE(m.empty());
  m.insert(std::make_pair(2, 12));
  ASSERT_FALSE(m.empty());
  m.clear();
  ASSERT_TRUE(m.empty());
}

TEST(LinkedHashMapTest, Erase) {
  SimpleLinkedHashMap<int, int> m;
  ASSERT_EQ(0u, m.size());
  EXPECT_EQ(0u, m.erase(2));  // Nothing to erase yet

  m.insert(std::make_pair(2, 12));
  ASSERT_EQ(1u, m.size());
  EXPECT_EQ(1u, m.erase(2));
  EXPECT_EQ(0u, m.size());

  EXPECT_EQ(0u, m.erase(2));  // Make sure nothing bad happens if we repeat.
  EXPECT_EQ(0u, m.size());
}

TEST(LinkedHashMapTest, Erase2) {
  SimpleLinkedHashMap<int, int> m;
  ASSERT_EQ(0u, m.size());
  EXPECT_EQ(0u, m.erase(2));  // Nothing to erase yet

  m.insert(std::make_pair(2, 12));
  m.insert(std::make_pair(1, 11));
  m.insert(std::make_pair(3, 13));
  m.insert(std::make_pair(4, 14));
  ASSERT_EQ(4u, m.size());

  // Erase middle two
  EXPECT_EQ(1u, m.erase(1));
  EXPECT_EQ(1u, m.erase(3));

  EXPECT_EQ(2u, m.size());

  // Make sure we can still iterate over everything that's left.
  SimpleLinkedHashMap<int, int>::iterator it = m.begin();
  ASSERT_TRUE(it != m.end());
  EXPECT_EQ(12, it->second);
  ++it;
  ASSERT_TRUE(it != m.end());
  EXPECT_EQ(14, it->second);
  ++it;
  ASSERT_TRUE(it == m.end());

  EXPECT_EQ(0u, m.erase(1));  // Make sure nothing bad happens if we repeat.
  ASSERT_EQ(2u, m.size());

  EXPECT_EQ(1u, m.erase(2));
  EXPECT_EQ(1u, m.erase(4));
  ASSERT_EQ(0u, m.size());

  EXPECT_EQ(0u, m.erase(1));  // Make sure nothing bad happens if we repeat.
  ASSERT_EQ(0u, m.size());
}

// Test that erase(iter,iter) and erase(iter) compile and work.
TEST(LinkedHashMapTest, Erase3) {
  SimpleLinkedHashMap<int, int> m;

  m.insert(std::make_pair(1, 11));
  m.insert(std::make_pair(2, 12));
  m.insert(std::make_pair(3, 13));
  m.insert(std::make_pair(4, 14));

  // Erase middle two
  SimpleLinkedHashMap<int, int>::iterator it2 = m.find(2);
  SimpleLinkedHashMap<int, int>::iterator it4 = m.find(4);
  EXPECT_EQ(m.erase(it2, it4), m.find(4));
  EXPECT_EQ(2u, m.size());

  // Make sure we can still iterate over everything that's left.
  SimpleLinkedHashMap<int, int>::iterator it = m.begin();
  ASSERT_TRUE(it != m.end());
  EXPECT_EQ(11, it->second);
  ++it;
  ASSERT_TRUE(it != m.end());
  EXPECT_EQ(14, it->second);
  ++it;
  ASSERT_TRUE(it == m.end());

  // Erase first one using an iterator.
  EXPECT_EQ(m.erase(m.begin()), m.find(4));

  // Only the last element should be left.
  it = m.begin();
  ASSERT_TRUE(it != m.end());
  EXPECT_EQ(14, it->second);
  ++it;
  ASSERT_TRUE(it == m.end());
}

TEST(LinkedHashMapTest, Insertion) {
  SimpleLinkedHashMap<int, int> m;
  ASSERT_EQ(0u, m.size());
  std::pair<SimpleLinkedHashMap<int, int>::iterator, bool> result;

  result = m.insert(std::make_pair(2, 12));
  ASSERT_EQ(1u, m.size());
  EXPECT_TRUE(result.second);
  EXPECT_EQ(2, result.first->first);
  EXPECT_EQ(12, result.first->second);

  result = m.insert(std::make_pair(1, 11));
  ASSERT_EQ(2u, m.size());
  EXPECT_TRUE(result.second);
  EXPECT_EQ(1, result.first->first);
  EXPECT_EQ(11, result.first->second);

  result = m.insert(std::make_pair(3, 13));
  SimpleLinkedHashMap<int, int>::iterator result_iterator = result.first;
  ASSERT_EQ(3u, m.size());
  EXPECT_TRUE(result.second);
  EXPECT_EQ(3, result.first->first);
  EXPECT_EQ(13, result.first->second);

  result = m.insert(std::make_pair(3, 13));
  EXPECT_EQ(3u, m.size());
  EXPECT_FALSE(result.second) << "No insertion should have occurred.";
  EXPECT_TRUE(result_iterator == result.first)
      << "Duplicate insertion should have given us the original iterator.";
}

static std::pair<int, int> Pair(int i, int j) {
  return {i, j};
}

// Test front accessors.
TEST(LinkedHashMapTest, Front) {
  SimpleLinkedHashMap<int, int> m;

  m.insert(std::make_pair(2, 12));
  m.insert(std::make_pair(1, 11));
  m.insert(std::make_pair(3, 13));

  EXPECT_EQ(3u, m.size());
  EXPECT_EQ(Pair(2, 12), m.front());
  m.pop_front();
  EXPECT_EQ(2u, m.size());
  EXPECT_EQ(Pair(1, 11), m.front());
  m.pop_front();
  EXPECT_EQ(1u, m.size());
  EXPECT_EQ(Pair(3, 13), m.front());
  m.pop_front();
  EXPECT_TRUE(m.empty());
}

TEST(LinkedHashMapTest, Find) {
  SimpleLinkedHashMap<int, int> m;

  EXPECT_TRUE(m.end() == m.find(1))
      << "We shouldn't find anything in an empty map.";

  m.insert(std::make_pair(2, 12));
  EXPECT_TRUE(m.end() == m.find(1))
      << "We shouldn't find an element that doesn't exist in the map.";

  std::pair<SimpleLinkedHashMap<int, int>::iterator, bool> result =
      m.insert(std::make_pair(1, 11));
  ASSERT_TRUE(result.second);
  ASSERT_TRUE(m.end() != result.first);
  EXPECT_TRUE(result.first == m.find(1))
      << "We should have found an element we know exists in the map.";
  EXPECT_EQ(11, result.first->second);

  // Check that a follow-up insertion doesn't affect our original
  m.insert(std::make_pair(3, 13));
  SimpleLinkedHashMap<int, int>::iterator it = m.find(1);
  ASSERT_TRUE(m.end() != it);
  EXPECT_EQ(11, it->second);

  m.clear();
  EXPECT_TRUE(m.end() == m.find(1))
      << "We shouldn't find anything in a map that we've cleared.";
}

TEST(LinkedHashMapTest, Contains) {
  SimpleLinkedHashMap<int, int> m;

  EXPECT_FALSE(m.contains(1)) << "An empty map shouldn't contain anything.";

  m.insert(std::make_pair(2, 12));
  EXPECT_FALSE(m.contains(1))
      << "The map shouldn't contain an element that doesn't exist.";

  m.insert(std::make_pair(1, 11));
  EXPECT_TRUE(m.contains(1))
      << "The map should contain an element that we know exists.";

  m.clear();
  EXPECT_FALSE(m.contains(1))
      << "A map that we've cleared shouldn't contain anything.";
}

TEST(LinkedHashMapTest, Swap) {
  SimpleLinkedHashMap<int, int> m1;
  SimpleLinkedHashMap<int, int> m2;
  m1.insert(std::make_pair(1, 1));
  m1.insert(std::make_pair(2, 2));
  m2.insert(std::make_pair(3, 3));
  ASSERT_EQ(2u, m1.size());
  ASSERT_EQ(1u, m2.size());
  m1.swap(m2);
  ASSERT_EQ(1u, m1.size());
  ASSERT_EQ(2u, m2.size());
}

TEST(LinkedHashMapTest, CustomHashAndEquality) {
  struct CustomIntHash {
    size_t operator()(int x) const { return x; }
  };
  SimpleLinkedHashMap<int, int, CustomIntHash> m;
  m.insert(std::make_pair(1, 1));
  EXPECT_TRUE(m.contains(1));
  EXPECT_EQ(1, m[1]);
}

}  // namespace test
}  // namespace quiche

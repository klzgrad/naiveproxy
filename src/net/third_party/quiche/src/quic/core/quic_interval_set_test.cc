// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_interval_set.h"

#include <stdarg.h>

#include <algorithm>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

using ::testing::ElementsAreArray;

class QuicIntervalSetTest : public QuicTest {
 protected:
  virtual void SetUp() {
    // Initialize two QuicIntervalSets for union, intersection, and difference
    // tests
    is.Add(100, 200);
    is.Add(300, 400);
    is.Add(500, 600);
    is.Add(700, 800);
    is.Add(900, 1000);
    is.Add(1100, 1200);
    is.Add(1300, 1400);
    is.Add(1500, 1600);
    is.Add(1700, 1800);
    is.Add(1900, 2000);
    is.Add(2100, 2200);

    // Lots of different cases:
    other.Add(50, 70);      // disjoint, at the beginning
    other.Add(2250, 2270);  // disjoint, at the end
    other.Add(650, 670);    // disjoint, in the middle
    other.Add(350, 360);    // included
    other.Add(370, 380);    // also included (two at once)
    other.Add(470, 530);    // overlaps low end
    other.Add(770, 830);    // overlaps high end
    other.Add(870, 900);    // meets at low end
    other.Add(1200, 1230);  // meets at high end
    other.Add(1270, 1830);  // overlaps multiple ranges
  }

  virtual void TearDown() {
    is.Clear();
    EXPECT_TRUE(is.Empty());
    other.Clear();
    EXPECT_TRUE(other.Empty());
  }
  QuicIntervalSet<int> is;
  QuicIntervalSet<int> other;
};

TEST_F(QuicIntervalSetTest, IsDisjoint) {
  EXPECT_TRUE(is.IsDisjoint(QuicInterval<int>(0, 99)));
  EXPECT_TRUE(is.IsDisjoint(QuicInterval<int>(0, 100)));
  EXPECT_TRUE(is.IsDisjoint(QuicInterval<int>(200, 200)));
  EXPECT_TRUE(is.IsDisjoint(QuicInterval<int>(200, 299)));
  EXPECT_TRUE(is.IsDisjoint(QuicInterval<int>(400, 407)));
  EXPECT_TRUE(is.IsDisjoint(QuicInterval<int>(405, 499)));
  EXPECT_TRUE(is.IsDisjoint(QuicInterval<int>(2300, 2300)));
  EXPECT_TRUE(
      is.IsDisjoint(QuicInterval<int>(2300, std::numeric_limits<int>::max())));
  EXPECT_FALSE(is.IsDisjoint(QuicInterval<int>(100, 105)));
  EXPECT_FALSE(is.IsDisjoint(QuicInterval<int>(199, 300)));
  EXPECT_FALSE(is.IsDisjoint(QuicInterval<int>(250, 450)));
  EXPECT_FALSE(is.IsDisjoint(QuicInterval<int>(299, 400)));
  EXPECT_FALSE(is.IsDisjoint(QuicInterval<int>(250, 2000)));
  EXPECT_FALSE(
      is.IsDisjoint(QuicInterval<int>(2199, std::numeric_limits<int>::max())));
  // Empty intervals.
  EXPECT_TRUE(is.IsDisjoint(QuicInterval<int>(90, 90)));
  EXPECT_TRUE(is.IsDisjoint(QuicInterval<int>(100, 100)));
  EXPECT_TRUE(is.IsDisjoint(QuicInterval<int>(100, 90)));
  EXPECT_TRUE(is.IsDisjoint(QuicInterval<int>(150, 150)));
  EXPECT_TRUE(is.IsDisjoint(QuicInterval<int>(200, 200)));
  EXPECT_TRUE(is.IsDisjoint(QuicInterval<int>(400, 300)));
}

// Base helper method for verifying the contents of an interval set.
// Returns true iff <is> contains <count> intervals whose successive
// endpoints match the sequence of args in <ap>:
static bool VA_Check(const QuicIntervalSet<int>& is, int count, va_list ap) {
  std::vector<QuicInterval<int>> intervals(is.begin(), is.end());
  if (count != static_cast<int>(intervals.size())) {
    QUIC_LOG(ERROR) << "Expected " << count << " intervals, got "
                    << intervals.size() << ": " << is;
    return false;
  }
  if (count != static_cast<int>(is.Size())) {
    QUIC_LOG(ERROR) << "Expected " << count << " intervals, got Size "
                    << is.Size() << ": " << is;
    return false;
  }
  bool result = true;
  for (int i = 0; i < count; i++) {
    int min = va_arg(ap, int);
    int max = va_arg(ap, int);
    if (min != intervals[i].min() || max != intervals[i].max()) {
      QUIC_LOG(ERROR) << "Expected: [" << min << ", " << max << ") got "
                      << intervals[i] << " in " << is;
      result = false;
    }
  }
  return result;
}

static bool Check(const QuicIntervalSet<int>& is, int count, ...) {
  va_list ap;
  va_start(ap, count);
  const bool result = VA_Check(is, count, ap);
  va_end(ap);
  return result;
}

// Some helper functions for testing Contains and Find, which are logically the
// same.
static void TestContainsAndFind(const QuicIntervalSet<int>& is, int value) {
  EXPECT_TRUE(is.Contains(value)) << "Set does not contain " << value;
  auto it = is.Find(value);
  EXPECT_NE(it, is.end()) << "No iterator to interval containing " << value;
  EXPECT_TRUE(it->Contains(value)) << "Iterator does not contain " << value;
}

static void TestContainsAndFind(const QuicIntervalSet<int>& is,
                                int min,
                                int max) {
  EXPECT_TRUE(is.Contains(min, max))
      << "Set does not contain interval with min " << min << "and max " << max;
  auto it = is.Find(min, max);
  EXPECT_NE(it, is.end()) << "No iterator to interval with min " << min
                          << "and max " << max;
  EXPECT_TRUE(it->Contains(QuicInterval<int>(min, max)))
      << "Iterator does not contain interval with min " << min << "and max "
      << max;
}

static void TestNotContainsAndFind(const QuicIntervalSet<int>& is, int value) {
  EXPECT_FALSE(is.Contains(value)) << "Set contains " << value;
  auto it = is.Find(value);
  EXPECT_EQ(it, is.end()) << "There is iterator to interval containing "
                          << value;
}

static void TestNotContainsAndFind(const QuicIntervalSet<int>& is,
                                   int min,
                                   int max) {
  EXPECT_FALSE(is.Contains(min, max))
      << "Set contains interval with min " << min << "and max " << max;
  auto it = is.Find(min, max);
  EXPECT_EQ(it, is.end()) << "There is iterator to interval with min " << min
                          << "and max " << max;
}

TEST_F(QuicIntervalSetTest, AddOptimizedForAppend) {
  QuicIntervalSet<int> empty_one, empty_two;
  empty_one.AddOptimizedForAppend(QuicInterval<int>(0, 99));
  EXPECT_TRUE(Check(empty_one, 1, 0, 99));

  empty_two.AddOptimizedForAppend(1, 50);
  EXPECT_TRUE(Check(empty_two, 1, 1, 50));

  QuicIntervalSet<int> iset;
  iset.AddOptimizedForAppend(100, 150);
  iset.AddOptimizedForAppend(200, 250);
  EXPECT_TRUE(Check(iset, 2, 100, 150, 200, 250));

  iset.AddOptimizedForAppend(199, 200);
  EXPECT_TRUE(Check(iset, 2, 100, 150, 199, 250));

  iset.AddOptimizedForAppend(251, 260);
  EXPECT_TRUE(Check(iset, 3, 100, 150, 199, 250, 251, 260));

  iset.AddOptimizedForAppend(252, 260);
  EXPECT_TRUE(Check(iset, 3, 100, 150, 199, 250, 251, 260));

  iset.AddOptimizedForAppend(252, 300);
  EXPECT_TRUE(Check(iset, 3, 100, 150, 199, 250, 251, 300));

  iset.AddOptimizedForAppend(300, 350);
  EXPECT_TRUE(Check(iset, 3, 100, 150, 199, 250, 251, 350));
}

TEST_F(QuicIntervalSetTest, PopFront) {
  QuicIntervalSet<int> iset{{100, 200}, {400, 500}, {700, 800}};
  EXPECT_TRUE(Check(iset, 3, 100, 200, 400, 500, 700, 800));

  iset.PopFront();
  EXPECT_TRUE(Check(iset, 2, 400, 500, 700, 800));

  iset.PopFront();
  EXPECT_TRUE(Check(iset, 1, 700, 800));

  iset.PopFront();
  EXPECT_TRUE(iset.Empty());
}

TEST_F(QuicIntervalSetTest, TrimLessThan) {
  QuicIntervalSet<int> iset{{100, 200}, {400, 500}, {700, 800}};
  EXPECT_TRUE(Check(iset, 3, 100, 200, 400, 500, 700, 800));

  EXPECT_FALSE(iset.TrimLessThan(99));
  EXPECT_FALSE(iset.TrimLessThan(100));
  EXPECT_TRUE(Check(iset, 3, 100, 200, 400, 500, 700, 800));

  EXPECT_TRUE(iset.TrimLessThan(101));
  EXPECT_TRUE(Check(iset, 3, 101, 200, 400, 500, 700, 800));

  EXPECT_TRUE(iset.TrimLessThan(199));
  EXPECT_TRUE(Check(iset, 3, 199, 200, 400, 500, 700, 800));

  EXPECT_TRUE(iset.TrimLessThan(450));
  EXPECT_TRUE(Check(iset, 2, 450, 500, 700, 800));

  EXPECT_TRUE(iset.TrimLessThan(500));
  EXPECT_TRUE(Check(iset, 1, 700, 800));

  EXPECT_TRUE(iset.TrimLessThan(801));
  EXPECT_TRUE(iset.Empty());

  EXPECT_FALSE(iset.TrimLessThan(900));
  EXPECT_TRUE(iset.Empty());
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetBasic) {
  // Test Add, Get, Contains and Find
  QuicIntervalSet<int> iset;
  EXPECT_TRUE(iset.Empty());
  EXPECT_EQ(0u, iset.Size());
  iset.Add(100, 200);
  EXPECT_FALSE(iset.Empty());
  EXPECT_EQ(1u, iset.Size());
  iset.Add(100, 150);
  iset.Add(150, 200);
  iset.Add(130, 170);
  iset.Add(90, 150);
  iset.Add(170, 220);
  iset.Add(300, 400);
  iset.Add(250, 450);
  EXPECT_FALSE(iset.Empty());
  EXPECT_EQ(2u, iset.Size());
  EXPECT_TRUE(Check(iset, 2, 90, 220, 250, 450));

  // Test two intervals with a.max == b.min, that will just join up.
  iset.Clear();
  iset.Add(100, 200);
  iset.Add(200, 300);
  EXPECT_FALSE(iset.Empty());
  EXPECT_EQ(1u, iset.Size());
  EXPECT_TRUE(Check(iset, 1, 100, 300));

  // Test adding two sets together.
  iset.Clear();
  QuicIntervalSet<int> iset_add;
  iset.Add(100, 200);
  iset.Add(100, 150);
  iset.Add(150, 200);
  iset.Add(130, 170);
  iset_add.Add(90, 150);
  iset_add.Add(170, 220);
  iset_add.Add(300, 400);
  iset_add.Add(250, 450);

  iset.Union(iset_add);
  EXPECT_FALSE(iset.Empty());
  EXPECT_EQ(2u, iset.Size());
  EXPECT_TRUE(Check(iset, 2, 90, 220, 250, 450));

  // Test begin()/end(), and rbegin()/rend()
  // to iterate over intervals.
  {
    std::vector<QuicInterval<int>> expected(iset.begin(), iset.end());

    std::vector<QuicInterval<int>> actual1;
    std::copy(iset.begin(), iset.end(), back_inserter(actual1));
    ASSERT_EQ(expected.size(), actual1.size());

    std::vector<QuicInterval<int>> actual2;
    std::copy(iset.begin(), iset.end(), back_inserter(actual2));
    ASSERT_EQ(expected.size(), actual2.size());

    for (size_t i = 0; i < expected.size(); i++) {
      EXPECT_EQ(expected[i].min(), actual1[i].min());
      EXPECT_EQ(expected[i].max(), actual1[i].max());

      EXPECT_EQ(expected[i].min(), actual2[i].min());
      EXPECT_EQ(expected[i].max(), actual2[i].max());
    }

    // Ensure that the rbegin()/rend() iterators correctly yield the intervals
    // in reverse order.
    EXPECT_THAT(std::vector<QuicInterval<int>>(iset.rbegin(), iset.rend()),
                ElementsAreArray(expected.rbegin(), expected.rend()));
  }

  TestNotContainsAndFind(iset, 89);
  TestContainsAndFind(iset, 90);
  TestContainsAndFind(iset, 120);
  TestContainsAndFind(iset, 219);
  TestNotContainsAndFind(iset, 220);
  TestNotContainsAndFind(iset, 235);
  TestNotContainsAndFind(iset, 249);
  TestContainsAndFind(iset, 250);
  TestContainsAndFind(iset, 300);
  TestContainsAndFind(iset, 449);
  TestNotContainsAndFind(iset, 450);
  TestNotContainsAndFind(iset, 451);

  TestNotContainsAndFind(iset, 50, 60);
  TestNotContainsAndFind(iset, 50, 90);
  TestNotContainsAndFind(iset, 50, 200);
  TestNotContainsAndFind(iset, 90, 90);
  TestContainsAndFind(iset, 90, 200);
  TestContainsAndFind(iset, 100, 200);
  TestContainsAndFind(iset, 100, 220);
  TestNotContainsAndFind(iset, 100, 221);
  TestNotContainsAndFind(iset, 220, 220);
  TestNotContainsAndFind(iset, 240, 300);
  TestContainsAndFind(iset, 250, 300);
  TestContainsAndFind(iset, 260, 300);
  TestContainsAndFind(iset, 300, 450);
  TestNotContainsAndFind(iset, 300, 451);

  QuicIntervalSet<int> iset_contains;
  iset_contains.Add(50, 90);
  EXPECT_FALSE(iset.Contains(iset_contains));
  iset_contains.Clear();

  iset_contains.Add(90, 200);
  EXPECT_TRUE(iset.Contains(iset_contains));
  iset_contains.Add(100, 200);
  EXPECT_TRUE(iset.Contains(iset_contains));
  iset_contains.Add(100, 220);
  EXPECT_TRUE(iset.Contains(iset_contains));
  iset_contains.Add(250, 300);
  EXPECT_TRUE(iset.Contains(iset_contains));
  iset_contains.Add(300, 450);
  EXPECT_TRUE(iset.Contains(iset_contains));
  iset_contains.Add(300, 451);
  EXPECT_FALSE(iset.Contains(iset_contains));
  EXPECT_FALSE(iset.Contains(QuicInterval<int>()));
  EXPECT_FALSE(iset.Contains(QuicIntervalSet<int>()));
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetContainsEmpty) {
  const QuicIntervalSet<int> empty;
  const QuicIntervalSet<int> other_empty;
  const QuicIntervalSet<int> non_empty({{10, 20}, {40, 50}});
  EXPECT_FALSE(empty.Contains(empty));
  EXPECT_FALSE(empty.Contains(other_empty));
  EXPECT_FALSE(empty.Contains(non_empty));
  EXPECT_FALSE(non_empty.Contains(empty));
}

TEST_F(QuicIntervalSetTest, Equality) {
  QuicIntervalSet<int> is_copy = is;
  EXPECT_EQ(is, is);
  EXPECT_EQ(is, is_copy);
  EXPECT_NE(is, other);
  EXPECT_NE(is, QuicIntervalSet<int>());
  EXPECT_EQ(QuicIntervalSet<int>(), QuicIntervalSet<int>());
}

TEST_F(QuicIntervalSetTest, LowerAndUpperBound) {
  QuicIntervalSet<int> intervals;
  intervals.Add(10, 20);
  intervals.Add(30, 40);

  //   [10, 20)  [30, 40)  end
  //   ^                        LowerBound(5)
  //   ^                        LowerBound(10)
  //   ^                        LowerBound(15)
  //             ^              LowerBound(20)
  //             ^              LowerBound(25)
  //             ^              LowerBound(30)
  //             ^              LowerBound(35)
  //                       ^    LowerBound(40)
  //                       ^    LowerBound(50)
  EXPECT_EQ(intervals.LowerBound(5)->min(), 10);
  EXPECT_EQ(intervals.LowerBound(10)->min(), 10);
  EXPECT_EQ(intervals.LowerBound(15)->min(), 10);
  EXPECT_EQ(intervals.LowerBound(20)->min(), 30);
  EXPECT_EQ(intervals.LowerBound(25)->min(), 30);
  EXPECT_EQ(intervals.LowerBound(30)->min(), 30);
  EXPECT_EQ(intervals.LowerBound(35)->min(), 30);
  EXPECT_EQ(intervals.LowerBound(40), intervals.end());
  EXPECT_EQ(intervals.LowerBound(50), intervals.end());

  //   [10, 20)  [30, 40)  end
  //   ^                        UpperBound(5)
  //             ^              UpperBound(10)
  //             ^              UpperBound(15)
  //             ^              UpperBound(20)
  //             ^              UpperBound(25)
  //                       ^    UpperBound(30)
  //                       ^    UpperBound(35)
  //                       ^    UpperBound(40)
  //                       ^    UpperBound(50)
  EXPECT_EQ(intervals.UpperBound(5)->min(), 10);
  EXPECT_EQ(intervals.UpperBound(10)->min(), 30);
  EXPECT_EQ(intervals.UpperBound(15)->min(), 30);
  EXPECT_EQ(intervals.UpperBound(20)->min(), 30);
  EXPECT_EQ(intervals.UpperBound(25)->min(), 30);
  EXPECT_EQ(intervals.UpperBound(30), intervals.end());
  EXPECT_EQ(intervals.UpperBound(35), intervals.end());
  EXPECT_EQ(intervals.UpperBound(40), intervals.end());
  EXPECT_EQ(intervals.UpperBound(50), intervals.end());
}

TEST_F(QuicIntervalSetTest, SpanningInterval) {
  // Spanning interval of an empty set is empty:
  {
    QuicIntervalSet<int> iset;
    const QuicInterval<int>& ival = iset.SpanningInterval();
    EXPECT_TRUE(ival.Empty());
  }

  // Spanning interval of a set with one interval is that interval:
  {
    QuicIntervalSet<int> iset;
    iset.Add(100, 200);
    const QuicInterval<int>& ival = iset.SpanningInterval();
    EXPECT_EQ(100, ival.min());
    EXPECT_EQ(200, ival.max());
  }

  // Spanning interval of a set with multiple elements is determined
  // by the endpoints of the first and last element:
  {
    const QuicInterval<int>& ival = is.SpanningInterval();
    EXPECT_EQ(100, ival.min());
    EXPECT_EQ(2200, ival.max());
  }
  {
    const QuicInterval<int>& ival = other.SpanningInterval();
    EXPECT_EQ(50, ival.min());
    EXPECT_EQ(2270, ival.max());
  }
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetUnion) {
  is.Union(other);
  EXPECT_TRUE(Check(is, 12, 50, 70, 100, 200, 300, 400, 470, 600, 650, 670, 700,
                    830, 870, 1000, 1100, 1230, 1270, 1830, 1900, 2000, 2100,
                    2200, 2250, 2270));
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetIntersection) {
  EXPECT_TRUE(is.Intersects(other));
  EXPECT_TRUE(other.Intersects(is));
  is.Intersection(other);
  EXPECT_TRUE(Check(is, 7, 350, 360, 370, 380, 500, 530, 770, 800, 1300, 1400,
                    1500, 1600, 1700, 1800));
  EXPECT_TRUE(is.Intersects(other));
  EXPECT_TRUE(other.Intersects(is));
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetIntersectionBothEmpty) {
  QuicIntervalSet<std::string> mine, theirs;
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.Empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetIntersectionEmptyMine) {
  QuicIntervalSet<std::string> mine;
  QuicIntervalSet<std::string> theirs("a", "b");
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.Empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetIntersectionEmptyTheirs) {
  QuicIntervalSet<std::string> mine("a", "b");
  QuicIntervalSet<std::string> theirs;
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.Empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetIntersectionTheirsBeforeMine) {
  QuicIntervalSet<std::string> mine("y", "z");
  QuicIntervalSet<std::string> theirs;
  theirs.Add("a", "b");
  theirs.Add("c", "d");
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.Empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetIntersectionMineBeforeTheirs) {
  QuicIntervalSet<std::string> mine;
  mine.Add("a", "b");
  mine.Add("c", "d");
  QuicIntervalSet<std::string> theirs("y", "z");
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.Empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(QuicIntervalSetTest,
       QuicIntervalSetIntersectionTheirsBeforeMineInt64Singletons) {
  QuicIntervalSet<int64_t> mine({{10, 15}});
  QuicIntervalSet<int64_t> theirs({{-20, -5}});
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.Empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(QuicIntervalSetTest,
       QuicIntervalSetIntersectionMineBeforeTheirsIntSingletons) {
  QuicIntervalSet<int> mine({{10, 15}});
  QuicIntervalSet<int> theirs({{90, 95}});
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.Empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetIntersectionTheirsBetweenMine) {
  QuicIntervalSet<int64_t> mine({{0, 5}, {40, 50}});
  QuicIntervalSet<int64_t> theirs({{10, 15}});
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.Empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetIntersectionMineBetweenTheirs) {
  QuicIntervalSet<int> mine({{20, 25}});
  QuicIntervalSet<int> theirs({{10, 15}, {30, 32}});
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.Empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetIntersectionAlternatingIntervals) {
  QuicIntervalSet<int> mine, theirs;
  mine.Add(10, 20);
  mine.Add(40, 50);
  mine.Add(60, 70);
  theirs.Add(25, 39);
  theirs.Add(55, 59);
  theirs.Add(75, 79);
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.Empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(QuicIntervalSetTest,
       QuicIntervalSetIntersectionAdjacentAlternatingNonIntersectingIntervals) {
  // Make sure that intersection with adjacent interval set is empty.
  const QuicIntervalSet<int> x1({{0, 10}});
  const QuicIntervalSet<int> y1({{-50, 0}, {10, 95}});

  QuicIntervalSet<int> result1 = x1;
  result1.Intersection(y1);
  EXPECT_TRUE(result1.Empty()) << result1;

  const QuicIntervalSet<int16_t> x2({{0, 10}, {20, 30}, {40, 90}});
  const QuicIntervalSet<int16_t> y2(
      {{-50, -40}, {-2, 0}, {10, 20}, {32, 40}, {90, 95}});

  QuicIntervalSet<int16_t> result2 = x2;
  result2.Intersection(y2);
  EXPECT_TRUE(result2.Empty()) << result2;

  const QuicIntervalSet<int64_t> x3({{-1, 5}, {5, 10}});
  const QuicIntervalSet<int64_t> y3({{-10, -1}, {10, 95}});

  QuicIntervalSet<int64_t> result3 = x3;
  result3.Intersection(y3);
  EXPECT_TRUE(result3.Empty()) << result3;
}

TEST_F(QuicIntervalSetTest,
       QuicIntervalSetIntersectionAlternatingIntersectingIntervals) {
  const QuicIntervalSet<int> x1({{0, 10}});
  const QuicIntervalSet<int> y1({{-50, 1}, {9, 95}});
  const QuicIntervalSet<int> expected_result1({{0, 1}, {9, 10}});

  QuicIntervalSet<int> result1 = x1;
  result1.Intersection(y1);
  EXPECT_EQ(result1, expected_result1);

  const QuicIntervalSet<int16_t> x2({{0, 10}, {20, 30}, {40, 90}});
  const QuicIntervalSet<int16_t> y2(
      {{-50, -40}, {-2, 2}, {9, 21}, {32, 41}, {85, 95}});
  const QuicIntervalSet<int16_t> expected_result2(
      {{0, 2}, {9, 10}, {20, 21}, {40, 41}, {85, 90}});

  QuicIntervalSet<int16_t> result2 = x2;
  result2.Intersection(y2);
  EXPECT_EQ(result2, expected_result2);

  const QuicIntervalSet<int64_t> x3({{-1, 5}, {5, 10}});
  const QuicIntervalSet<int64_t> y3({{-10, 3}, {4, 95}});
  const QuicIntervalSet<int64_t> expected_result3({{-1, 3}, {4, 10}});

  QuicIntervalSet<int64_t> result3 = x3;
  result3.Intersection(y3);
  EXPECT_EQ(result3, expected_result3);
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetIntersectionIdentical) {
  QuicIntervalSet<int> copy(is);
  EXPECT_TRUE(copy.Intersects(is));
  EXPECT_TRUE(is.Intersects(copy));
  is.Intersection(copy);
  EXPECT_EQ(copy, is);
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetIntersectionSuperset) {
  QuicIntervalSet<int> mine(-1, 10000);
  EXPECT_TRUE(mine.Intersects(is));
  EXPECT_TRUE(is.Intersects(mine));
  mine.Intersection(is);
  EXPECT_EQ(is, mine);
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetIntersectionSubset) {
  QuicIntervalSet<int> copy(is);
  QuicIntervalSet<int> theirs(-1, 10000);
  EXPECT_TRUE(copy.Intersects(theirs));
  EXPECT_TRUE(theirs.Intersects(copy));
  is.Intersection(theirs);
  EXPECT_EQ(copy, is);
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetIntersectionLargeSet) {
  QuicIntervalSet<int> mine, theirs;
  // mine: [0, 9), [10, 19), ..., [990, 999)
  for (int i = 0; i < 1000; i += 10) {
    mine.Add(i, i + 9);
  }

  theirs.Add(500, 520);
  theirs.Add(535, 545);
  theirs.Add(801, 809);
  EXPECT_TRUE(mine.Intersects(theirs));
  EXPECT_TRUE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(Check(mine, 5, 500, 509, 510, 519, 535, 539, 540, 545, 801, 809));
  EXPECT_TRUE(mine.Intersects(theirs));
  EXPECT_TRUE(theirs.Intersects(mine));
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetDifference) {
  is.Difference(other);
  EXPECT_TRUE(Check(is, 10, 100, 200, 300, 350, 360, 370, 380, 400, 530, 600,
                    700, 770, 900, 1000, 1100, 1200, 1900, 2000, 2100, 2200));
  QuicIntervalSet<int> copy = is;
  is.Difference(copy);
  EXPECT_TRUE(is.Empty());
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetDifferenceSingleBounds) {
  std::vector<QuicInterval<int>> ivals(other.begin(), other.end());
  for (const QuicInterval<int>& ival : ivals) {
    is.Difference(ival.min(), ival.max());
  }
  EXPECT_TRUE(Check(is, 10, 100, 200, 300, 350, 360, 370, 380, 400, 530, 600,
                    700, 770, 900, 1000, 1100, 1200, 1900, 2000, 2100, 2200));
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetDifferenceSingleInterval) {
  std::vector<QuicInterval<int>> ivals(other.begin(), other.end());
  for (const QuicInterval<int>& ival : ivals) {
    is.Difference(ival);
  }
  EXPECT_TRUE(Check(is, 10, 100, 200, 300, 350, 360, 370, 380, 400, 530, 600,
                    700, 770, 900, 1000, 1100, 1200, 1900, 2000, 2100, 2200));
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetDifferenceAlternatingIntervals) {
  QuicIntervalSet<int> mine, theirs;
  mine.Add(10, 20);
  mine.Add(40, 50);
  mine.Add(60, 70);
  theirs.Add(25, 39);
  theirs.Add(55, 59);
  theirs.Add(75, 79);

  mine.Difference(theirs);
  EXPECT_TRUE(Check(mine, 3, 10, 20, 40, 50, 60, 70));
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetDifferenceEmptyMine) {
  QuicIntervalSet<std::string> mine, theirs;
  theirs.Add("a", "b");

  mine.Difference(theirs);
  EXPECT_TRUE(mine.Empty());
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetDifferenceEmptyTheirs) {
  QuicIntervalSet<std::string> mine, theirs;
  mine.Add("a", "b");

  mine.Difference(theirs);
  EXPECT_EQ(1u, mine.Size());
  EXPECT_EQ("a", mine.begin()->min());
  EXPECT_EQ("b", mine.begin()->max());
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetDifferenceTheirsBeforeMine) {
  QuicIntervalSet<std::string> mine, theirs;
  mine.Add("y", "z");
  theirs.Add("a", "b");

  mine.Difference(theirs);
  EXPECT_EQ(1u, mine.Size());
  EXPECT_EQ("y", mine.begin()->min());
  EXPECT_EQ("z", mine.begin()->max());
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetDifferenceMineBeforeTheirs) {
  QuicIntervalSet<std::string> mine, theirs;
  mine.Add("a", "b");
  theirs.Add("y", "z");

  mine.Difference(theirs);
  EXPECT_EQ(1u, mine.Size());
  EXPECT_EQ("a", mine.begin()->min());
  EXPECT_EQ("b", mine.begin()->max());
}

TEST_F(QuicIntervalSetTest, QuicIntervalSetDifferenceIdentical) {
  QuicIntervalSet<std::string> mine;
  mine.Add("a", "b");
  mine.Add("c", "d");
  QuicIntervalSet<std::string> theirs(mine);

  mine.Difference(theirs);
  EXPECT_TRUE(mine.Empty());
}

TEST_F(QuicIntervalSetTest, EmptyComplement) {
  // The complement of an empty set is the input interval:
  QuicIntervalSet<int> iset;
  iset.Complement(100, 200);
  EXPECT_TRUE(Check(iset, 1, 100, 200));
}

TEST(QuicIntervalSetMultipleCompactionTest, OuterCovering) {
  QuicIntervalSet<int> iset;
  // First add a bunch of disjoint ranges
  iset.Add(100, 150);
  iset.Add(200, 250);
  iset.Add(300, 350);
  iset.Add(400, 450);
  EXPECT_TRUE(Check(iset, 4, 100, 150, 200, 250, 300, 350, 400, 450));
  // Now add a big range that covers all of these ranges
  iset.Add(0, 500);
  EXPECT_TRUE(Check(iset, 1, 0, 500));
}

TEST(QuicIntervalSetMultipleCompactionTest, InnerCovering) {
  QuicIntervalSet<int> iset;
  // First add a bunch of disjoint ranges
  iset.Add(100, 150);
  iset.Add(200, 250);
  iset.Add(300, 350);
  iset.Add(400, 450);
  EXPECT_TRUE(Check(iset, 4, 100, 150, 200, 250, 300, 350, 400, 450));
  // Now add a big range that partially covers the left and right most ranges.
  iset.Add(125, 425);
  EXPECT_TRUE(Check(iset, 1, 100, 450));
}

TEST(QuicIntervalSetMultipleCompactionTest, LeftCovering) {
  QuicIntervalSet<int> iset;
  // First add a bunch of disjoint ranges
  iset.Add(100, 150);
  iset.Add(200, 250);
  iset.Add(300, 350);
  iset.Add(400, 450);
  EXPECT_TRUE(Check(iset, 4, 100, 150, 200, 250, 300, 350, 400, 450));
  // Now add a big range that partially covers the left most range.
  iset.Add(125, 500);
  EXPECT_TRUE(Check(iset, 1, 100, 500));
}

TEST(QuicIntervalSetMultipleCompactionTest, RightCovering) {
  QuicIntervalSet<int> iset;
  // First add a bunch of disjoint ranges
  iset.Add(100, 150);
  iset.Add(200, 250);
  iset.Add(300, 350);
  iset.Add(400, 450);
  EXPECT_TRUE(Check(iset, 4, 100, 150, 200, 250, 300, 350, 400, 450));
  // Now add a big range that partially covers the right most range.
  iset.Add(0, 425);
  EXPECT_TRUE(Check(iset, 1, 0, 450));
}

// Helper method for testing and verifying the results of a one-interval
// completement case.
static bool CheckOneComplement(int add_min,
                               int add_max,
                               int comp_min,
                               int comp_max,
                               int count,
                               ...) {
  QuicIntervalSet<int> iset;
  iset.Add(add_min, add_max);
  iset.Complement(comp_min, comp_max);
  bool result = true;
  va_list ap;
  va_start(ap, count);
  if (!VA_Check(iset, count, ap)) {
    result = false;
  }
  va_end(ap);
  return result;
}

TEST_F(QuicIntervalSetTest, SingleIntervalComplement) {
  // Verify the complement of a set with one interval (i):
  //                     |-----   i  -----|
  // |----- args -----|
  EXPECT_TRUE(CheckOneComplement(0, 10, 50, 150, 1, 50, 150));

  //          |-----   i  -----|
  //    |----- args -----|
  EXPECT_TRUE(CheckOneComplement(50, 150, 0, 100, 1, 0, 50));

  //    |-----   i  -----|
  //    |----- args -----|
  EXPECT_TRUE(CheckOneComplement(50, 150, 50, 150, 0));

  //    |----------   i  ----------|
  //        |----- args -----|
  EXPECT_TRUE(CheckOneComplement(50, 500, 100, 300, 0));

  //        |----- i -----|
  //    |---------- args  ----------|
  EXPECT_TRUE(CheckOneComplement(50, 500, 0, 800, 2, 0, 50, 500, 800));

  //    |-----   i  -----|
  //          |----- args -----|
  EXPECT_TRUE(CheckOneComplement(50, 150, 100, 300, 1, 150, 300));

  //    |-----   i  -----|
  //                        |----- args -----|
  EXPECT_TRUE(CheckOneComplement(50, 150, 200, 300, 1, 200, 300));
}

// Helper method that copies <iset> and takes its complement,
// returning false if Check succeeds.
static bool CheckComplement(const QuicIntervalSet<int>& iset,
                            int comp_min,
                            int comp_max,
                            int count,
                            ...) {
  QuicIntervalSet<int> iset_copy = iset;
  iset_copy.Complement(comp_min, comp_max);
  bool result = true;
  va_list ap;
  va_start(ap, count);
  if (!VA_Check(iset_copy, count, ap)) {
    result = false;
  }
  va_end(ap);
  return result;
}

TEST_F(QuicIntervalSetTest, MultiIntervalComplement) {
  // Initialize a small test set:
  QuicIntervalSet<int> iset;
  iset.Add(100, 200);
  iset.Add(300, 400);
  iset.Add(500, 600);

  //                     |-----   i  -----|
  // |----- comp -----|
  EXPECT_TRUE(CheckComplement(iset, 0, 50, 1, 0, 50));

  //          |-----   i  -----|
  //    |----- comp -----|
  EXPECT_TRUE(CheckComplement(iset, 0, 200, 1, 0, 100));
  EXPECT_TRUE(CheckComplement(iset, 0, 220, 2, 0, 100, 200, 220));

  //    |-----   i  -----|
  //    |----- comp -----|
  EXPECT_TRUE(CheckComplement(iset, 100, 600, 2, 200, 300, 400, 500));

  //    |----------   i  ----------|
  //        |----- comp -----|
  EXPECT_TRUE(CheckComplement(iset, 300, 400, 0));
  EXPECT_TRUE(CheckComplement(iset, 250, 400, 1, 250, 300));
  EXPECT_TRUE(CheckComplement(iset, 300, 450, 1, 400, 450));
  EXPECT_TRUE(CheckComplement(iset, 250, 450, 2, 250, 300, 400, 450));

  //        |----- i -----|
  //    |---------- comp  ----------|
  EXPECT_TRUE(
      CheckComplement(iset, 0, 700, 4, 0, 100, 200, 300, 400, 500, 600, 700));

  //    |-----   i  -----|
  //          |----- comp -----|
  EXPECT_TRUE(CheckComplement(iset, 400, 700, 2, 400, 500, 600, 700));
  EXPECT_TRUE(CheckComplement(iset, 350, 700, 2, 400, 500, 600, 700));

  //    |-----   i  -----|
  //                        |----- comp -----|
  EXPECT_TRUE(CheckComplement(iset, 700, 800, 1, 700, 800));
}

// Verifies ToString, operator<< don't assert.
TEST_F(QuicIntervalSetTest, ToString) {
  QuicIntervalSet<int> iset;
  iset.Add(300, 400);
  iset.Add(100, 200);
  iset.Add(500, 600);
  EXPECT_TRUE(!iset.ToString().empty());
  QUIC_VLOG(2) << iset;
  // Order and format of ToString() output is guaranteed.
  EXPECT_EQ("{ [100, 200) [300, 400) [500, 600) }", iset.ToString());
  EXPECT_EQ("{ [1, 2) }", QuicIntervalSet<int>(1, 2).ToString());
  EXPECT_EQ("{ }", QuicIntervalSet<int>().ToString());
}

TEST_F(QuicIntervalSetTest, ConstructionDiscardsEmptyInterval) {
  EXPECT_TRUE(QuicIntervalSet<int>(QuicInterval<int>(2, 2)).Empty());
  EXPECT_TRUE(QuicIntervalSet<int>(2, 2).Empty());
  EXPECT_FALSE(QuicIntervalSet<int>(QuicInterval<int>(2, 3)).Empty());
  EXPECT_FALSE(QuicIntervalSet<int>(2, 3).Empty());
}

TEST_F(QuicIntervalSetTest, Swap) {
  QuicIntervalSet<int> a, b;
  a.Add(300, 400);
  b.Add(100, 200);
  b.Add(500, 600);
  a.Swap(&b);
  EXPECT_TRUE(Check(a, 2, 100, 200, 500, 600));
  EXPECT_TRUE(Check(b, 1, 300, 400));
  swap(a, b);
  EXPECT_TRUE(Check(a, 1, 300, 400));
  EXPECT_TRUE(Check(b, 2, 100, 200, 500, 600));
}

TEST_F(QuicIntervalSetTest, OutputReturnsOstreamRef) {
  std::stringstream ss;
  const QuicIntervalSet<int> v(QuicInterval<int>(1, 2));
  auto return_type_is_a_ref = [](std::ostream&) {};
  return_type_is_a_ref(ss << v);
}

struct NotOstreamable {
  bool operator<(const NotOstreamable&) const { return false; }
  bool operator>(const NotOstreamable&) const { return false; }
  bool operator!=(const NotOstreamable&) const { return false; }
  bool operator>=(const NotOstreamable&) const { return true; }
  bool operator<=(const NotOstreamable&) const { return true; }
  bool operator==(const NotOstreamable&) const { return true; }
};

TEST_F(QuicIntervalSetTest, IntervalOfTypeWithNoOstreamSupport) {
  const NotOstreamable v;
  const QuicIntervalSet<NotOstreamable> d(QuicInterval<NotOstreamable>(v, v));
  // EXPECT_EQ builds a string representation of d. If d::operator<<()
  // would be defined then this test would not compile because NotOstreamable
  // objects lack the operator<<() support.
  EXPECT_EQ(d, d);
}

class QuicIntervalSetInitTest : public QuicTest {
 protected:
  const std::vector<QuicInterval<int>> intervals_{{0, 1}, {2, 4}};
};

TEST_F(QuicIntervalSetInitTest, DirectInit) {
  std::initializer_list<QuicInterval<int>> il = {{0, 1}, {2, 3}, {3, 4}};
  QuicIntervalSet<int> s(il);
  EXPECT_THAT(s, ElementsAreArray(intervals_));
}

TEST_F(QuicIntervalSetInitTest, CopyInit) {
  std::initializer_list<QuicInterval<int>> il = {{0, 1}, {2, 3}, {3, 4}};
  QuicIntervalSet<int> s = il;
  EXPECT_THAT(s, ElementsAreArray(intervals_));
}

TEST_F(QuicIntervalSetInitTest, AssignIterPair) {
  QuicIntervalSet<int> s(0, 1000);  // Make sure assign clears.
  s.assign(intervals_.begin(), intervals_.end());
  EXPECT_THAT(s, ElementsAreArray(intervals_));
}

TEST_F(QuicIntervalSetInitTest, AssignInitList) {
  QuicIntervalSet<int> s(0, 1000);  // Make sure assign clears.
  s.assign({{0, 1}, {2, 3}, {3, 4}});
  EXPECT_THAT(s, ElementsAreArray(intervals_));
}

TEST_F(QuicIntervalSetInitTest, AssignmentInitList) {
  std::initializer_list<QuicInterval<int>> il = {{0, 1}, {2, 3}, {3, 4}};
  QuicIntervalSet<int> s;
  s = il;
  EXPECT_THAT(s, ElementsAreArray(intervals_));
}

TEST_F(QuicIntervalSetInitTest, BracedInitThenBracedAssign) {
  QuicIntervalSet<int> s{{0, 1}, {2, 3}, {3, 4}};
  s = {{0, 1}, {2, 4}};
  EXPECT_THAT(s, ElementsAreArray(intervals_));
}

}  // namespace
}  // namespace test
}  // namespace quic

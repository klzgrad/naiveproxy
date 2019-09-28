// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_interval.h"

#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

template <typename ForwardIterator>
void STLDeleteContainerPointers(ForwardIterator begin, ForwardIterator end) {
  while (begin != end) {
    auto temp = begin;
    ++begin;
    delete *temp;
  }
}

template <typename T>
void STLDeleteElements(T* container) {
  if (!container)
    return;
  STLDeleteContainerPointers(container->begin(), container->end());
  container->clear();
}

class ConstructorListener {
 public:
  ConstructorListener(int* copy_construct_counter, int* move_construct_counter)
      : copy_construct_counter_(copy_construct_counter),
        move_construct_counter_(move_construct_counter) {
    *copy_construct_counter_ = 0;
    *move_construct_counter_ = 0;
  }
  ConstructorListener(const ConstructorListener& other) {
    copy_construct_counter_ = other.copy_construct_counter_;
    move_construct_counter_ = other.move_construct_counter_;
    ++*copy_construct_counter_;
  }
  ConstructorListener(ConstructorListener&& other) {
    copy_construct_counter_ = other.copy_construct_counter_;
    move_construct_counter_ = other.move_construct_counter_;
    ++*move_construct_counter_;
  }
  bool operator<(const ConstructorListener&) { return false; }
  bool operator>(const ConstructorListener&) { return false; }
  bool operator<=(const ConstructorListener&) { return true; }
  bool operator>=(const ConstructorListener&) { return true; }
  bool operator==(const ConstructorListener&) { return true; }

 private:
  int* copy_construct_counter_;
  int* move_construct_counter_;
};

TEST(QuicIntervalConstructorTest, Move) {
  int object1_copy_count, object1_move_count;
  ConstructorListener object1(&object1_copy_count, &object1_move_count);
  int object2_copy_count, object2_move_count;
  ConstructorListener object2(&object2_copy_count, &object2_move_count);

  QuicInterval<ConstructorListener> interval(object1, std::move(object2));
  EXPECT_EQ(1, object1_copy_count);
  EXPECT_EQ(0, object1_move_count);
  EXPECT_EQ(0, object2_copy_count);
  EXPECT_EQ(1, object2_move_count);
}

TEST(QuicIntervalConstructorTest, ImplicitConversion) {
  struct WrappedInt {
    WrappedInt(int value) : value(value) {}
    bool operator<(const WrappedInt& other) { return value < other.value; }
    bool operator>(const WrappedInt& other) { return value > other.value; }
    bool operator<=(const WrappedInt& other) { return value <= other.value; }
    bool operator>=(const WrappedInt& other) { return value >= other.value; }
    bool operator==(const WrappedInt& other) { return value == other.value; }
    int value;
  };

  static_assert(std::is_convertible<int, WrappedInt>::value, "");
  static_assert(
      std::is_constructible<QuicInterval<WrappedInt>, int, int>::value, "");

  QuicInterval<WrappedInt> i(10, 20);
  EXPECT_EQ(10, i.min().value);
  EXPECT_EQ(20, i.max().value);
}

class QuicIntervalTest : public QuicTest {
 protected:
  // Test intersection between the two intervals i1 and i2.  Tries
  // i1.IntersectWith(i2) and vice versa. The intersection should change i1 iff
  // changes_i1 is true, and the same for changes_i2.  The resulting
  // intersection should be result.
  void TestIntersect(const QuicInterval<int64_t>& i1,
                     const QuicInterval<int64_t>& i2,
                     bool changes_i1,
                     bool changes_i2,
                     const QuicInterval<int64_t>& result) {
    QuicInterval<int64_t> i;
    i = i1;
    EXPECT_TRUE(i.IntersectWith(i2) == changes_i1 && i == result);
    i = i2;
    EXPECT_TRUE(i.IntersectWith(i1) == changes_i2 && i == result);
  }
};

TEST_F(QuicIntervalTest, ConstructorsCopyAndClear) {
  QuicInterval<int32_t> empty;
  EXPECT_TRUE(empty.Empty());

  QuicInterval<int32_t> d2(0, 100);
  EXPECT_EQ(0, d2.min());
  EXPECT_EQ(100, d2.max());
  EXPECT_EQ(QuicInterval<int32_t>(0, 100), d2);
  EXPECT_NE(QuicInterval<int32_t>(0, 99), d2);

  empty = d2;
  EXPECT_EQ(0, d2.min());
  EXPECT_EQ(100, d2.max());
  EXPECT_TRUE(empty == d2);
  EXPECT_EQ(empty, d2);
  EXPECT_TRUE(d2 == empty);
  EXPECT_EQ(d2, empty);

  QuicInterval<int32_t> max_less_than_min(40, 20);
  EXPECT_TRUE(max_less_than_min.Empty());
  EXPECT_EQ(40, max_less_than_min.min());
  EXPECT_EQ(20, max_less_than_min.max());

  QuicInterval<int> d3(10, 20);
  d3.Clear();
  EXPECT_TRUE(d3.Empty());
}

TEST_F(QuicIntervalTest, MakeQuicInterval) {
  static_assert(
      std::is_same<QuicInterval<int>, decltype(MakeQuicInterval(0, 3))>::value,
      "Type is deduced incorrectly.");
  static_assert(std::is_same<QuicInterval<double>,
                             decltype(MakeQuicInterval(0., 3.))>::value,
                "Type is deduced incorrectly.");

  EXPECT_EQ(MakeQuicInterval(0., 3.), QuicInterval<double>(0, 3));
}

TEST_F(QuicIntervalTest, GettersSetters) {
  QuicInterval<int32_t> d1(100, 200);

  // SetMin:
  d1.SetMin(30);
  EXPECT_EQ(30, d1.min());
  EXPECT_EQ(200, d1.max());

  // SetMax:
  d1.SetMax(220);
  EXPECT_EQ(30, d1.min());
  EXPECT_EQ(220, d1.max());

  // Set:
  d1.Clear();
  d1.Set(30, 220);
  EXPECT_EQ(30, d1.min());
  EXPECT_EQ(220, d1.max());

  // SpanningUnion:
  QuicInterval<int32_t> d2;
  EXPECT_TRUE(!d1.SpanningUnion(d2));
  EXPECT_EQ(30, d1.min());
  EXPECT_EQ(220, d1.max());

  EXPECT_TRUE(d2.SpanningUnion(d1));
  EXPECT_EQ(30, d2.min());
  EXPECT_EQ(220, d2.max());

  d2.SetMin(40);
  d2.SetMax(100);
  EXPECT_TRUE(!d1.SpanningUnion(d2));
  EXPECT_EQ(30, d1.min());
  EXPECT_EQ(220, d1.max());

  d2.SetMin(20);
  d2.SetMax(100);
  EXPECT_TRUE(d1.SpanningUnion(d2));
  EXPECT_EQ(20, d1.min());
  EXPECT_EQ(220, d1.max());

  d2.SetMin(50);
  d2.SetMax(300);
  EXPECT_TRUE(d1.SpanningUnion(d2));
  EXPECT_EQ(20, d1.min());
  EXPECT_EQ(300, d1.max());

  d2.SetMin(0);
  d2.SetMax(500);
  EXPECT_TRUE(d1.SpanningUnion(d2));
  EXPECT_EQ(0, d1.min());
  EXPECT_EQ(500, d1.max());

  d2.SetMin(100);
  d2.SetMax(0);
  EXPECT_TRUE(!d1.SpanningUnion(d2));
  EXPECT_EQ(0, d1.min());
  EXPECT_EQ(500, d1.max());
  EXPECT_TRUE(d2.SpanningUnion(d1));
  EXPECT_EQ(0, d2.min());
  EXPECT_EQ(500, d2.max());
}

TEST_F(QuicIntervalTest, CoveringOps) {
  const QuicInterval<int64_t> empty;
  const QuicInterval<int64_t> d(100, 200);
  const QuicInterval<int64_t> d1(0, 50);
  const QuicInterval<int64_t> d2(50, 110);
  const QuicInterval<int64_t> d3(110, 180);
  const QuicInterval<int64_t> d4(180, 220);
  const QuicInterval<int64_t> d5(220, 300);
  const QuicInterval<int64_t> d6(100, 150);
  const QuicInterval<int64_t> d7(150, 200);
  const QuicInterval<int64_t> d8(0, 300);

  // Intersection:
  EXPECT_TRUE(d.Intersects(d));
  EXPECT_TRUE(!empty.Intersects(d) && !d.Intersects(empty));
  EXPECT_TRUE(!d.Intersects(d1) && !d1.Intersects(d));
  EXPECT_TRUE(d.Intersects(d2) && d2.Intersects(d));
  EXPECT_TRUE(d.Intersects(d3) && d3.Intersects(d));
  EXPECT_TRUE(d.Intersects(d4) && d4.Intersects(d));
  EXPECT_TRUE(!d.Intersects(d5) && !d5.Intersects(d));
  EXPECT_TRUE(d.Intersects(d6) && d6.Intersects(d));
  EXPECT_TRUE(d.Intersects(d7) && d7.Intersects(d));
  EXPECT_TRUE(d.Intersects(d8) && d8.Intersects(d));

  QuicInterval<int64_t> i;
  EXPECT_TRUE(d.Intersects(d, &i) && d == i);
  EXPECT_TRUE(!empty.Intersects(d, nullptr) && !d.Intersects(empty, nullptr));
  EXPECT_TRUE(!d.Intersects(d1, nullptr) && !d1.Intersects(d, nullptr));
  EXPECT_TRUE(d.Intersects(d2, &i) && i == QuicInterval<int64_t>(100, 110));
  EXPECT_TRUE(d2.Intersects(d, &i) && i == QuicInterval<int64_t>(100, 110));
  EXPECT_TRUE(d.Intersects(d3, &i) && i == d3);
  EXPECT_TRUE(d3.Intersects(d, &i) && i == d3);
  EXPECT_TRUE(d.Intersects(d4, &i) && i == QuicInterval<int64_t>(180, 200));
  EXPECT_TRUE(d4.Intersects(d, &i) && i == QuicInterval<int64_t>(180, 200));
  EXPECT_TRUE(!d.Intersects(d5, nullptr) && !d5.Intersects(d, nullptr));
  EXPECT_TRUE(d.Intersects(d6, &i) && i == d6);
  EXPECT_TRUE(d6.Intersects(d, &i) && i == d6);
  EXPECT_TRUE(d.Intersects(d7, &i) && i == d7);
  EXPECT_TRUE(d7.Intersects(d, &i) && i == d7);
  EXPECT_TRUE(d.Intersects(d8, &i) && i == d);
  EXPECT_TRUE(d8.Intersects(d, &i) && i == d);

  // Test IntersectsWith().
  // Arguments are TestIntersect(i1, i2, changes_i1, changes_i2, result).
  TestIntersect(empty, d, false, true, empty);
  TestIntersect(d, d1, true, true, empty);
  TestIntersect(d1, d2, true, true, empty);
  TestIntersect(d, d2, true, true, QuicInterval<int64_t>(100, 110));
  TestIntersect(d8, d, true, false, d);
  TestIntersect(d8, d1, true, false, d1);
  TestIntersect(d8, d5, true, false, d5);

  // Contains:
  EXPECT_TRUE(!empty.Contains(d) && !d.Contains(empty));
  EXPECT_TRUE(d.Contains(d));
  EXPECT_TRUE(!d.Contains(d1) && !d1.Contains(d));
  EXPECT_TRUE(!d.Contains(d2) && !d2.Contains(d));
  EXPECT_TRUE(d.Contains(d3) && !d3.Contains(d));
  EXPECT_TRUE(!d.Contains(d4) && !d4.Contains(d));
  EXPECT_TRUE(!d.Contains(d5) && !d5.Contains(d));
  EXPECT_TRUE(d.Contains(d6) && !d6.Contains(d));
  EXPECT_TRUE(d.Contains(d7) && !d7.Contains(d));
  EXPECT_TRUE(!d.Contains(d8) && d8.Contains(d));

  EXPECT_TRUE(d.Contains(100));
  EXPECT_TRUE(!d.Contains(200));
  EXPECT_TRUE(d.Contains(150));
  EXPECT_TRUE(!d.Contains(99));
  EXPECT_TRUE(!d.Contains(201));

  // Difference:
  std::vector<QuicInterval<int64_t>*> diff;

  EXPECT_TRUE(!d.Difference(empty, &diff));
  EXPECT_EQ(1u, diff.size());
  EXPECT_EQ(100, diff[0]->min());
  EXPECT_EQ(200, diff[0]->max());
  STLDeleteElements(&diff);
  EXPECT_TRUE(!empty.Difference(d, &diff) && diff.empty());

  EXPECT_TRUE(d.Difference(d, &diff) && diff.empty());
  EXPECT_TRUE(!d.Difference(d1, &diff));
  EXPECT_EQ(1u, diff.size());
  EXPECT_EQ(100, diff[0]->min());
  EXPECT_EQ(200, diff[0]->max());
  STLDeleteElements(&diff);

  QuicInterval<int64_t> lo;
  QuicInterval<int64_t> hi;

  EXPECT_TRUE(d.Difference(d2, &lo, &hi));
  EXPECT_TRUE(lo.Empty());
  EXPECT_EQ(110, hi.min());
  EXPECT_EQ(200, hi.max());
  EXPECT_TRUE(d.Difference(d2, &diff));
  EXPECT_EQ(1u, diff.size());
  EXPECT_EQ(110, diff[0]->min());
  EXPECT_EQ(200, diff[0]->max());
  STLDeleteElements(&diff);

  EXPECT_TRUE(d.Difference(d3, &lo, &hi));
  EXPECT_EQ(100, lo.min());
  EXPECT_EQ(110, lo.max());
  EXPECT_EQ(180, hi.min());
  EXPECT_EQ(200, hi.max());
  EXPECT_TRUE(d.Difference(d3, &diff));
  EXPECT_EQ(2u, diff.size());
  EXPECT_EQ(100, diff[0]->min());
  EXPECT_EQ(110, diff[0]->max());
  EXPECT_EQ(180, diff[1]->min());
  EXPECT_EQ(200, diff[1]->max());
  STLDeleteElements(&diff);

  EXPECT_TRUE(d.Difference(d4, &lo, &hi));
  EXPECT_EQ(100, lo.min());
  EXPECT_EQ(180, lo.max());
  EXPECT_TRUE(hi.Empty());
  EXPECT_TRUE(d.Difference(d4, &diff));
  EXPECT_EQ(1u, diff.size());
  EXPECT_EQ(100, diff[0]->min());
  EXPECT_EQ(180, diff[0]->max());
  STLDeleteElements(&diff);

  EXPECT_FALSE(d.Difference(d5, &lo, &hi));
  EXPECT_EQ(100, lo.min());
  EXPECT_EQ(200, lo.max());
  EXPECT_TRUE(hi.Empty());
  EXPECT_FALSE(d.Difference(d5, &diff));
  EXPECT_EQ(1u, diff.size());
  EXPECT_EQ(100, diff[0]->min());
  EXPECT_EQ(200, diff[0]->max());
  STLDeleteElements(&diff);

  EXPECT_TRUE(d.Difference(d6, &lo, &hi));
  EXPECT_TRUE(lo.Empty());
  EXPECT_EQ(150, hi.min());
  EXPECT_EQ(200, hi.max());
  EXPECT_TRUE(d.Difference(d6, &diff));
  EXPECT_EQ(1u, diff.size());
  EXPECT_EQ(150, diff[0]->min());
  EXPECT_EQ(200, diff[0]->max());
  STLDeleteElements(&diff);

  EXPECT_TRUE(d.Difference(d7, &lo, &hi));
  EXPECT_EQ(100, lo.min());
  EXPECT_EQ(150, lo.max());
  EXPECT_TRUE(hi.Empty());
  EXPECT_TRUE(d.Difference(d7, &diff));
  EXPECT_EQ(1u, diff.size());
  EXPECT_EQ(100, diff[0]->min());
  EXPECT_EQ(150, diff[0]->max());
  STLDeleteElements(&diff);

  EXPECT_TRUE(d.Difference(d8, &lo, &hi));
  EXPECT_TRUE(lo.Empty());
  EXPECT_TRUE(hi.Empty());
  EXPECT_TRUE(d.Difference(d8, &diff) && diff.empty());
}

TEST_F(QuicIntervalTest, Length) {
  const QuicInterval<int> empty1;
  const QuicInterval<int> empty2(1, 1);
  const QuicInterval<int> empty3(1, 0);
  const QuicInterval<QuicTime> empty4(
      QuicTime::Zero() + QuicTime::Delta::FromSeconds(1), QuicTime::Zero());
  const QuicInterval<int> d1(1, 2);
  const QuicInterval<int> d2(0, 50);
  const QuicInterval<QuicTime> d3(
      QuicTime::Zero(), QuicTime::Zero() + QuicTime::Delta::FromSeconds(1));
  const QuicInterval<QuicTime> d4(
      QuicTime::Zero() + QuicTime::Delta::FromSeconds(3600),
      QuicTime::Zero() + QuicTime::Delta::FromSeconds(5400));

  EXPECT_EQ(0, empty1.Length());
  EXPECT_EQ(0, empty2.Length());
  EXPECT_EQ(0, empty3.Length());
  EXPECT_EQ(QuicTime::Delta::Zero(), empty4.Length());
  EXPECT_EQ(1, d1.Length());
  EXPECT_EQ(50, d2.Length());
  EXPECT_EQ(QuicTime::Delta::FromSeconds(1), d3.Length());
  EXPECT_EQ(QuicTime::Delta::FromSeconds(1800), d4.Length());
}

TEST_F(QuicIntervalTest, IntervalOfTypeWithNoOperatorMinus) {
  // QuicInterval<T> should work even if T does not support operator-().  We
  // just can't call QuicInterval<T>::Length() for such types.
  const QuicInterval<std::string> d1("a", "b");
  const QuicInterval<std::pair<int, int>> d2({1, 2}, {4, 3});
  EXPECT_EQ("a", d1.min());
  EXPECT_EQ("b", d1.max());
  EXPECT_EQ(std::make_pair(1, 2), d2.min());
  EXPECT_EQ(std::make_pair(4, 3), d2.max());
}

struct NoEquals {
  NoEquals(int v) : value(v) {}  // NOLINT
  int value;
  bool operator<(const NoEquals& other) const { return value < other.value; }
};

TEST_F(QuicIntervalTest, OrderedComparisonForTypeWithoutEquals) {
  const QuicInterval<NoEquals> d1(0, 4);
  const QuicInterval<NoEquals> d2(0, 3);
  const QuicInterval<NoEquals> d3(1, 4);
  const QuicInterval<NoEquals> d4(1, 5);
  const QuicInterval<NoEquals> d6(0, 4);
  EXPECT_TRUE(d1 < d2);
  EXPECT_TRUE(d1 < d3);
  EXPECT_TRUE(d1 < d4);
  EXPECT_FALSE(d1 < d6);
}

TEST_F(QuicIntervalTest, OutputReturnsOstreamRef) {
  std::stringstream ss;
  const QuicInterval<int> v(1, 2);
  // If (ss << v) were to return a value, it wouldn't match the signature of
  // return_type_is_a_ref() function.
  auto return_type_is_a_ref = [](std::ostream&) {};
  return_type_is_a_ref(ss << v);
}

struct NotOstreamable {
  bool operator<(const NotOstreamable&) const { return false; }
  bool operator>=(const NotOstreamable&) const { return true; }
  bool operator==(const NotOstreamable&) const { return true; }
};

TEST_F(QuicIntervalTest, IntervalOfTypeWithNoOstreamSupport) {
  const NotOstreamable v;
  const QuicInterval<NotOstreamable> d(v, v);
  // EXPECT_EQ builds a string representation of d. If d::operator<<() would be
  // defined then this test would not compile because NotOstreamable objects
  // lack the operator<<() support.
  EXPECT_EQ(d, d);
}

}  // namespace
}  // namespace test
}  // namespace quic

// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_circular_deque.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

using testing::ElementsAre;

namespace quic {
namespace test {
namespace {

template <typename T, template <typename> class BaseAllocator = std::allocator>
class CountingAllocator : public BaseAllocator<T> {
  typedef BaseAllocator<T> BaseType;

 public:
  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;

  T* allocate(std::size_t n) {
    ++shared_counts_->allocate_count;
    return BaseType::allocate(n);
  }

  void deallocate(T* ptr, std::size_t n) {
    ++shared_counts_->deallocate_count;
    return BaseType::deallocate(ptr, n);
  }

  size_t allocate_count() const { return shared_counts_->allocate_count; }

  size_t deallocate_count() const { return shared_counts_->deallocate_count; }

  friend bool operator==(const CountingAllocator& lhs,
                         const CountingAllocator& rhs) {
    return lhs.shared_counts_ == rhs.shared_counts_;
  }

  friend bool operator!=(const CountingAllocator& lhs,
                         const CountingAllocator& rhs) {
    return !(lhs == rhs);
  }

 private:
  struct Counts {
    size_t allocate_count = 0;
    size_t deallocate_count = 0;
  };

  std::shared_ptr<Counts> shared_counts_ = std::make_shared<Counts>();
};

template <typename T,
          typename propagate_on_copy_assignment,
          typename propagate_on_move_assignment,
          typename propagate_on_swap,
          bool equality_result,
          template <typename> class BaseAllocator = std::allocator>
struct ConfigurableAllocator : public BaseAllocator<T> {
  using propagate_on_container_copy_assignment = propagate_on_copy_assignment;
  using propagate_on_container_move_assignment = propagate_on_move_assignment;
  using propagate_on_container_swap = propagate_on_swap;

  friend bool operator==(const ConfigurableAllocator& /*lhs*/,
                         const ConfigurableAllocator& /*rhs*/) {
    return equality_result;
  }

  friend bool operator!=(const ConfigurableAllocator& lhs,
                         const ConfigurableAllocator& rhs) {
    return !(lhs == rhs);
  }
};

// [1, 2, 3, 4] ==> [4, 1, 2, 3]
template <typename Deque>
void ShiftRight(Deque* dq, bool emplace) {
  auto back = *(&dq->back());
  dq->pop_back();
  if (emplace) {
    dq->emplace_front(back);
  } else {
    dq->push_front(back);
  }
}

// [1, 2, 3, 4] ==> [2, 3, 4, 1]
template <typename Deque>
void ShiftLeft(Deque* dq, bool emplace) {
  auto front = *(&dq->front());
  dq->pop_front();
  if (emplace) {
    dq->emplace_back(front);
  } else {
    dq->push_back(front);
  }
}

class QuicCircularDequeTest : public QuicTest {};

TEST_F(QuicCircularDequeTest, Empty) {
  QuicCircularDeque<int> dq;
  EXPECT_TRUE(dq.empty());
  EXPECT_EQ(0u, dq.size());
  dq.clear();
  dq.push_back(10);
  EXPECT_FALSE(dq.empty());
  EXPECT_EQ(1u, dq.size());
  EXPECT_EQ(10, dq.front());
  EXPECT_EQ(10, dq.back());
  dq.pop_front();
  EXPECT_TRUE(dq.empty());
  EXPECT_EQ(0u, dq.size());

  EXPECT_QUIC_DEBUG_DEATH(dq.front(), "");
  EXPECT_QUIC_DEBUG_DEATH(dq.back(), "");
  EXPECT_QUIC_DEBUG_DEATH(dq.at(0), "");
  EXPECT_QUIC_DEBUG_DEATH(dq[0], "");
}

TEST_F(QuicCircularDequeTest, Constructor) {
  QuicCircularDeque<int> dq;
  EXPECT_TRUE(dq.empty());

  std::allocator<int> alloc;
  QuicCircularDeque<int> dq1(alloc);
  EXPECT_TRUE(dq1.empty());

  QuicCircularDeque<int> dq2(8, 100, alloc);
  EXPECT_THAT(dq2, ElementsAre(100, 100, 100, 100, 100, 100, 100, 100));

  QuicCircularDeque<int> dq3(5, alloc);
  EXPECT_THAT(dq3, ElementsAre(0, 0, 0, 0, 0));

  QuicCircularDeque<int> dq4_rand_iter(dq3.begin(), dq3.end(), alloc);
  EXPECT_THAT(dq4_rand_iter, ElementsAre(0, 0, 0, 0, 0));
  EXPECT_EQ(dq4_rand_iter, dq3);

  std::list<int> dq4_src = {4, 4, 4, 4};
  QuicCircularDeque<int> dq4_bidi_iter(dq4_src.begin(), dq4_src.end());
  EXPECT_THAT(dq4_bidi_iter, ElementsAre(4, 4, 4, 4));

  QuicCircularDeque<int> dq5(dq4_bidi_iter);
  EXPECT_THAT(dq5, ElementsAre(4, 4, 4, 4));
  EXPECT_EQ(dq5, dq4_bidi_iter);

  QuicCircularDeque<int> dq6(dq5, alloc);
  EXPECT_THAT(dq6, ElementsAre(4, 4, 4, 4));
  EXPECT_EQ(dq6, dq5);

  QuicCircularDeque<int> dq7(std::move(*&dq6));
  EXPECT_THAT(dq7, ElementsAre(4, 4, 4, 4));
  EXPECT_TRUE(dq6.empty());

  QuicCircularDeque<int> dq8_equal_allocator(std::move(*&dq7), alloc);
  EXPECT_THAT(dq8_equal_allocator, ElementsAre(4, 4, 4, 4));
  EXPECT_TRUE(dq7.empty());

  QuicCircularDeque<int, 3, CountingAllocator<int>> dq8_temp = {5, 6, 7, 8, 9};
  QuicCircularDeque<int, 3, CountingAllocator<int>> dq8_unequal_allocator(
      std::move(*&dq8_temp), CountingAllocator<int>());
  EXPECT_THAT(dq8_unequal_allocator, ElementsAre(5, 6, 7, 8, 9));
  EXPECT_TRUE(dq8_temp.empty());

  QuicCircularDeque<int> dq9({3, 4, 5, 6, 7}, alloc);
  EXPECT_THAT(dq9, ElementsAre(3, 4, 5, 6, 7));
}

TEST_F(QuicCircularDequeTest, Assign) {
  // assign()
  QuicCircularDeque<int, 3, CountingAllocator<int>> dq;
  dq.assign(7, 1);
  EXPECT_THAT(dq, ElementsAre(1, 1, 1, 1, 1, 1, 1));
  EXPECT_EQ(1u, dq.get_allocator().allocate_count());

  QuicCircularDeque<int, 3, CountingAllocator<int>> dq2;
  dq2.assign(dq.begin(), dq.end());
  EXPECT_THAT(dq2, ElementsAre(1, 1, 1, 1, 1, 1, 1));
  EXPECT_EQ(1u, dq2.get_allocator().allocate_count());
  EXPECT_TRUE(std::equal(dq.begin(), dq.end(), dq2.begin(), dq2.end()));

  dq2.assign({2, 2, 2, 2, 2, 2});
  EXPECT_THAT(dq2, ElementsAre(2, 2, 2, 2, 2, 2));

  // Assign from a non random access iterator.
  std::list<int> dq3_src = {3, 3, 3, 3, 3};
  QuicCircularDeque<int, 3, CountingAllocator<int>> dq3;
  dq3.assign(dq3_src.begin(), dq3_src.end());
  EXPECT_THAT(dq3, ElementsAre(3, 3, 3, 3, 3));
  EXPECT_LT(1u, dq3.get_allocator().allocate_count());

  // Copy assignment
  dq3 = *&dq3;
  EXPECT_THAT(dq3, ElementsAre(3, 3, 3, 3, 3));

  QuicCircularDeque<
      int, 3,
      ConfigurableAllocator<int,
                            /*propagate_on_copy_assignment=*/std::true_type,
                            /*propagate_on_move_assignment=*/std::true_type,
                            /*propagate_on_swap=*/std::true_type,
                            /*equality_result=*/false>>
      dq4, dq5;
  dq4.assign(dq3.begin(), dq3.end());
  dq5 = dq4;
  EXPECT_THAT(dq5, ElementsAre(3, 3, 3, 3, 3));

  QuicCircularDeque<
      int, 3,
      ConfigurableAllocator<int,
                            /*propagate_on_copy_assignment=*/std::false_type,
                            /*propagate_on_move_assignment=*/std::true_type,
                            /*propagate_on_swap=*/std::true_type,
                            /*equality_result=*/true>>
      dq6, dq7;
  dq6.assign(dq3.begin(), dq3.end());
  dq7 = dq6;
  EXPECT_THAT(dq7, ElementsAre(3, 3, 3, 3, 3));

  // Move assignment
  dq3 = std::move(*&dq3);
  EXPECT_THAT(dq3, ElementsAre(3, 3, 3, 3, 3));

  ASSERT_TRUE(decltype(
      dq3.get_allocator())::propagate_on_container_move_assignment::value);
  decltype(dq3) dq8;
  dq8 = std::move(*&dq3);
  EXPECT_THAT(dq8, ElementsAre(3, 3, 3, 3, 3));
  EXPECT_TRUE(dq3.empty());

  QuicCircularDeque<
      int, 3,
      ConfigurableAllocator<int,
                            /*propagate_on_copy_assignment=*/std::true_type,
                            /*propagate_on_move_assignment=*/std::false_type,
                            /*propagate_on_swap=*/std::true_type,
                            /*equality_result=*/true>>
      dq9, dq10;
  dq9.assign(dq8.begin(), dq8.end());
  dq10.assign(dq2.begin(), dq2.end());
  dq9 = std::move(*&dq10);
  EXPECT_THAT(dq9, ElementsAre(2, 2, 2, 2, 2, 2));
  EXPECT_TRUE(dq10.empty());

  QuicCircularDeque<
      int, 3,
      ConfigurableAllocator<int,
                            /*propagate_on_copy_assignment=*/std::true_type,
                            /*propagate_on_move_assignment=*/std::false_type,
                            /*propagate_on_swap=*/std::true_type,
                            /*equality_result=*/false>>
      dq11, dq12;
  dq11.assign(dq8.begin(), dq8.end());
  dq12.assign(dq2.begin(), dq2.end());
  dq11 = std::move(*&dq12);
  EXPECT_THAT(dq11, ElementsAre(2, 2, 2, 2, 2, 2));
  EXPECT_TRUE(dq12.empty());
}

TEST_F(QuicCircularDequeTest, Access) {
  // at()
  // operator[]
  // front()
  // back()

  QuicCircularDeque<int, 3, CountingAllocator<int>> dq;
  dq.push_back(10);
  EXPECT_EQ(dq.front(), 10);
  EXPECT_EQ(dq.back(), 10);
  EXPECT_EQ(dq.at(0), 10);
  EXPECT_EQ(dq[0], 10);
  dq.front() = 12;
  EXPECT_EQ(dq.front(), 12);
  EXPECT_EQ(dq.back(), 12);
  EXPECT_EQ(dq.at(0), 12);
  EXPECT_EQ(dq[0], 12);

  const auto& dqref = dq;
  EXPECT_EQ(dqref.front(), 12);
  EXPECT_EQ(dqref.back(), 12);
  EXPECT_EQ(dqref.at(0), 12);
  EXPECT_EQ(dqref[0], 12);

  dq.pop_front();
  EXPECT_TRUE(dqref.empty());

  // Push to capacity.
  dq.push_back(15);
  dq.push_front(5);
  dq.push_back(25);
  EXPECT_EQ(dq.size(), dq.capacity());
  EXPECT_THAT(dq, ElementsAre(5, 15, 25));
  EXPECT_LT(&dq.front(), &dq.back());
  EXPECT_EQ(dq.front(), 5);
  EXPECT_EQ(dq.back(), 25);
  EXPECT_EQ(dq.at(0), 5);
  EXPECT_EQ(dq.at(1), 15);
  EXPECT_EQ(dq.at(2), 25);
  EXPECT_EQ(dq[0], 5);
  EXPECT_EQ(dq[1], 15);
  EXPECT_EQ(dq[2], 25);

  // Shift right such that begin=1 and end=0. Data is still not wrapped.
  dq.pop_front();
  dq.push_back(35);
  EXPECT_THAT(dq, ElementsAre(15, 25, 35));
  EXPECT_LT(&dq.front(), &dq.back());
  EXPECT_EQ(dq.front(), 15);
  EXPECT_EQ(dq.back(), 35);
  EXPECT_EQ(dq.at(0), 15);
  EXPECT_EQ(dq.at(1), 25);
  EXPECT_EQ(dq.at(2), 35);
  EXPECT_EQ(dq[0], 15);
  EXPECT_EQ(dq[1], 25);
  EXPECT_EQ(dq[2], 35);

  // Shift right such that data is wrapped.
  dq.pop_front();
  dq.push_back(45);
  EXPECT_THAT(dq, ElementsAre(25, 35, 45));
  EXPECT_GT(&dq.front(), &dq.back());
  EXPECT_EQ(dq.front(), 25);
  EXPECT_EQ(dq.back(), 45);
  EXPECT_EQ(dq.at(0), 25);
  EXPECT_EQ(dq.at(1), 35);
  EXPECT_EQ(dq.at(2), 45);
  EXPECT_EQ(dq[0], 25);
  EXPECT_EQ(dq[1], 35);
  EXPECT_EQ(dq[2], 45);

  // Shift right again, data is still wrapped.
  dq.pop_front();
  dq.push_back(55);
  EXPECT_THAT(dq, ElementsAre(35, 45, 55));
  EXPECT_GT(&dq.front(), &dq.back());
  EXPECT_EQ(dq.front(), 35);
  EXPECT_EQ(dq.back(), 55);
  EXPECT_EQ(dq.at(0), 35);
  EXPECT_EQ(dq.at(1), 45);
  EXPECT_EQ(dq.at(2), 55);
  EXPECT_EQ(dq[0], 35);
  EXPECT_EQ(dq[1], 45);
  EXPECT_EQ(dq[2], 55);

  // Shift right one last time. begin returns to 0. Data is no longer wrapped.
  dq.pop_front();
  dq.push_back(65);
  EXPECT_THAT(dq, ElementsAre(45, 55, 65));
  EXPECT_LT(&dq.front(), &dq.back());
  EXPECT_EQ(dq.front(), 45);
  EXPECT_EQ(dq.back(), 65);
  EXPECT_EQ(dq.at(0), 45);
  EXPECT_EQ(dq.at(1), 55);
  EXPECT_EQ(dq.at(2), 65);
  EXPECT_EQ(dq[0], 45);
  EXPECT_EQ(dq[1], 55);
  EXPECT_EQ(dq[2], 65);

  EXPECT_EQ(1u, dq.get_allocator().allocate_count());
}

TEST_F(QuicCircularDequeTest, Iterate) {
  QuicCircularDeque<int> dq;
  EXPECT_EQ(dq.begin(), dq.end());
  EXPECT_EQ(dq.cbegin(), dq.cend());
  EXPECT_EQ(dq.rbegin(), dq.rend());
  EXPECT_EQ(dq.crbegin(), dq.crend());

  dq.emplace_back(2);
  QuicCircularDeque<int>::const_iterator citer = dq.begin();
  EXPECT_NE(citer, dq.end());
  EXPECT_EQ(*citer, 2);
  ++citer;
  EXPECT_EQ(citer, dq.end());

  EXPECT_EQ(*dq.begin(), 2);
  EXPECT_EQ(*dq.cbegin(), 2);
  EXPECT_EQ(*dq.rbegin(), 2);
  EXPECT_EQ(*dq.crbegin(), 2);

  dq.emplace_front(1);
  QuicCircularDeque<int>::const_reverse_iterator criter = dq.rbegin();
  EXPECT_NE(criter, dq.rend());
  EXPECT_EQ(*criter, 2);
  ++criter;
  EXPECT_NE(criter, dq.rend());
  EXPECT_EQ(*criter, 1);
  ++criter;
  EXPECT_EQ(criter, dq.rend());

  EXPECT_EQ(*dq.begin(), 1);
  EXPECT_EQ(*dq.cbegin(), 1);
  EXPECT_EQ(*dq.rbegin(), 2);
  EXPECT_EQ(*dq.crbegin(), 2);

  dq.push_back(3);

  // Forward iterate.
  int expected_value = 1;
  for (QuicCircularDeque<int>::iterator it = dq.begin(); it != dq.end(); ++it) {
    EXPECT_EQ(expected_value++, *it);
  }

  expected_value = 1;
  for (QuicCircularDeque<int>::const_iterator it = dq.cbegin(); it != dq.cend();
       ++it) {
    EXPECT_EQ(expected_value++, *it);
  }

  // Reverse iterate.
  expected_value = 3;
  for (QuicCircularDeque<int>::reverse_iterator it = dq.rbegin();
       it != dq.rend(); ++it) {
    EXPECT_EQ(expected_value--, *it);
  }

  expected_value = 3;
  for (QuicCircularDeque<int>::const_reverse_iterator it = dq.crbegin();
       it != dq.crend(); ++it) {
    EXPECT_EQ(expected_value--, *it);
  }
}

TEST_F(QuicCircularDequeTest, Iterator) {
  // Default constructed iterators of the same type compare equal.
  EXPECT_EQ(QuicCircularDeque<int>::iterator(),
            QuicCircularDeque<int>::iterator());
  EXPECT_EQ(QuicCircularDeque<int>::const_iterator(),
            QuicCircularDeque<int>::const_iterator());
  EXPECT_EQ(QuicCircularDeque<int>::reverse_iterator(),
            QuicCircularDeque<int>::reverse_iterator());
  EXPECT_EQ(QuicCircularDeque<int>::const_reverse_iterator(),
            QuicCircularDeque<int>::const_reverse_iterator());

  QuicCircularDeque<QuicCircularDeque<int>, 3> dqdq = {
      {1, 2}, {10, 20, 30}, {100, 200, 300, 400}};

  // iter points to {1, 2}
  decltype(dqdq)::iterator iter = dqdq.begin();
  EXPECT_EQ(iter->size(), 2u);
  EXPECT_THAT(*iter, ElementsAre(1, 2));

  // citer points to {10, 20, 30}
  decltype(dqdq)::const_iterator citer = dqdq.cbegin() + 1;
  EXPECT_NE(*iter, *citer);
  EXPECT_EQ(citer->size(), 3u);
  int x = 10;
  for (auto it = citer->begin(); it != citer->end(); ++it) {
    EXPECT_EQ(*it, x);
    x += 10;
  }

  EXPECT_LT(iter, citer);
  EXPECT_LE(iter, iter);
  EXPECT_GT(citer, iter);
  EXPECT_GE(citer, citer);

  // iter points to {100, 200, 300, 400}
  iter += 2;
  EXPECT_NE(*iter, *citer);
  EXPECT_EQ(iter->size(), 4u);
  for (int i = 1; i <= 4; ++i) {
    EXPECT_EQ(iter->begin()[i - 1], i * 100);
  }

  EXPECT_LT(citer, iter);
  EXPECT_LE(iter, iter);
  EXPECT_GT(iter, citer);
  EXPECT_GE(citer, citer);

  // iter points to {10, 20, 30}. (same as citer)
  iter -= 1;
  EXPECT_EQ(*iter, *citer);
  EXPECT_EQ(iter->size(), 3u);
  x = 10;
  for (auto it = iter->begin(); it != iter->end();) {
    EXPECT_EQ(*(it++), x);
    x += 10;
  }
  x = 30;
  for (auto it = iter->begin() + 2; it != iter->begin();) {
    EXPECT_EQ(*(it--), x);
    x -= 10;
  }
}

TEST_F(QuicCircularDequeTest, Resize) {
  QuicCircularDeque<int, 3, CountingAllocator<int>> dq;
  dq.resize(8);
  EXPECT_THAT(dq, ElementsAre(0, 0, 0, 0, 0, 0, 0, 0));
  EXPECT_EQ(1u, dq.get_allocator().allocate_count());

  dq.resize(10, 5);
  EXPECT_THAT(dq, ElementsAre(0, 0, 0, 0, 0, 0, 0, 0, 5, 5));

  QuicCircularDeque<int, 3, CountingAllocator<int>> dq2 = dq;

  for (size_t new_size = dq.size(); new_size != 0; --new_size) {
    dq.resize(new_size);
    EXPECT_TRUE(
        std::equal(dq.begin(), dq.end(), dq2.begin(), dq2.begin() + new_size));
  }

  dq.resize(0);
  EXPECT_TRUE(dq.empty());

  // Resize when data is wrapped.
  ASSERT_EQ(dq2.size(), dq2.capacity());
  while (dq2.size() < dq2.capacity()) {
    dq2.push_back(5);
  }

  // Shift left once such that data is wrapped.
  ASSERT_LT(&dq2.front(), &dq2.back());
  dq2.pop_back();
  dq2.push_front(-5);
  ASSERT_GT(&dq2.front(), &dq2.back());

  EXPECT_EQ(-5, dq2.front());
  EXPECT_EQ(5, dq2.back());
  dq2.resize(dq2.size() + 1, 10);

  // Data should be unwrapped after the resize.
  ASSERT_LT(&dq2.front(), &dq2.back());
  EXPECT_EQ(-5, dq2.front());
  EXPECT_EQ(10, dq2.back());
  EXPECT_EQ(5, *(dq2.rbegin() + 1));
}

namespace {
class Foo {
 public:
  Foo() : Foo(0xF00) {}

  explicit Foo(int i) : i_(new int(i)) {}

  ~Foo() {
    if (i_ != nullptr) {
      delete i_;
      // Do not set i_ to nullptr such that if the container calls destructor
      // multiple times, asan can detect it.
    }
  }

  Foo(const Foo& other) : i_(new int(*other.i_)) {}

  Foo(Foo&& other) = delete;

  void Set(int i) { *i_ = i; }

  int i() const { return *i_; }

  friend bool operator==(const Foo& lhs, const Foo& rhs) {
    return lhs.i() == rhs.i();
  }

  friend std::ostream& operator<<(std::ostream& os, const Foo& foo) {
    return os << "Foo(" << foo.i() << ")";
  }

 private:
  // By pointing i_ to a dynamically allocated integer, a memory leak will be
  // reported if the container forget to properly destruct this object.
  int* i_ = nullptr;
};
}  // namespace

TEST_F(QuicCircularDequeTest, RelocateNonTriviallyCopyable) {
  // When relocating non-trivially-copyable objects:
  // - Move constructor is preferred, if available.
  // - Copy constructor is used otherwise.

  {
    // Move construct in Relocate.
    typedef std::unique_ptr<Foo> MoveConstructible;
    ASSERT_FALSE(std::is_trivially_copyable<MoveConstructible>::value);
    ASSERT_TRUE(std::is_move_constructible<MoveConstructible>::value);
    QuicCircularDeque<MoveConstructible, 3,
                      CountingAllocator<MoveConstructible>>
        dq1;
    dq1.resize(3);
    EXPECT_EQ(dq1.size(), dq1.capacity());
    EXPECT_EQ(1u, dq1.get_allocator().allocate_count());

    dq1.emplace_back(new Foo(0xF1));  // Cause existing elements to relocate.
    EXPECT_EQ(4u, dq1.size());
    EXPECT_EQ(2u, dq1.get_allocator().allocate_count());
    EXPECT_EQ(dq1[0], nullptr);
    EXPECT_EQ(dq1[1], nullptr);
    EXPECT_EQ(dq1[2], nullptr);
    EXPECT_EQ(dq1[3]->i(), 0xF1);
  }

  {
    // Copy construct in Relocate.
    typedef Foo NonMoveConstructible;
    ASSERT_FALSE(std::is_trivially_copyable<NonMoveConstructible>::value);
    ASSERT_FALSE(std::is_move_constructible<NonMoveConstructible>::value);
    QuicCircularDeque<NonMoveConstructible, 3,
                      CountingAllocator<NonMoveConstructible>>
        dq2;
    dq2.resize(3);
    EXPECT_EQ(dq2.size(), dq2.capacity());
    EXPECT_EQ(1u, dq2.get_allocator().allocate_count());

    dq2.emplace_back(0xF1);  // Cause existing elements to relocate.
    EXPECT_EQ(4u, dq2.size());
    EXPECT_EQ(2u, dq2.get_allocator().allocate_count());
    EXPECT_EQ(dq2[0].i(), 0xF00);
    EXPECT_EQ(dq2[1].i(), 0xF00);
    EXPECT_EQ(dq2[2].i(), 0xF00);
    EXPECT_EQ(dq2[3].i(), 0xF1);
  }
}

TEST_F(QuicCircularDequeTest, PushPop) {
  // (push|pop|emplace)_(back|front)

  {
    QuicCircularDeque<Foo, 4, CountingAllocator<Foo>> dq(4);
    for (size_t i = 0; i < dq.size(); ++i) {
      dq[i].Set(i + 1);
    }
    QUIC_LOG(INFO) << "dq initialized to " << dq;
    EXPECT_THAT(dq, ElementsAre(Foo(1), Foo(2), Foo(3), Foo(4)));

    ShiftLeft(&dq, false);
    QUIC_LOG(INFO) << "shift left once : " << dq;
    EXPECT_THAT(dq, ElementsAre(Foo(2), Foo(3), Foo(4), Foo(1)));

    ShiftLeft(&dq, true);
    QUIC_LOG(INFO) << "shift left twice: " << dq;
    EXPECT_THAT(dq, ElementsAre(Foo(3), Foo(4), Foo(1), Foo(2)));
    ASSERT_GT(&dq.front(), &dq.back());
    // dq destructs with wrapped data.
  }

  {
    QuicCircularDeque<Foo, 4, CountingAllocator<Foo>> dq1(4);
    for (size_t i = 0; i < dq1.size(); ++i) {
      dq1[i].Set(i + 1);
    }
    QUIC_LOG(INFO) << "dq1 initialized to " << dq1;
    EXPECT_THAT(dq1, ElementsAre(Foo(1), Foo(2), Foo(3), Foo(4)));

    ShiftRight(&dq1, false);
    QUIC_LOG(INFO) << "shift right once : " << dq1;
    EXPECT_THAT(dq1, ElementsAre(Foo(4), Foo(1), Foo(2), Foo(3)));

    ShiftRight(&dq1, true);
    QUIC_LOG(INFO) << "shift right twice: " << dq1;
    EXPECT_THAT(dq1, ElementsAre(Foo(3), Foo(4), Foo(1), Foo(2)));
    ASSERT_GT(&dq1.front(), &dq1.back());
    // dq1 destructs with wrapped data.
  }

  {  // Pop n elements from front.
    QuicCircularDeque<Foo, 4, CountingAllocator<Foo>> dq2(5);
    for (size_t i = 0; i < dq2.size(); ++i) {
      dq2[i].Set(i + 1);
    }
    EXPECT_THAT(dq2, ElementsAre(Foo(1), Foo(2), Foo(3), Foo(4), Foo(5)));

    EXPECT_EQ(2u, dq2.pop_front_n(2));
    EXPECT_THAT(dq2, ElementsAre(Foo(3), Foo(4), Foo(5)));

    EXPECT_EQ(3u, dq2.pop_front_n(100));
    EXPECT_TRUE(dq2.empty());
  }

  {  // Pop n elements from back.
    QuicCircularDeque<Foo, 4, CountingAllocator<Foo>> dq3(6);
    for (size_t i = 0; i < dq3.size(); ++i) {
      dq3[i].Set(i + 1);
    }
    EXPECT_THAT(dq3,
                ElementsAre(Foo(1), Foo(2), Foo(3), Foo(4), Foo(5), Foo(6)));

    ShiftRight(&dq3, true);
    ShiftRight(&dq3, true);
    ShiftRight(&dq3, true);
    EXPECT_THAT(dq3,
                ElementsAre(Foo(4), Foo(5), Foo(6), Foo(1), Foo(2), Foo(3)));

    EXPECT_EQ(2u, dq3.pop_back_n(2));
    EXPECT_THAT(dq3, ElementsAre(Foo(4), Foo(5), Foo(6), Foo(1)));

    EXPECT_EQ(2u, dq3.pop_back_n(2));
    EXPECT_THAT(dq3, ElementsAre(Foo(4), Foo(5)));
  }
}

TEST_F(QuicCircularDequeTest, Allocation) {
  CountingAllocator<int> alloc;

  {
    QuicCircularDeque<int, 3, CountingAllocator<int>> dq(alloc);
    EXPECT_EQ(alloc, dq.get_allocator());
    EXPECT_EQ(0u, dq.size());
    EXPECT_EQ(0u, dq.capacity());
    EXPECT_EQ(0u, alloc.allocate_count());
    EXPECT_EQ(0u, alloc.deallocate_count());

    for (int i = 1; i <= 18; ++i) {
      SCOPED_TRACE(testing::Message()
                   << "i=" << i << ", capacity_b4_push=" << dq.capacity());
      dq.push_back(i);
      EXPECT_EQ(i, static_cast<int>(dq.size()));

      const size_t capacity = 3 + (i - 1) / 3 * 3;
      EXPECT_EQ(capacity, dq.capacity());
      EXPECT_EQ(capacity / 3, alloc.allocate_count());
      EXPECT_EQ(capacity / 3 - 1, alloc.deallocate_count());
    }

    dq.push_back(19);
    EXPECT_EQ(22u, dq.capacity());  // 18 + 18 / 4
    EXPECT_EQ(7u, alloc.allocate_count());
    EXPECT_EQ(6u, alloc.deallocate_count());
  }

  EXPECT_EQ(7u, alloc.deallocate_count());
}

}  // namespace
}  // namespace test
}  // namespace quic

// Use a non-quic namespace to make sure swap can be used via ADL.
namespace {

template <typename T>
using SwappableAllocator = quic::test::ConfigurableAllocator<
    T,
    /*propagate_on_copy_assignment=*/std::true_type,
    /*propagate_on_move_assignment=*/std::true_type,
    /*propagate_on_swap=*/std::true_type,
    /*equality_result=*/true>;

template <typename T>
using UnswappableEqualAllocator = quic::test::ConfigurableAllocator<
    T,
    /*propagate_on_copy_assignment=*/std::true_type,
    /*propagate_on_move_assignment=*/std::true_type,
    /*propagate_on_swap=*/std::false_type,
    /*equality_result=*/true>;

template <typename T>
using UnswappableUnequalAllocator = quic::test::ConfigurableAllocator<
    T,
    /*propagate_on_copy_assignment=*/std::true_type,
    /*propagate_on_move_assignment=*/std::true_type,
    /*propagate_on_swap=*/std::false_type,
    /*equality_result=*/false>;

using quic::test::QuicCircularDequeTest;

TEST_F(QuicCircularDequeTest, Swap) {
  using std::swap;

  quic::QuicCircularDeque<int64_t, 3, SwappableAllocator<int64_t>> dq1, dq2;
  dq1.push_back(10);
  dq1.push_back(11);
  dq2.push_back(20);
  swap(dq1, dq2);
  EXPECT_THAT(dq1, ElementsAre(20));
  EXPECT_THAT(dq2, ElementsAre(10, 11));

  quic::QuicCircularDeque<char, 3, UnswappableEqualAllocator<char>> dq3, dq4;
  dq3 = {1, 2, 3, 4, 5};
  dq4 = {6, 7, 8, 9, 0};
  swap(dq3, dq4);
  EXPECT_THAT(dq3, ElementsAre(6, 7, 8, 9, 0));
  EXPECT_THAT(dq4, ElementsAre(1, 2, 3, 4, 5));

  quic::QuicCircularDeque<int, 3, UnswappableUnequalAllocator<int>> dq5, dq6;
  dq6.push_front(4);

  // Using UnswappableUnequalAllocator is ok as long as swap is not called.
  dq5.assign(dq6.begin(), dq6.end());
  EXPECT_THAT(dq5, ElementsAre(4));

  // Undefined behavior to swap between two containers with unequal allocators.
  EXPECT_QUIC_DEBUG_DEATH(swap(dq5, dq6), "Undefined swap behavior");
}
}  // namespace

// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CIRCULAR_DEQUE_H_
#define QUICHE_QUIC_CORE_QUIC_CIRCULAR_DEQUE_H_

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <ostream>
#include <type_traits>

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

// QuicCircularDeque is a STL-style container that is similar to std deque in
// API and std::vector in capacity management. The goal is to optimize a common
// QUIC use case where we keep adding new elements to the end and removing old
// elements from the beginning, under such scenarios, if the container's size()
// remain relatively stable, QuicCircularDeque requires little to no memory
// allocations or deallocations.
//
// The implementation, as the name suggests, uses a flat circular buffer to hold
// all elements. At any point in time, either
// a) All elements are placed in a contiguous portion of this buffer, like a
//    c-array, or
// b) Elements are phycially divided into two parts: the first part occupies the
//    end of the buffer and the second part occupies the beginning of the
//    buffer.
//
// Currently, elements can only be pushed or poped from either ends, it can't be
// inserted or erased in the middle.
//
// TODO(wub): Make memory grow/shrink strategies customizable.
template <typename T,
          size_t MinCapacityIncrement = 3,
          typename Allocator = std::allocator<T>>
class QUIC_NO_EXPORT QuicCircularDeque {
  using AllocatorTraits = std::allocator_traits<Allocator>;

  // Pointee is either T or const T.
  template <typename Pointee>
  class QUIC_NO_EXPORT basic_iterator {
    using size_type = typename AllocatorTraits::size_type;

   public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = typename AllocatorTraits::value_type;
    using difference_type = typename AllocatorTraits::difference_type;
    using pointer = Pointee*;
    using reference = Pointee&;

    basic_iterator() = default;

    // A copy constructor if Pointee is T.
    // A conversion from iterator to const_iterator if Pointee is const T.
    basic_iterator(
        const basic_iterator<value_type>& it)  // NOLINT(runtime/explicit)
        : deque_(it.deque_), index_(it.index_) {}

    reference operator*() const { return *deque_->index_to_address(index_); }
    pointer operator->() const { return deque_->index_to_address(index_); }
    reference operator[](difference_type i) { return *(*this + i); }

    basic_iterator& operator++() {
      Increment();
      return *this;
    }

    basic_iterator operator++(int) {
      basic_iterator result = *this;
      Increment();
      return result;
    }

    basic_iterator operator--() {
      Decrement();
      return *this;
    }

    basic_iterator operator--(int) {
      basic_iterator result = *this;
      Decrement();
      return result;
    }

    friend basic_iterator operator+(const basic_iterator& it,
                                    difference_type delta) {
      basic_iterator result = it;
      result.IncrementBy(delta);
      return result;
    }

    basic_iterator& operator+=(difference_type delta) {
      IncrementBy(delta);
      return *this;
    }

    friend basic_iterator operator-(const basic_iterator& it,
                                    difference_type delta) {
      basic_iterator result = it;
      result.IncrementBy(-delta);
      return result;
    }

    basic_iterator& operator-=(difference_type delta) {
      IncrementBy(-delta);
      return *this;
    }

    friend difference_type operator-(const basic_iterator& lhs,
                                     const basic_iterator& rhs) {
      return lhs.ExternalPosition() - rhs.ExternalPosition();
    }

    friend bool operator==(const basic_iterator& lhs,
                           const basic_iterator& rhs) {
      return lhs.index_ == rhs.index_;
    }

    friend bool operator!=(const basic_iterator& lhs,
                           const basic_iterator& rhs) {
      return !(lhs == rhs);
    }

    friend bool operator<(const basic_iterator& lhs,
                          const basic_iterator& rhs) {
      return lhs.ExternalPosition() < rhs.ExternalPosition();
    }

    friend bool operator<=(const basic_iterator& lhs,
                           const basic_iterator& rhs) {
      return !(lhs > rhs);
    }

    friend bool operator>(const basic_iterator& lhs,
                          const basic_iterator& rhs) {
      return lhs.ExternalPosition() > rhs.ExternalPosition();
    }

    friend bool operator>=(const basic_iterator& lhs,
                           const basic_iterator& rhs) {
      return !(lhs < rhs);
    }

   private:
    basic_iterator(const QuicCircularDeque* deque, size_type index)
        : deque_(deque), index_(index) {}

    void Increment() {
      DCHECK_LE(ExternalPosition() + 1, deque_->size());
      index_ = deque_->index_next(index_);
    }

    void Decrement() {
      DCHECK_GE(ExternalPosition(), 1u);
      index_ = deque_->index_prev(index_);
    }

    void IncrementBy(difference_type delta) {
      if (delta >= 0) {
        // After increment we are before or at end().
        DCHECK_LE(static_cast<size_type>(ExternalPosition() + delta),
                  deque_->size());
      } else {
        // After decrement we are after or at begin().
        DCHECK_GE(ExternalPosition(), static_cast<size_type>(-delta));
      }
      index_ = deque_->index_increment_by(index_, delta);
    }

    size_type ExternalPosition() const {
      if (index_ >= deque_->begin_) {
        return index_ - deque_->begin_;
      }
      return index_ + deque_->data_capacity() - deque_->begin_;
    }

    friend class QuicCircularDeque;
    const QuicCircularDeque* deque_ = nullptr;
    size_type index_ = 0;
  };

 public:
  using allocator_type = typename AllocatorTraits::allocator_type;
  using value_type = typename AllocatorTraits::value_type;
  using size_type = typename AllocatorTraits::size_type;
  using difference_type = typename AllocatorTraits::difference_type;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = typename AllocatorTraits::pointer;
  using const_pointer = typename AllocatorTraits::const_pointer;
  using iterator = basic_iterator<T>;
  using const_iterator = basic_iterator<const T>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  QuicCircularDeque() : QuicCircularDeque(allocator_type()) {}
  explicit QuicCircularDeque(const allocator_type& alloc)
      : allocator_and_data_(alloc) {}

  QuicCircularDeque(size_type count,
                    const T& value,
                    const Allocator& alloc = allocator_type())
      : allocator_and_data_(alloc) {
    resize(count, value);
  }

  explicit QuicCircularDeque(size_type count,
                             const Allocator& alloc = allocator_type())
      : allocator_and_data_(alloc) {
    resize(count);
  }

  template <
      class InputIt,
      typename = std::enable_if_t<std::is_base_of<
          std::input_iterator_tag,
          typename std::iterator_traits<InputIt>::iterator_category>::value>>
  QuicCircularDeque(InputIt first,
                    InputIt last,
                    const Allocator& alloc = allocator_type())
      : allocator_and_data_(alloc) {
    AssignRange(first, last);
  }

  QuicCircularDeque(const QuicCircularDeque& other)
      : QuicCircularDeque(
            other,
            AllocatorTraits::select_on_container_copy_construction(
                other.allocator_and_data_.allocator())) {}

  QuicCircularDeque(const QuicCircularDeque& other, const allocator_type& alloc)
      : allocator_and_data_(alloc) {
    assign(other.begin(), other.end());
  }

  QuicCircularDeque(QuicCircularDeque&& other)
      : begin_(other.begin_),
        end_(other.end_),
        allocator_and_data_(std::move(other.allocator_and_data_)) {
    other.begin_ = other.end_ = 0;
    other.allocator_and_data_.data = nullptr;
    other.allocator_and_data_.data_capacity = 0;
  }

  QuicCircularDeque(QuicCircularDeque&& other, const allocator_type& alloc)
      : allocator_and_data_(alloc) {
    MoveRetainAllocator(std::move(other));
  }

  QuicCircularDeque(std::initializer_list<T> init,
                    const allocator_type& alloc = allocator_type())
      : QuicCircularDeque(init.begin(), init.end(), alloc) {}

  QuicCircularDeque& operator=(const QuicCircularDeque& other) {
    if (this == &other) {
      return *this;
    }
    if (AllocatorTraits::propagate_on_container_copy_assignment::value &&
        (allocator_and_data_.allocator() !=
         other.allocator_and_data_.allocator())) {
      // Destroy all current elements and blocks with the current allocator,
      // before switching this to use the allocator propagated from "other".
      DestroyAndDeallocateAll();
      begin_ = end_ = 0;
      allocator_and_data_ =
          AllocatorAndData(other.allocator_and_data_.allocator());
    }
    assign(other.begin(), other.end());
    return *this;
  }

  QuicCircularDeque& operator=(QuicCircularDeque&& other) {
    if (this == &other) {
      return *this;
    }
    if (AllocatorTraits::propagate_on_container_move_assignment::value) {
      // Take over the storage of "other", along with its allocator.
      this->~QuicCircularDeque();
      new (this) QuicCircularDeque(std::move(other));
    } else {
      MoveRetainAllocator(std::move(other));
    }
    return *this;
  }

  ~QuicCircularDeque() { DestroyAndDeallocateAll(); }

  void assign(size_type count, const T& value) {
    ClearRetainCapacity();
    reserve(count);
    for (size_t i = 0; i < count; ++i) {
      emplace_back(value);
    }
  }

  template <
      class InputIt,
      typename = std::enable_if_t<std::is_base_of<
          std::input_iterator_tag,
          typename std::iterator_traits<InputIt>::iterator_category>::value>>
  void assign(InputIt first, InputIt last) {
    AssignRange(first, last);
  }

  void assign(std::initializer_list<T> ilist) {
    assign(ilist.begin(), ilist.end());
  }

  reference at(size_type pos) {
    DCHECK(pos < size()) << "pos:" << pos << ", size():" << size();
    size_type index = begin_ + pos;
    if (index < data_capacity()) {
      return *index_to_address(index);
    }
    return *index_to_address(index - data_capacity());
  }

  const_reference at(size_type pos) const {
    return const_cast<QuicCircularDeque*>(this)->at(pos);
  }

  reference operator[](size_type pos) { return at(pos); }

  const_reference operator[](size_type pos) const { return at(pos); }

  reference front() {
    DCHECK(!empty());
    return *index_to_address(begin_);
  }

  const_reference front() const {
    return const_cast<QuicCircularDeque*>(this)->front();
  }

  reference back() {
    DCHECK(!empty());
    return *(index_to_address(end_ == 0 ? data_capacity() - 1 : end_ - 1));
  }

  const_reference back() const {
    return const_cast<QuicCircularDeque*>(this)->back();
  }

  iterator begin() { return iterator(this, begin_); }
  const_iterator begin() const { return const_iterator(this, begin_); }
  const_iterator cbegin() const { return const_iterator(this, begin_); }

  iterator end() { return iterator(this, end_); }
  const_iterator end() const { return const_iterator(this, end_); }
  const_iterator cend() const { return const_iterator(this, end_); }

  reverse_iterator rbegin() { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }
  const_reverse_iterator crbegin() const { return rbegin(); }

  reverse_iterator rend() { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }
  const_reverse_iterator crend() const { return rend(); }

  size_type capacity() const {
    return data_capacity() == 0 ? 0 : data_capacity() - 1;
  }

  void reserve(size_type new_cap) {
    if (new_cap > capacity()) {
      Relocate(new_cap);
    }
  }

  // Remove all elements. Leave capacity unchanged.
  void clear() { ClearRetainCapacity(); }

  bool empty() const { return begin_ == end_; }

  size_type size() const {
    if (begin_ <= end_) {
      return end_ - begin_;
    }
    return data_capacity() + end_ - begin_;
  }

  void resize(size_type count) { ResizeInternal(count); }

  void resize(size_type count, const value_type& value) {
    ResizeInternal(count, value);
  }

  void push_front(const T& value) { emplace_front(value); }
  void push_front(T&& value) { emplace_front(std::move(value)); }

  template <class... Args>
  reference emplace_front(Args&&... args) {
    MaybeExpandCapacity(1);
    begin_ = index_prev(begin_);
    new (index_to_address(begin_)) T(std::forward<Args>(args)...);
    return front();
  }

  void push_back(const T& value) { emplace_back(value); }
  void push_back(T&& value) { emplace_back(std::move(value)); }

  template <class... Args>
  reference emplace_back(Args&&... args) {
    MaybeExpandCapacity(1);
    new (index_to_address(end_)) T(std::forward<Args>(args)...);
    end_ = index_next(end_);
    return back();
  }

  void pop_front() {
    DCHECK(!empty());
    DestroyByIndex(begin_);
    begin_ = index_next(begin_);
    MaybeShrinkCapacity();
  }

  size_type pop_front_n(size_type count) {
    size_type num_elements_to_pop = std::min(count, size());
    size_type new_begin = index_increment_by(begin_, num_elements_to_pop);
    DestroyRange(begin_, new_begin);
    begin_ = new_begin;
    MaybeShrinkCapacity();
    return num_elements_to_pop;
  }

  void pop_back() {
    DCHECK(!empty());
    end_ = index_prev(end_);
    DestroyByIndex(end_);
    MaybeShrinkCapacity();
  }

  size_type pop_back_n(size_type count) {
    size_type num_elements_to_pop = std::min(count, size());
    size_type new_end = index_increment_by(end_, -num_elements_to_pop);
    DestroyRange(new_end, end_);
    end_ = new_end;
    MaybeShrinkCapacity();
    return num_elements_to_pop;
  }

  void swap(QuicCircularDeque& other) {
    using std::swap;
    swap(begin_, other.begin_);
    swap(end_, other.end_);

    if (AllocatorTraits::propagate_on_container_swap::value) {
      swap(allocator_and_data_, other.allocator_and_data_);
    } else {
      // When propagate_on_container_swap is false, it is undefined behavior, by
      // c++ standard, to swap between two AllocatorAwareContainer(s) with
      // unequal allocators.
      DCHECK(get_allocator() == other.get_allocator())
          << "Undefined swap behavior";
      swap(allocator_and_data_.data, other.allocator_and_data_.data);
      swap(allocator_and_data_.data_capacity,
           other.allocator_and_data_.data_capacity);
    }
  }

  friend void swap(QuicCircularDeque& lhs, QuicCircularDeque& rhs) {
    lhs.swap(rhs);
  }

  allocator_type get_allocator() const {
    return allocator_and_data_.allocator();
  }

  friend bool operator==(const QuicCircularDeque& lhs,
                         const QuicCircularDeque& rhs) {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
  }

  friend bool operator!=(const QuicCircularDeque& lhs,
                         const QuicCircularDeque& rhs) {
    return !(lhs == rhs);
  }

  friend QUIC_NO_EXPORT std::ostream& operator<<(std::ostream& os,
                                                 const QuicCircularDeque& dq) {
    os << "{";
    for (size_type pos = 0; pos != dq.size(); ++pos) {
      if (pos != 0) {
        os << ",";
      }
      os << " " << dq[pos];
    }
    os << " }";
    return os;
  }

 private:
  void MoveRetainAllocator(QuicCircularDeque&& other) {
    if (get_allocator() == other.get_allocator()) {
      // Take over the storage of "other", with which we share an allocator.
      DestroyAndDeallocateAll();

      begin_ = other.begin_;
      end_ = other.end_;
      allocator_and_data_.data = other.allocator_and_data_.data;
      allocator_and_data_.data_capacity =
          other.allocator_and_data_.data_capacity;

      other.begin_ = other.end_ = 0;
      other.allocator_and_data_.data = nullptr;
      other.allocator_and_data_.data_capacity = 0;
    } else {
      // We cannot take over of the storage from "other", since it has a
      // different allocator; we're stuck move-assigning elements individually.
      ClearRetainCapacity();
      for (auto& elem : other) {
        push_back(std::move(elem));
      }
      other.clear();
    }
  }

  template <
      typename InputIt,
      typename = std::enable_if_t<std::is_base_of<
          std::input_iterator_tag,
          typename std::iterator_traits<InputIt>::iterator_category>::value>>
  void AssignRange(InputIt first, InputIt last) {
    ClearRetainCapacity();
    if (std::is_base_of<
            std::random_access_iterator_tag,
            typename std::iterator_traits<InputIt>::iterator_category>::value) {
      reserve(std::distance(first, last));
    }
    for (; first != last; ++first) {
      emplace_back(*first);
    }
  }

  // WARNING: begin_, end_ and allocator_and_data_ are not modified.
  void DestroyAndDeallocateAll() {
    DestroyRange(begin_, end_);

    if (data_capacity() > 0) {
      DCHECK_NE(nullptr, allocator_and_data_.data);
      AllocatorTraits::deallocate(allocator_and_data_.allocator(),
                                  allocator_and_data_.data, data_capacity());
    }
  }

  void ClearRetainCapacity() {
    DestroyRange(begin_, end_);
    begin_ = end_ = 0;
  }

  void MaybeShrinkCapacity() {
    // TODO(wub): Implement a storage policy that actually shrinks.
  }

  void MaybeExpandCapacity(size_t num_additional_elements) {
    size_t new_size = size() + num_additional_elements;
    if (capacity() >= new_size) {
      return;
    }

    // The minimum amount of additional capacity to grow.
    size_t min_additional_capacity =
        std::max(MinCapacityIncrement, capacity() / 4);
    size_t new_capacity =
        std::max(new_size, capacity() + min_additional_capacity);

    Relocate(new_capacity);
  }

  void Relocate(size_t new_capacity) {
    const size_t num_elements = size();
    DCHECK_GT(new_capacity, num_elements)
        << "new_capacity:" << new_capacity << ", num_elements:" << num_elements;

    size_t new_data_capacity = new_capacity + 1;
    pointer new_data = AllocatorTraits::allocate(
        allocator_and_data_.allocator(), new_data_capacity);

    if (begin_ < end_) {
      // Not wrapped.
      RelocateUnwrappedRange(begin_, end_, new_data);
    } else if (begin_ > end_) {
      // Wrapped.
      const size_t num_elements_before_wrap = data_capacity() - begin_;
      RelocateUnwrappedRange(begin_, data_capacity(), new_data);
      RelocateUnwrappedRange(0, end_, new_data + num_elements_before_wrap);
    }

    if (data_capacity()) {
      AllocatorTraits::deallocate(allocator_and_data_.allocator(),
                                  allocator_and_data_.data, data_capacity());
    }

    allocator_and_data_.data = new_data;
    allocator_and_data_.data_capacity = new_data_capacity;
    begin_ = 0;
    end_ = num_elements;
  }

  template <typename T_ = T>
  typename std::enable_if<std::is_trivially_copyable<T_>::value, void>::type
  RelocateUnwrappedRange(size_type begin, size_type end, pointer dest) const {
    DCHECK_LE(begin, end) << "begin:" << begin << ", end:" << end;
    pointer src = index_to_address(begin);
    DCHECK_NE(src, nullptr);
    memcpy(dest, src, sizeof(T) * (end - begin));
    DestroyRange(begin, end);
  }

  template <typename T_ = T>
  typename std::enable_if<!std::is_trivially_copyable<T_>::value &&
                              std::is_move_constructible<T_>::value,
                          void>::type
  RelocateUnwrappedRange(size_type begin, size_type end, pointer dest) const {
    DCHECK_LE(begin, end) << "begin:" << begin << ", end:" << end;
    pointer src = index_to_address(begin);
    pointer src_end = index_to_address(end);
    while (src != src_end) {
      new (dest) T(std::move(*src));
      DestroyByAddress(src);
      ++dest;
      ++src;
    }
  }

  template <typename T_ = T>
  typename std::enable_if<!std::is_trivially_copyable<T_>::value &&
                              !std::is_move_constructible<T_>::value,
                          void>::type
  RelocateUnwrappedRange(size_type begin, size_type end, pointer dest) const {
    DCHECK_LE(begin, end) << "begin:" << begin << ", end:" << end;
    pointer src = index_to_address(begin);
    pointer src_end = index_to_address(end);
    while (src != src_end) {
      new (dest) T(*src);
      DestroyByAddress(src);
      ++dest;
      ++src;
    }
  }

  template <class... U>
  void ResizeInternal(size_type count, U&&... u) {
    if (count > size()) {
      // Expanding.
      MaybeExpandCapacity(count - size());
      while (size() < count) {
        emplace_back(std::forward<U>(u)...);
      }
    } else {
      // Most likely shrinking. No-op if count == size().
      size_type new_end = (begin_ + count) % data_capacity();
      DestroyRange(new_end, end_);
      end_ = new_end;

      MaybeShrinkCapacity();
    }
  }

  void DestroyRange(size_type begin, size_type end) const {
    if (std::is_trivially_destructible<T>::value) {
      return;
    }
    if (end >= begin) {
      DestroyUnwrappedRange(begin, end);
    } else {
      DestroyUnwrappedRange(begin, data_capacity());
      DestroyUnwrappedRange(0, end);
    }
  }

  // Should only be called from DestroyRange.
  void DestroyUnwrappedRange(size_type begin, size_type end) const {
    DCHECK_LE(begin, end) << "begin:" << begin << ", end:" << end;
    for (; begin != end; ++begin) {
      DestroyByIndex(begin);
    }
  }

  void DestroyByIndex(size_type index) const {
    DestroyByAddress(index_to_address(index));
  }

  void DestroyByAddress(pointer address) const {
    if (std::is_trivially_destructible<T>::value) {
      return;
    }
    address->~T();
  }

  size_type data_capacity() const { return allocator_and_data_.data_capacity; }

  pointer index_to_address(size_type index) const {
    return allocator_and_data_.data + index;
  }

  size_type index_prev(size_type index) const {
    return index == 0 ? data_capacity() - 1 : index - 1;
  }

  size_type index_next(size_type index) const {
    return index == data_capacity() - 1 ? 0 : index + 1;
  }

  size_type index_increment_by(size_type index, difference_type delta) const {
    if (delta == 0) {
      return index;
    }

    DCHECK_LT(static_cast<size_type>(std::abs(delta)), data_capacity());
    return (index + data_capacity() + delta) % data_capacity();
  }

  // Empty base-class optimization: bundle storage for our allocator together
  // with the fields we had to store anyway, via inheriting from the allocator,
  // so this allocator instance doesn't consume any storage when its type has no
  // data members.
  struct AllocatorAndData : private allocator_type {
    explicit AllocatorAndData(const allocator_type& alloc)
        : allocator_type(alloc) {}

    const allocator_type& allocator() const { return *this; }
    allocator_type& allocator() { return *this; }

    pointer data = nullptr;
    size_type data_capacity = 0;
  };

  size_type begin_ = 0;
  size_type end_ = 0;
  AllocatorAndData allocator_and_data_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CIRCULAR_DEQUE_H_

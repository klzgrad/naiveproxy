// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_STRING_TUPLE_H_
#define QUICHE_COMMON_QUICHE_STRING_TUPLE_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/inlined_vector.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/vectorized_io_utils.h"

namespace quiche {

// Flexible container for storing tuples of potentially small strings.  Allows
// the maximum byte size of the string to be specified at the compile-time; the
// specified size can be used to optimize the data layout of the container.
//
// Note that the limit in question is the limit on the number of string bytes;
// as empty elements are allowed, the number of tuple elements can be
// potentially unlimited.
template <size_t MaxDataSize = std::numeric_limits<size_t>::max(),
          size_t InlinedSizes = 4>
class QUICHE_NO_EXPORT QuicheStringTuple {
 private:
  // Use the information about the maximum size of the tuple to pick the
  // smallest possible offset type.
  using OffsetT = std::conditional_t<
      MaxDataSize <= 0xffff, uint16_t,
      std::conditional_t<MaxDataSize <= 0xffffffff, uint32_t, uint64_t> >;

  // Basic read-only proxy iterator over the string tuple.  Invalidated on any
  // removal of elements from the tuple.
  class TupleIterator {
   public:
    using value_type = absl::string_view;
    using difference_type = std::ptrdiff_t;
    using reference = value_type&;
    using iterator_category = std::input_iterator_tag;

    TupleIterator(const TupleIterator&) = default;
    TupleIterator(TupleIterator&&) = default;
    TupleIterator& operator=(const TupleIterator&) = default;
    TupleIterator& operator=(TupleIterator&&) = default;

    absl::string_view operator*() const { return (*tuple_)[offset_]; }

    TupleIterator& operator++() {
      ++offset_;
      return *this;
    }
    TupleIterator operator++(int) {
      ++offset_;
      return TupleIterator(tuple_, offset_ - 1);
    }
    TupleIterator& operator--() {
      --offset_;
      return *this;
    }
    TupleIterator operator--(int) {
      --offset_;
      return TupleIterator(tuple_, offset_ + 1);
    }

    bool operator==(const TupleIterator&) const = default;
    bool operator!=(const TupleIterator&) const = default;

   private:
    friend class QuicheStringTuple;

    TupleIterator(const QuicheStringTuple* tuple, size_t offset)
        : tuple_(tuple), offset_(offset) {}

    const QuicheStringTuple* tuple_;
    OffsetT offset_;
  };

 public:
  using value_type = absl::string_view;
  using reference = value_type&;
  using const_reference = const value_type&;
  using iterator = TupleIterator;
  using const_iterator = TupleIterator;

  static constexpr size_t kMaxDataSize = MaxDataSize;

  QuicheStringTuple() = default;
  QuicheStringTuple(const QuicheStringTuple&) noexcept = default;
  QuicheStringTuple(QuicheStringTuple&&) noexcept = default;
  QuicheStringTuple& operator=(const QuicheStringTuple&) noexcept = default;
  QuicheStringTuple& operator=(QuicheStringTuple&&) noexcept = default;

  // Convenience constructor meant for constants. Will QUIC_BUG if adding any of
  // the elements fails.
  explicit QuicheStringTuple(
      std::initializer_list<const absl::string_view> items) {
    ReserveTupleElements(items.size());
    for (absl::string_view item : items) {
      bool success = Add(item);
      QUICHE_BUG_IF(QuicheStringTuple_initializer_list_too_large, !success)
          << "Attempted to use initializer list constructor for "
             "QuicheStringTuple with a string that exceeds the specified "
             "maximum size";
    }
  }

  // Adds a new string to the end of the tuple. Returns false if this operation
  // results in the size limit being exceeded.
  [[nodiscard]] bool Add(absl::string_view element) {
    if (element.size() + data_.size() > kMaxDataSize) {
      return false;
    }
    AddUnchecked(element);
    return true;
  }

  // Appends another tuple to the end of this one.  Returns false if this
  // operation results in the size limit being exceeded.
  [[nodiscard]] bool Append(const QuicheStringTuple& suffix) {
    if (data_.size() + suffix.data_.size() > kMaxDataSize) {
      return false;
    }
    OffsetT current_offset = data_.size();
    elements_start_offsets_.reserve(elements_start_offsets_.size() +
                                    suffix.elements_start_offsets_.size());
    for (const absl::string_view element : suffix) {
      elements_start_offsets_.push_back(current_offset);
      current_offset += element.size();
    }
    data_.append(suffix.data_);
    return true;
  }

  // Appends a span of string_views to the end of the tuple.  Returns false if
  // this operation results in the size limit being exceeded.
  [[nodiscard]] bool Append(absl::Span<const absl::string_view> span) {
    size_t total_size = TotalStringViewSpanSize(span);
    if (data_.size() + total_size > kMaxDataSize) {
      return false;
    }
    elements_start_offsets_.reserve(elements_start_offsets_.size() +
                                    span.size());
    data_.reserve(data_.size() + total_size);
    for (absl::string_view element : span) {
      AddUnchecked(element);
    }
    return true;
  }

  // If `prefix` is a prefix of this current tuple, the prefix is removed.
  [[nodiscard]] bool ConsumePrefix(const QuicheStringTuple& prefix) {
    if (!IsPrefix(prefix)) {
      return false;
    }

    data_ = data_.substr(prefix.data_.size());
    const OffsetT prefix_offset = prefix.data_.size();
    const size_t prefix_element_count = prefix.elements_start_offsets_.size();
    for (size_t i = prefix_element_count; i < elements_start_offsets_.size();
         ++i) {
      elements_start_offsets_[i - prefix_element_count] =
          elements_start_offsets_[i] - prefix_offset;
    }
    elements_start_offsets_.resize(elements_start_offsets_.size() -
                                   prefix_element_count);
    return true;
  }

  // Removes an element from the end of the tuple, if there are any.
  bool Pop() {
    if (empty()) {
      return false;
    }
    data_.resize(elements_start_offsets_.back());
    elements_start_offsets_.pop_back();
    return true;
  }

  // Removes all elements from the tuple.
  void Clear() {
    data_.clear();
    elements_start_offsets_.clear();
  }

  // Returns the string at the offset `i`, or nullopt if `i` is out of bounds.
  std::optional<absl::string_view> ValueAt(size_t i) const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    if (i >= elements_start_offsets_.size()) {
      return std::nullopt;
    }
    if (i == elements_start_offsets_.size() - 1) {
      return absl::string_view(data_).substr(elements_start_offsets_[i]);
    }
    const size_t start = elements_start_offsets_[i];
    const size_t end = elements_start_offsets_[i + 1];
    return absl::string_view(data_).substr(start, end - start);
  }

  // Returns the string at the offset `i`; QUICHE_BUGs if `i` is out of bounds
  // and returns an empty string.
  absl::string_view operator[](size_t i) const {
    auto result = ValueAt(i);
    if (!result.has_value()) {
      QUICHE_BUG(QuicheStringTuple_oob_access)
          << "Tried to access an out-of-bounds element #" << i
          << " for a string tuple of size " << size();
      return absl::string_view();
    }
    return *result;
  }

  // Allows pre-allocating memory if the total byte size of the tuple and the
  // number of elements in it are known in advance.
  void ReserveDataBytes(size_t n) { data_.reserve(n); }
  void ReserveTupleElements(size_t n) { elements_start_offsets_.reserve(n); }

  // Returns true if the specified tuple is a prefix of (or is equal to) this
  // tuple.
  bool IsPrefix(const QuicheStringTuple& other) const {
    return other.size() <= size() &&
           std::equal(iterator(this, 0), iterator(this, other.size()),
                      other.begin());
  }

  bool empty() const { return elements_start_offsets_.empty(); }
  size_t size() const { return elements_start_offsets_.size(); }
  size_t TotalBytes() const { return data_.size(); }
  size_t BytesLeft() const { return kMaxDataSize - data_.size(); }

  absl::string_view front() const { return (*this)[0]; }
  absl::string_view back() const { return (*this)[size() - 1]; }

  iterator begin() const { return iterator(this, 0); }
  iterator end() const { return iterator(this, size()); }

  // Tuples are lexicographical ordered.
  auto operator<=>(const QuicheStringTuple& other) const {
    return std::lexicographical_compare_three_way(begin(), end(), other.begin(),
                                                  other.end());
  }
  bool operator==(const QuicheStringTuple& other) const = default;

  template <typename H>
  friend H AbslHashValue(H h, const QuicheStringTuple& tuple) {
    return H::combine(std::move(h), tuple.data_, tuple.elements_start_offsets_);
  }

  // String tuple pretty-printer primarily meant for debugging purposes.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const QuicheStringTuple& tuple) {
    std::vector<std::string> bits;
    bits.reserve(tuple.size());
    for (absl::string_view element : tuple) {
      bits.push_back(absl::StrCat("\"", absl::CHexEscape(element), "\""));
    }
    absl::Format(&sink, "{%s}", absl::StrJoin(bits, ", "));
  }

 private:
  void AddUnchecked(absl::string_view element) {
    elements_start_offsets_.push_back(data_.size());
    data_.append(element.data(), element.size());
  }

  std::string data_;
  absl::InlinedVector<OffsetT, InlinedSizes> elements_start_offsets_;
};

static_assert(std::input_iterator<QuicheStringTuple<>::iterator>);

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_STRING_TUPLE_H_

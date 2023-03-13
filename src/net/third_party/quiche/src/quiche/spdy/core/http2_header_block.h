// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_HTTP2_HEADER_BLOCK_H_
#define QUICHE_SPDY_CORE_HTTP2_HEADER_BLOCK_H_

#include <stddef.h>

#include <functional>
#include <list>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_linked_hash_map.h"
#include "quiche/common/quiche_text_utils.h"
#include "quiche/spdy/core/http2_header_storage.h"

namespace spdy {

namespace test {
class Http2HeaderBlockPeer;
class ValueProxyPeer;
}  // namespace test

#ifndef SPDY_HEADER_DEBUG
#if !defined(NDEBUG) || defined(ADDRESS_SANITIZER)
#define SPDY_HEADER_DEBUG 1
#else  // !defined(NDEBUG) || defined(ADDRESS_SANITIZER)
#define SPDY_HEADER_DEBUG 0
#endif  // !defined(NDEBUG) || defined(ADDRESS_SANITIZER)
#endif  // SPDY_HEADER_DEBUG

// This class provides a key-value map that can be used to store SPDY header
// names and values. This data structure preserves insertion order.
//
// Under the hood, this data structure uses large, contiguous blocks of memory
// to store names and values. Lookups may be performed with absl::string_view
// keys, and values are returned as absl::string_views (via ValueProxy, below).
// Value absl::string_views are valid as long as the Http2HeaderBlock exists;
// allocated memory is never freed until Http2HeaderBlock's destruction.
//
// This implementation does not make much of an effort to minimize wasted space.
// It's expected that keys are rarely deleted from a Http2HeaderBlock.
class QUICHE_EXPORT Http2HeaderBlock {
 private:
  // Stores a list of value fragments that can be joined later with a
  // key-dependent separator.
  class QUICHE_EXPORT HeaderValue {
   public:
    HeaderValue(Http2HeaderStorage* storage, absl::string_view key,
                absl::string_view initial_value);

    // Moves are allowed.
    HeaderValue(HeaderValue&& other);
    HeaderValue& operator=(HeaderValue&& other);

    void set_storage(Http2HeaderStorage* storage);

    // Copies are not.
    HeaderValue(const HeaderValue& other) = delete;
    HeaderValue& operator=(const HeaderValue& other) = delete;

    ~HeaderValue();

    // Consumes at most |fragment.size()| bytes of memory.
    void Append(absl::string_view fragment);

    absl::string_view value() const { return as_pair().second; }
    const std::pair<absl::string_view, absl::string_view>& as_pair() const;

    // Size estimate including separators. Used when keys are erased from
    // Http2HeaderBlock.
    size_t SizeEstimate() const { return size_; }

   private:
    // May allocate a large contiguous region of memory to hold the concatenated
    // fragments and separators.
    absl::string_view ConsolidatedValue() const;

    mutable Http2HeaderStorage* storage_;
    mutable std::vector<absl::string_view> fragments_;
    // The first element is the key; the second is the consolidated value.
    mutable std::pair<absl::string_view, absl::string_view> pair_;
    size_t size_ = 0;
    size_t separator_size_ = 0;
  };

  typedef quiche::QuicheLinkedHashMap<absl::string_view, HeaderValue,
                                      quiche::StringPieceCaseHash,
                                      quiche::StringPieceCaseEqual>
      MapType;

 public:
  typedef std::pair<absl::string_view, absl::string_view> value_type;

  // Provides iteration over a sequence of std::pair<absl::string_view,
  // absl::string_view>, even though the underlying MapType::value_type is
  // different. Dereferencing the iterator will result in memory allocation for
  // multi-value headers.
  class QUICHE_EXPORT iterator {
   public:
    // The following type definitions fulfill the requirements for iterator
    // implementations.
    typedef std::pair<absl::string_view, absl::string_view> value_type;
    typedef value_type& reference;
    typedef value_type* pointer;
    typedef std::forward_iterator_tag iterator_category;
    typedef MapType::iterator::difference_type difference_type;

    // In practice, this iterator only offers access to const value_type.
    typedef const value_type& const_reference;
    typedef const value_type* const_pointer;

    explicit iterator(MapType::const_iterator it);
    iterator(const iterator& other);
    ~iterator();

    // This will result in memory allocation if the value consists of multiple
    // fragments.
    const_reference operator*() const {
#if SPDY_HEADER_DEBUG
      QUICHE_CHECK(!dereference_forbidden_);
#endif  // SPDY_HEADER_DEBUG
      return it_->second.as_pair();
    }

    const_pointer operator->() const { return &(this->operator*()); }
    bool operator==(const iterator& it) const { return it_ == it.it_; }
    bool operator!=(const iterator& it) const { return !(*this == it); }

    iterator& operator++() {
      it_++;
      return *this;
    }

    iterator operator++(int) {
      auto ret = *this;
      this->operator++();
      return ret;
    }

#if SPDY_HEADER_DEBUG
    void forbid_dereference() { dereference_forbidden_ = true; }
#endif  // SPDY_HEADER_DEBUG

   private:
    MapType::const_iterator it_;
#if SPDY_HEADER_DEBUG
    bool dereference_forbidden_ = false;
#endif  // SPDY_HEADER_DEBUG
  };
  typedef iterator const_iterator;

  Http2HeaderBlock();
  Http2HeaderBlock(const Http2HeaderBlock& other) = delete;
  Http2HeaderBlock(Http2HeaderBlock&& other);
  ~Http2HeaderBlock();

  Http2HeaderBlock& operator=(const Http2HeaderBlock& other) = delete;
  Http2HeaderBlock& operator=(Http2HeaderBlock&& other);
  Http2HeaderBlock Clone() const;

  bool operator==(const Http2HeaderBlock& other) const;
  bool operator!=(const Http2HeaderBlock& other) const;

  // Provides a human readable multi-line representation of the stored header
  // keys and values.
  std::string DebugString() const;

  iterator begin() { return wrap_iterator(map_.begin()); }
  iterator end() { return wrap_iterator(map_.end()); }
  const_iterator begin() const { return wrap_const_iterator(map_.begin()); }
  const_iterator end() const { return wrap_const_iterator(map_.end()); }
  bool empty() const { return map_.empty(); }
  size_t size() const { return map_.size(); }
  iterator find(absl::string_view key) { return wrap_iterator(map_.find(key)); }
  const_iterator find(absl::string_view key) const {
    return wrap_const_iterator(map_.find(key));
  }
  bool contains(absl::string_view key) const { return find(key) != end(); }
  void erase(absl::string_view key);

  // Clears both our MapType member and the memory used to hold headers.
  void clear();

  // The next few methods copy data into our backing storage.

  // If key already exists in the block, replaces the value of that key. Else
  // adds a new header to the end of the block.
  void insert(const value_type& value);

  // If a header with the key is already present, then append the value to the
  // existing header value, NUL ("\0") separated unless the key is cookie, in
  // which case the separator is "; ".
  // If there is no such key, a new header with the key and value is added.
  void AppendValueOrAddHeader(const absl::string_view key,
                              const absl::string_view value);

  // This object provides automatic conversions that allow Http2HeaderBlock to
  // be nearly a drop-in replacement for
  // SpdyLinkedHashMap<std::string, std::string>.
  // It reads data from or writes data to a Http2HeaderStorage.
  class QUICHE_EXPORT ValueProxy {
   public:
    ~ValueProxy();

    // Moves are allowed.
    ValueProxy(ValueProxy&& other);
    ValueProxy& operator=(ValueProxy&& other);

    // Copies are not.
    ValueProxy(const ValueProxy& other) = delete;
    ValueProxy& operator=(const ValueProxy& other) = delete;

    // Assignment modifies the underlying Http2HeaderBlock.
    ValueProxy& operator=(absl::string_view value);

    // Provides easy comparison against absl::string_view.
    bool operator==(absl::string_view value) const;

    std::string as_string() const;

   private:
    friend class Http2HeaderBlock;
    friend class test::ValueProxyPeer;

    ValueProxy(Http2HeaderBlock* block,
               Http2HeaderBlock::MapType::iterator lookup_result,
               const absl::string_view key,
               size_t* spdy_header_block_value_size);

    Http2HeaderBlock* block_;
    Http2HeaderBlock::MapType::iterator lookup_result_;
    absl::string_view key_;
    size_t* spdy_header_block_value_size_;
    bool valid_;
  };

  // Allows either lookup or mutation of the value associated with a key.
  ABSL_MUST_USE_RESULT ValueProxy operator[](const absl::string_view key);

  size_t TotalBytesUsed() const { return key_size_ + value_size_; }

 private:
  friend class test::Http2HeaderBlockPeer;

  inline iterator wrap_iterator(MapType::const_iterator inner_iterator) const {
#if SPDY_HEADER_DEBUG
    iterator outer_iterator(inner_iterator);
    if (inner_iterator == map_.end()) {
      outer_iterator.forbid_dereference();
    }
    return outer_iterator;
#else   // SPDY_HEADER_DEBUG
    return iterator(inner_iterator);
#endif  // SPDY_HEADER_DEBUG
  }

  inline const_iterator wrap_const_iterator(
      MapType::const_iterator inner_iterator) const {
#if SPDY_HEADER_DEBUG
    const_iterator outer_iterator(inner_iterator);
    if (inner_iterator == map_.end()) {
      outer_iterator.forbid_dereference();
    }
    return outer_iterator;
#else   // SPDY_HEADER_DEBUG
    return iterator(inner_iterator);
#endif  // SPDY_HEADER_DEBUG
  }

  void AppendHeader(const absl::string_view key, const absl::string_view value);
  absl::string_view WriteKey(const absl::string_view key);
  size_t bytes_allocated() const;

  // absl::string_views held by |map_| point to memory owned by |storage_|.
  MapType map_;
  Http2HeaderStorage storage_;

  size_t key_size_ = 0;
  size_t value_size_ = 0;
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_HTTP2_HEADER_BLOCK_H_

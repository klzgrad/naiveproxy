// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_SPDY_HEADER_BLOCK_H_
#define QUICHE_SPDY_CORE_SPDY_HEADER_BLOCK_H_

#include <stddef.h>

#include <functional>
#include <list>
#include <string>
#include <utility>
#include <vector>

#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/spdy_header_storage.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_containers.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_macros.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_string_utils.h"

namespace spdy {

namespace test {
class SpdyHeaderBlockPeer;
class ValueProxyPeer;
}  // namespace test

// This class provides a key-value map that can be used to store SPDY header
// names and values. This data structure preserves insertion order.
//
// Under the hood, this data structure uses large, contiguous blocks of memory
// to store names and values. Lookups may be performed with QuicheStringPiece
// keys, and values are returned as QuicheStringPieces (via ValueProxy, below).
// Value QuicheStringPieces are valid as long as the SpdyHeaderBlock exists;
// allocated memory is never freed until SpdyHeaderBlock's destruction.
//
// This implementation does not make much of an effort to minimize wasted space.
// It's expected that keys are rarely deleted from a SpdyHeaderBlock.
class QUICHE_EXPORT_PRIVATE SpdyHeaderBlock {
 private:
  // Stores a list of value fragments that can be joined later with a
  // key-dependent separator.
  class QUICHE_EXPORT_PRIVATE HeaderValue {
   public:
    HeaderValue(SpdyHeaderStorage* storage,
                quiche::QuicheStringPiece key,
                quiche::QuicheStringPiece initial_value);

    // Moves are allowed.
    HeaderValue(HeaderValue&& other);
    HeaderValue& operator=(HeaderValue&& other);

    void set_storage(SpdyHeaderStorage* storage);

    // Copies are not.
    HeaderValue(const HeaderValue& other) = delete;
    HeaderValue& operator=(const HeaderValue& other) = delete;

    ~HeaderValue();

    // Consumes at most |fragment.size()| bytes of memory.
    void Append(quiche::QuicheStringPiece fragment);

    quiche::QuicheStringPiece value() const { return as_pair().second; }
    const std::pair<quiche::QuicheStringPiece, quiche::QuicheStringPiece>&
    as_pair() const;

    // Size estimate including separators. Used when keys are erased from
    // SpdyHeaderBlock.
    size_t SizeEstimate() const { return size_; }

   private:
    // May allocate a large contiguous region of memory to hold the concatenated
    // fragments and separators.
    quiche::QuicheStringPiece ConsolidatedValue() const;

    mutable SpdyHeaderStorage* storage_;
    mutable std::vector<quiche::QuicheStringPiece> fragments_;
    // The first element is the key; the second is the consolidated value.
    mutable std::pair<quiche::QuicheStringPiece, quiche::QuicheStringPiece>
        pair_;
    size_t size_ = 0;
    size_t separator_size_ = 0;
  };

  typedef SpdyLinkedHashMap<quiche::QuicheStringPiece,
                            HeaderValue,
                            SpdyStringPieceCaseHash,
                            SpdyStringPieceCaseEq>
      MapType;

 public:
  typedef std::pair<quiche::QuicheStringPiece, quiche::QuicheStringPiece>
      value_type;

  // Provides iteration over a sequence of std::pair<QuicheStringPiece,
  // QuicheStringPiece>, even though the underlying MapType::value_type is
  // different. Dereferencing the iterator will result in memory allocation for
  // multi-value headers.
  class QUICHE_EXPORT_PRIVATE iterator {
   public:
    // The following type definitions fulfill the requirements for iterator
    // implementations.
    typedef std::pair<quiche::QuicheStringPiece, quiche::QuicheStringPiece>
        value_type;
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
    const_reference operator*() const { return it_->second.as_pair(); }

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

   private:
    MapType::const_iterator it_;
  };
  typedef iterator const_iterator;

  SpdyHeaderBlock();
  SpdyHeaderBlock(const SpdyHeaderBlock& other) = delete;
  SpdyHeaderBlock(SpdyHeaderBlock&& other);
  ~SpdyHeaderBlock();

  SpdyHeaderBlock& operator=(const SpdyHeaderBlock& other) = delete;
  SpdyHeaderBlock& operator=(SpdyHeaderBlock&& other);
  SpdyHeaderBlock Clone() const;

  bool operator==(const SpdyHeaderBlock& other) const;
  bool operator!=(const SpdyHeaderBlock& other) const;

  // Provides a human readable multi-line representation of the stored header
  // keys and values.
  std::string DebugString() const;

  iterator begin() { return iterator(map_.begin()); }
  iterator end() { return iterator(map_.end()); }
  const_iterator begin() const { return const_iterator(map_.begin()); }
  const_iterator end() const { return const_iterator(map_.end()); }
  bool empty() const { return map_.empty(); }
  size_t size() const { return map_.size(); }
  iterator find(quiche::QuicheStringPiece key) {
    return iterator(map_.find(key));
  }
  const_iterator find(quiche::QuicheStringPiece key) const {
    return const_iterator(map_.find(key));
  }
  void erase(quiche::QuicheStringPiece key);

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
  void AppendValueOrAddHeader(const quiche::QuicheStringPiece key,
                              const quiche::QuicheStringPiece value);

  // This object provides automatic conversions that allow SpdyHeaderBlock to be
  // nearly a drop-in replacement for
  // SpdyLinkedHashMap<std::string, std::string>.
  // It reads data from or writes data to a SpdyHeaderStorage.
  class QUICHE_EXPORT_PRIVATE ValueProxy {
   public:
    ~ValueProxy();

    // Moves are allowed.
    ValueProxy(ValueProxy&& other);
    ValueProxy& operator=(ValueProxy&& other);

    // Copies are not.
    ValueProxy(const ValueProxy& other) = delete;
    ValueProxy& operator=(const ValueProxy& other) = delete;

    // Assignment modifies the underlying SpdyHeaderBlock.
    ValueProxy& operator=(quiche::QuicheStringPiece value);

    // Provides easy comparison against QuicheStringPiece.
    bool operator==(quiche::QuicheStringPiece value) const;

    std::string as_string() const;

   private:
    friend class SpdyHeaderBlock;
    friend class test::ValueProxyPeer;

    ValueProxy(SpdyHeaderBlock* block,
               SpdyHeaderBlock::MapType::iterator lookup_result,
               const quiche::QuicheStringPiece key,
               size_t* spdy_header_block_value_size);

    SpdyHeaderBlock* block_;
    SpdyHeaderBlock::MapType::iterator lookup_result_;
    quiche::QuicheStringPiece key_;
    size_t* spdy_header_block_value_size_;
    bool valid_;
  };

  // Allows either lookup or mutation of the value associated with a key.
  SPDY_MUST_USE_RESULT ValueProxy
  operator[](const quiche::QuicheStringPiece key);

  // Returns the estimate of dynamically allocated memory in bytes.
  size_t EstimateMemoryUsage() const;

  size_t TotalBytesUsed() const { return key_size_ + value_size_; }

 private:
  friend class test::SpdyHeaderBlockPeer;

  void AppendHeader(const quiche::QuicheStringPiece key,
                    const quiche::QuicheStringPiece value);
  quiche::QuicheStringPiece WriteKey(const quiche::QuicheStringPiece key);
  size_t bytes_allocated() const;

  // QuicheStringPieces held by |map_| point to memory owned by |storage_|.
  MapType map_;
  SpdyHeaderStorage storage_;

  size_t key_size_ = 0;
  size_t value_size_ = 0;
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_SPDY_HEADER_BLOCK_H_

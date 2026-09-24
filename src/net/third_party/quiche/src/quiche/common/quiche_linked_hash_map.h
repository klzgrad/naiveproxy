// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a simplistic insertion-ordered map.  It behaves similarly to an STL
// map, but only implements a small subset of the map's methods.  Internally, we
// just keep a map and a list going in parallel.
//
// This class provides no thread safety guarantees, beyond what you would
// normally see with std::list.
//
// Iterators point into the list and should be stable in the face of
// mutations, except for an iterator pointing to an element that was just
// deleted.

#ifndef QUICHE_COMMON_QUICHE_LINKED_HASH_MAP_H_
#define QUICHE_COMMON_QUICHE_LINKED_HASH_MAP_H_

#include <functional>
#include <list>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_flag_utils.h"
#include "quiche/common/platform/api/quiche_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/stable_block_list.h"

namespace quiche {

// This holds a list of pair<Key, Value> items.  This list is what gets
// traversed, and it's iterators from this list that we return from
// begin/end/find.
//
// We also keep a set<list::iterator> for find.  Since std::list is a
// doubly-linked list, the iterators should remain stable.

// QUICHE_NO_EXPORT comments suppress erroneous presubmit failures.
template <class Key,                      // QUICHE_NO_EXPORT
          class Value,                    // QUICHE_NO_EXPORT
          class Hash = absl::Hash<Key>,   // QUICHE_NO_EXPORT
          class Eq = std::equal_to<Key>,  // QUICHE_NO_EXPORT
          size_t BlockSize = 16>          // QUICHE_NO_EXPORT
class QuicheLinkedHashMap {               // QUICHE_NO_EXPORT
 private:
  using StdList = std::list<std::pair<Key, Value>>;
  using BlockList = quiche::StableBlockList<std::pair<Key, Value>, BlockSize>;
  using ListType = std::variant<StdList, BlockList>;

 public:
  class const_iterator;

  class iterator {
   public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = std::pair<Key, Value>;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type*;
    using reference = value_type&;

    iterator() = default;
    iterator(typename StdList::iterator it) : it_(it) {}
    iterator(typename BlockList::iterator it) : it_(it) {}

    reference operator*() const {
      return std::visit([](auto&& it) -> reference { return *it; }, it_);
    }
    pointer operator->() const {
      return std::visit([](auto&& it) -> pointer { return &*it; }, it_);
    }
    iterator& operator++() {
      std::visit([](auto&& it) { ++it; }, it_);
      return *this;
    }
    iterator operator++(int) {
      iterator tmp = *this;
      ++(*this);
      return tmp;
    }
    iterator& operator--() {
      std::visit([](auto&& it) { --it; }, it_);
      return *this;
    }
    iterator operator--(int) {
      iterator tmp = *this;
      --(*this);
      return tmp;
    }
    bool operator==(const iterator& other) const { return it_ == other.it_; }
    bool operator!=(const iterator& other) const { return !(*this == other); }

   private:
    std::variant<typename StdList::iterator, typename BlockList::iterator> it_;
    friend class QuicheLinkedHashMap;
    friend class const_iterator;
  };

  class const_iterator {
   public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = std::pair<Key, Value>;
    using difference_type = std::ptrdiff_t;
    using pointer = const value_type*;
    using reference = const value_type&;

    const_iterator() = default;
    const_iterator(typename StdList::const_iterator it) : it_(it) {}
    const_iterator(typename BlockList::const_iterator it) : it_(it) {}
    const_iterator(const iterator& other) {
      std::visit([this](auto&& it) { it_ = it; }, other.it_);
    }

    reference operator*() const {
      return std::visit([](auto&& it) -> reference { return *it; }, it_);
    }
    pointer operator->() const {
      return std::visit([](auto&& it) -> pointer { return &*it; }, it_);
    }
    const_iterator& operator++() {
      std::visit([](auto&& it) { ++it; }, it_);
      return *this;
    }
    const_iterator operator++(int) {
      const_iterator tmp = *this;
      ++(*this);
      return tmp;
    }
    const_iterator& operator--() {
      std::visit([](auto&& it) { --it; }, it_);
      return *this;
    }
    const_iterator operator--(int) {
      const_iterator tmp = *this;
      --(*this);
      return tmp;
    }
    bool operator==(const const_iterator& other) const {
      return it_ == other.it_;
    }
    bool operator!=(const const_iterator& other) const {
      return !(*this == other);
    }

   private:
    std::variant<typename StdList::const_iterator,
                 typename BlockList::const_iterator>
        it_;
    friend class QuicheLinkedHashMap;
  };

  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using key_type = Key;
  using value_type = std::pair<Key, Value>;
  using size_type = size_t;

  QuicheLinkedHashMap() {
    if (GetQuicheReloadableFlag(quiche_linked_hash_map_use_stable_block_list)) {
      list_.template emplace<BlockList>();
      QUICHE_RELOADABLE_FLAG_COUNT(
          quiche_linked_hash_map_use_stable_block_list);
    }
  }

  explicit QuicheLinkedHashMap(size_type bucket_count) : map_(bucket_count) {
    if (GetQuicheReloadableFlag(quiche_linked_hash_map_use_stable_block_list)) {
      list_.template emplace<BlockList>();
      QUICHE_RELOADABLE_FLAG_COUNT(
          quiche_linked_hash_map_use_stable_block_list);
    }
  }

  QuicheLinkedHashMap(const QuicheLinkedHashMap& other) = delete;
  QuicheLinkedHashMap& operator=(const QuicheLinkedHashMap& other) = delete;
  QuicheLinkedHashMap(QuicheLinkedHashMap&& other) = default;
  QuicheLinkedHashMap& operator=(QuicheLinkedHashMap&& other) = default;

  // Returns an iterator to the first (insertion-ordered) element.  Like a map,
  // this can be dereferenced to a pair<Key, Value>.
  iterator begin() {
    return std::visit([](auto& list) -> iterator { return list.begin(); },
                      list_);
  }
  const_iterator begin() const {
    return std::visit([](auto& list) -> const_iterator { return list.begin(); },
                      list_);
  }

  // Returns an iterator beyond the last element.
  iterator end() {
    return std::visit([](auto& list) -> iterator { return list.end(); }, list_);
  }
  const_iterator end() const {
    return std::visit([](auto& list) -> const_iterator { return list.end(); },
                      list_);
  }

  // Returns an iterator to the last (insertion-ordered) element.  Like a map,
  // this can be dereferenced to a pair<Key, Value>.
  reverse_iterator rbegin() { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }

  // Returns an iterator beyond the first element.
  reverse_iterator rend() { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }

  // Front and back accessors common to many stl containers.

  // Returns the earliest-inserted element
  const value_type& front() const {
    return std::visit(
        [](auto& list) -> const value_type& { return list.front(); }, list_);
  }
  // Returns the earliest-inserted element.
  value_type& front() {
    return std::visit([](auto& list) -> value_type& { return list.front(); },
                      list_);
  }

  // Returns the most-recently-inserted element.
  const value_type& back() const {
    return std::visit(
        [](auto& list) -> const value_type& { return list.back(); }, list_);
  }
  // Returns the most-recently-inserted element.
  value_type& back() {
    return std::visit([](auto& list) -> value_type& { return list.back(); },
                      list_);
  }

  // Clears the map of all values.
  void clear() {
    map_.clear();
    std::visit([](auto& list) { list.clear(); }, list_);
  }

  // Returns true iff the map is empty.
  bool empty() const {
    return std::visit([](auto& list) { return list.empty(); }, list_);
  }

  // Removes the first element from the list.
  void pop_front() { erase(begin()); }

  // Erases values with the provided key.  Returns the number of elements
  // erased.  In this implementation, this will be 0 or 1.
  size_type erase(const Key& key) {
    typename MapType::iterator found = map_.find(key);
    if (found == map_.end()) {
      return 0;
    }

    if (auto* list = std::get_if<BlockList>(&list_)) {
      list->erase(std::get<typename BlockList::iterator>(found->second.it_));
    } else {
      std::get<StdList>(list_).erase(
          std::get<typename StdList::iterator>(found->second.it_));
    }
    map_.erase(found);

    return 1;
  }

  // Erases the item that 'position' points to. Returns an iterator that points
  // to the item that comes immediately after the deleted item in the list, or
  // end().
  // If the provided iterator is invalid or there is inconsistency between the
  // map and list, a QUICHE_CHECK() error will occur.
  iterator erase(iterator position) {
    typename MapType::iterator found = map_.find(position->first);
    QUICHE_CHECK(found->second == position)
        << "Inconsistent iterator for map and list, or the iterator is "
           "invalid.";

    map_.erase(found);
    if (auto* list = std::get_if<BlockList>(&list_)) {
      return list->erase(std::get<typename BlockList::iterator>(position.it_));
    } else {
      return std::get<StdList>(list_).erase(
          std::get<typename StdList::iterator>(position.it_));
    }
  }

  // Erases all the items in the range [first, last).  Returns an iterator that
  // points to the item that comes immediately after the last deleted item in
  // the list, or end().
  iterator erase(iterator first, iterator last) {
    while (first != last && first != end()) {
      first = erase(first);
    }
    return first;
  }

  // Finds the element with the given key.  Returns an iterator to the
  // value found, or to end() if the value was not found.  Like a map, this
  // iterator points to a pair<Key, Value>.
  iterator find(const Key& key) {
    typename MapType::iterator found = map_.find(key);
    if (found == map_.end()) {
      return end();
    }
    return found->second;
  }

  const_iterator find(const Key& key) const {
    typename MapType::const_iterator found = map_.find(key);
    if (found == map_.end()) {
      return end();
    }
    return const_iterator(found->second);
  }

  bool contains(const Key& key) const { return find(key) != end(); }

  // Returns the value mapped to key, or an inserted iterator to that position
  // in the map.
  Value& operator[](const key_type& key) {
    return (*((this->insert(std::make_pair(key, Value()))).first)).second;
  }

  // Inserts an element into the map
  std::pair<iterator, bool> insert(const std::pair<Key, Value>& pair) {
    return InsertInternal(pair);
  }

  // Inserts an element into the map
  std::pair<iterator, bool> insert(std::pair<Key, Value>&& pair) {
    return InsertInternal(std::move(pair));
  }

  // Derive size_ from map_, as list::size might be O(N).
  size_type size() const { return map_.size(); }

  template <typename... Args>
  std::pair<iterator, bool> try_emplace(const key_type& key, Args&&... args) {
    return TryEmplaceInternal(key, std::forward<Args>(args)...);
  }

  template <typename... Args>
  std::pair<iterator, bool> try_emplace(key_type&& key, Args&&... args) {
    return TryEmplaceInternal(std::move(key), std::forward<Args>(args)...);
  }

  // TODO(b/532261946): add back `emplace()` if needed

  void swap(QuicheLinkedHashMap& other) {
    map_.swap(other.map_);
    list_.swap(other.list_);
  }

 private:
  template <typename U>
  std::pair<iterator, bool> InsertInternal(U&& pair) {
    auto insert_result = map_.try_emplace(pair.first);
    auto map_iter = insert_result.first;

    // If the map already contains this key, return a pair with an iterator to
    // it, and false indicating that we didn't insert anything.
    if (!insert_result.second) {
      return {map_iter->second, false};
    }

    // Otherwise, insert into the list, and set value in map.
    iterator list_iter = std::visit(
        [&pair](auto& list) -> iterator {
          return iterator(list.insert(list.end(), std::forward<U>(pair)));
        },
        list_);
    map_iter->second = list_iter;

    return {list_iter, true};
  }

  template <typename K, typename... Args>
  std::pair<iterator, bool> TryEmplaceInternal(K&& key, Args&&... args) {
    auto insert_result = map_.try_emplace(std::forward<K>(key));

    if (!insert_result.second) {
      return {insert_result.first->second, false};
    }

    iterator list_iter = std::visit(
        [&insert_result, &args...](auto& list) -> iterator {
          return iterator(
              list.emplace(list.end(), std::piecewise_construct,
                           std::forward_as_tuple(insert_result.first->first),
                           std::forward_as_tuple(std::forward<Args>(args)...)));
        },
        list_);

    insert_result.first->second = list_iter;
    return {list_iter, true};
  }

  // The list component, used for maintaining insertion order
  ListType list_;
  using MapType = absl::flat_hash_map<Key, iterator, Hash, Eq>;
  // The map component, used for speedy lookups
  MapType map_;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_LINKED_HASH_MAP_H_

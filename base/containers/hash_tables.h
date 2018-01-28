// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_CONTAINERS_HASH_TABLES_H_
#define BASE_CONTAINERS_HASH_TABLES_H_

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "base/hash.h"

// This header file is deprecated. Use the corresponding C++11 type
// instead. https://crbug.com/576864

// Use a custom hasher instead.
#define BASE_HASH_NAMESPACE base_hash

namespace BASE_HASH_NAMESPACE {

// A separate hasher which, by default, forwards to std::hash. This is so legacy
// uses of BASE_HASH_NAMESPACE with base::hash_map do not interfere with
// std::hash mid-transition.
template<typename T>
struct hash {
  std::size_t operator()(const T& value) const { return std::hash<T>()(value); }
};

// Use base::IntPairHash from base/hash.h as a custom hasher instead.
template <typename Type1, typename Type2>
struct hash<std::pair<Type1, Type2>> {
  std::size_t operator()(std::pair<Type1, Type2> value) const {
    return base::HashInts(value.first, value.second);
  }
};

}  // namespace BASE_HASH_NAMESPACE

namespace base {

// Use std::unordered_map instead.
template <class Key,
          class T,
          class Hash = BASE_HASH_NAMESPACE::hash<Key>,
          class Pred = std::equal_to<Key>,
          class Alloc = std::allocator<std::pair<const Key, T>>>
using hash_map = std::unordered_map<Key, T, Hash, Pred, Alloc>;

// Use std::unordered_multimap instead.
template <class Key,
          class T,
          class Hash = BASE_HASH_NAMESPACE::hash<Key>,
          class Pred = std::equal_to<Key>,
          class Alloc = std::allocator<std::pair<const Key, T>>>
using hash_multimap = std::unordered_multimap<Key, T, Hash, Pred, Alloc>;

// Use std::unordered_multiset instead.
template <class Key,
          class Hash = BASE_HASH_NAMESPACE::hash<Key>,
          class Pred = std::equal_to<Key>,
          class Alloc = std::allocator<Key>>
using hash_multiset = std::unordered_multiset<Key, Hash, Pred, Alloc>;

// Use std::unordered_set instead.
template <class Key,
          class Hash = BASE_HASH_NAMESPACE::hash<Key>,
          class Pred = std::equal_to<Key>,
          class Alloc = std::allocator<Key>>
using hash_set = std::unordered_set<Key, Hash, Pred, Alloc>;

}  // namespace base

#endif  // BASE_CONTAINERS_HASH_TABLES_H_

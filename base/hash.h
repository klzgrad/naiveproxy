// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_HASH_H_
#define BASE_HASH_H_

#include <stddef.h>
#include <stdint.h>

#include <limits>
#include <string>
#include <utility>

#include "base/base_export.h"
#include "base/logging.h"
#include "base/strings/string16.h"

namespace base {

// Computes a hash of a memory buffer. This hash function is subject to change
// in the future, so use only for temporary in-memory structures. If you need
// to persist a change on disk or between computers, use PersistentHash().
//
// WARNING: This hash function should not be used for any cryptographic purpose.
BASE_EXPORT uint32_t Hash(const void* data, size_t length);
BASE_EXPORT uint32_t Hash(const std::string& str);
BASE_EXPORT uint32_t Hash(const string16& str);

// Computes a hash of a memory buffer. This hash function must not change so
// that code can use the hashed values for persistent storage purposes or
// sending across the network. If a new persistent hash function is desired, a
// new version will have to be added in addition.
//
// WARNING: This hash function should not be used for any cryptographic purpose.
BASE_EXPORT uint32_t PersistentHash(const void* data, size_t length);
BASE_EXPORT uint32_t PersistentHash(const std::string& str);

// Hash pairs of 32-bit or 64-bit numbers.
BASE_EXPORT size_t HashInts32(uint32_t value1, uint32_t value2);
BASE_EXPORT size_t HashInts64(uint64_t value1, uint64_t value2);

template <typename T1, typename T2>
inline size_t HashInts(T1 value1, T2 value2) {
  // This condition is expected to be compile-time evaluated and optimised away
  // in release builds.
  if (sizeof(T1) > sizeof(uint32_t) || (sizeof(T2) > sizeof(uint32_t)))
    return HashInts64(value1, value2);

  return HashInts32(value1, value2);
}

// A templated hasher for pairs of integer types. Example:
//
//   using MyPair = std::pair<int32_t, int32_t>;
//   std::unordered_set<MyPair, base::IntPairHash<MyPair>> set;
template <typename T>
struct IntPairHash;

template <typename Type1, typename Type2>
struct IntPairHash<std::pair<Type1, Type2>> {
  size_t operator()(std::pair<Type1, Type2> value) const {
    return HashInts(value.first, value.second);
  }
};

}  // namespace base

#endif  // BASE_HASH_H_

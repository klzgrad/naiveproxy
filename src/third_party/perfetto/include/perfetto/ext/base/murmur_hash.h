/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef INCLUDE_PERFETTO_EXT_BASE_MURMUR_HASH_H_
#define INCLUDE_PERFETTO_EXT_BASE_MURMUR_HASH_H_

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>

#include "perfetto/public/compiler.h"

// This file provides an implementation of the 64-bit MurmurHash2 algorithm,
// also known as MurmurHash64A. This algorithm, created by Austin Appleby, is a
// fast, non-cryptographic hash function with excellent distribution properties,
// making it ideal for use in hash tables.
//
// The file also includes related hashing utilities:
// - A standalone `fmix64` finalizer from MurmurHash3, used for hashing
//   individual numeric types.
// - A hash combiner for creating a single hash from a sequence of values.
//
// NOTE: This implementation is NOT cryptographically secure. It must not be
// used for security-sensitive applications like password storage or digital
// signatures, as it is not designed to be resistant to malicious attacks.

namespace perfetto::base {

namespace murmur_internal {

// Finalizes an intermediate hash value using the `fmix64` routine from
// MurmurHash3.
//
// This function's purpose is to thoroughly mix the bits of the hash state to
// ensure the final result is well-distributed, which is critical for avoiding
// collisions in hash tables.
//
// Args:
//   h: The intermediate hash value to be finalized.
//
// Returns:
//   The final, well-mixed 64-bit hash value.
inline uint64_t MurmurHashMix(uint64_t h) {
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdULL;
  h ^= h >> 33;
  h *= 0xc4ceb9fe1a85ec53ULL;
  h ^= h >> 33;
  return h;
}

// Computes a 64-bit hash for a block of memory using the MurmurHash64A
// algorithm.
//
// The process involves four main steps:
// 1. Initialization: The hash state is seeded with a value derived from the
//    input length.
// 2. Main Loop: Data is processed in 8-byte chunks, with each chunk being
//    mixed into the hash state.
// 3. Tail Processing: The final 1-7 bytes of data are handled.
// 4. Finalization: The hash state is passed through a final mixing sequence to
//    ensure good bit distribution.
//
// Args:
//   input: A pointer to the data to be hashed.
//   len:   The length of the data in bytes.
//
// Returns:
//   The 64-bit MurmurHash64A hash of the input data.
inline uint64_t MurmurHashBytes(const void* input, size_t len) {
  // This implementation follows the canonical MurmurHash64A algorithm.
  // The constants `kMulConstant` (m) and the shift value `47` (r) are from
  // the original specification.
  // The seed is inspired by the one used in DuckDB.
  static constexpr uint64_t kSeed = 0xe17a1465U;
  static constexpr uint64_t kMulConstant = 0xc6a4a7935bd1e995;
  static constexpr int kShift = 47;

  uint64_t h = kSeed ^ (len * kMulConstant);
  const auto* data = static_cast<const uint8_t*>(input);
  const size_t num_blocks = len / 8;

  // Process 8-byte (64-bit) chunks
  for (size_t i = 0; i < num_blocks; ++i) {
    uint64_t k;
    memcpy(&k, data, sizeof(k));
    data += sizeof(k);  // Advance the pointer by 8 bytes

    k *= kMulConstant;
    k ^= k >> kShift;
    k *= kMulConstant;

    h ^= k;
    h *= kMulConstant;
  }

  // Process the remaining 1 to 7 bytes
  // The 'byte_ptr' now points to the beginning of the tail.
  switch (len & 7) {
    case 7:
      h ^= static_cast<uint64_t>(data[6]) << 48;
      [[fallthrough]];
    case 6:
      h ^= static_cast<uint64_t>(data[5]) << 40;
      [[fallthrough]];
    case 5:
      h ^= static_cast<uint64_t>(data[4]) << 32;
      [[fallthrough]];
    case 4:
      h ^= static_cast<uint64_t>(data[3]) << 24;
      [[fallthrough]];
    case 3:
      h ^= static_cast<uint64_t>(data[2]) << 16;
      [[fallthrough]];
    case 2:
      h ^= static_cast<uint64_t>(data[1]) << 8;
      [[fallthrough]];
    case 1:
      h ^= static_cast<uint64_t>(data[0]);
      h *= kMulConstant;
  }

  // Final mixing stage
  h ^= h >> kShift;
  h *= kMulConstant;
  h ^= h >> kShift;

  return h;
}

template <typename Float, typename Int>
Int NormalizeFloatToInt(Float value) {
  static_assert(std::is_floating_point_v<Float>);
  static_assert(std::is_integral_v<Int>);

  // Normalize floating point representations which can vary.
  if (PERFETTO_UNLIKELY(value == 0.0)) {
    // Turn negative zero into positive zero
    value = 0.0;
  } else if (PERFETTO_UNLIKELY(std::isnan(value))) {
    // Turn arbtirary NaN representations to a consistent NaN repr.
    value = std::numeric_limits<Float>::quiet_NaN();
  }
  Int res;
  static_assert(sizeof(Float) == sizeof(Int));
  memcpy(&res, &value, sizeof(Float));
  return res;
}

}  // namespace murmur_internal

// std::hash<T> drop-in class which uses the core MurmurHash functions above to
// produce a hash.
//
// Uses:
//  1) MurmurHashMix for fixed size numeric types (integers, floats, doubles).
//  2) MurmurHashBytes for string types (string, string_view) etc.
//  3) Falls back to std::hash<T> for all other types.
//     TODO(lalitm): create a absl-like API for allowing aribtrary types
//     to be hashed without needing to override std::hash<T>.
template <typename T>
struct MurmurHash {
  uint64_t operator()(const T& value) const {
    if constexpr (std::is_integral_v<T>) {
      return murmur_internal::MurmurHashMix(static_cast<uint64_t>(value));
    } else if constexpr (std::is_same_v<T, double>) {
      return murmur_internal::MurmurHashMix(
          murmur_internal::NormalizeFloatToInt<double, uint64_t>(value));
    } else if constexpr (std::is_same_v<T, float>) {
      return murmur_internal::MurmurHashMix(
          murmur_internal::NormalizeFloatToInt<float, uint32_t>(value));
    } else if constexpr (std::is_same_v<T, std::string> ||
                         std::is_same_v<T, std::string_view>) {
      return murmur_internal::MurmurHashBytes(value.data(), value.size());
    } else {
      return std::hash<T>{}(value);
    }
  }
};

// Simple wrapper function around MurmurHash to improve clarity in callsites
// to not have to instantiate the class and then call operator().
template <typename T>
uint64_t MurmurHashValue(const T& value) {
  return MurmurHash<T>{}(value);
}

// A helper class to create a 64-bit MurmurHash from a series of
// structured fields.
//
// IMPORTANT: This is NOT a true streaming hash. It is an order-dependent
// combiner. It does not guarantee that hashing two concatenated chunks of data
// will produce the same result as hashing them separately in sequence. It is
// designed exclusively for creating a hash from a fixed set of fields.
class MurmurHashCombiner {
 public:
  MurmurHashCombiner() : hash_(kSeed) {}

  // Combines the hash of one or more arguments into the combiner's state.
  //
  // This function uses a C++17 fold expression to hash each argument with
  // `MurmurHashValue` and then mixes it into the current state via the private
  // `Update` method. The combination is order-dependent.
  template <typename... Args>
  void Combine(const Args&... args) {
    // A C++17 fold expression that calls our private Update for each hashed
    // arg.
    (Update(MurmurHashValue(args)), ...);
  }

  // Returns the digest (i.e. current state of the combiner).
  inline uint64_t digest() const { return hash_; }

 private:
  // Low-level update with a pre-computed hash value. This uses a fast,
  // order-dependent combination step inspired by the `hash_combine` function
  // in the Boost C++ libraries.
  inline void Update(uint64_t piece_hash) {
    hash_ ^= piece_hash + 0x9e3779b9 + (hash_ << 6) + (hash_ >> 2);
  }

  static constexpr uint64_t kSeed = 0xe17a1465U;
  uint64_t hash_;
};

// Simple wrapper function around MurmurHashCombiner to improve clarity in
// callsites to not have to instantiate the class, call Combine() then digest().
template <typename... Args>
uint64_t MurmurHashCombine(const Args&... value) {
  MurmurHashCombiner combiner;
  combiner.Combine(value...);
  return combiner.digest();
}

}  // namespace perfetto::base

#endif  // INCLUDE_PERFETTO_EXT_BASE_MURMUR_HASH_H_

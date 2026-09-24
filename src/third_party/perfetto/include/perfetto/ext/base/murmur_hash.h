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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "perfetto/ext/base/hash.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/public/compiler.h"

// We need <cmath> only for std::isnan here. But cmath is a quite heavy
// header AND is not frequently used, so its include cost is generally
// NOT amortized. OTOH this header is very frequently used.
// The code below avoids pulling cmath in many translation units, reducing
// compilation time. Under the hoods std::isnan uses __builtin_isnan if
// available.
#if PERFETTO_HAS_BUILTIN(__builtin_isnan)
#define PERFETTO_IS_NAN(x) __builtin_isnan(x)
#else
#include <cmath>
#define PERFETTO_IS_NAN(x) std::isnan(x)
#endif

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
  } else if (PERFETTO_UNLIKELY(PERFETTO_IS_NAN(value))) {
    // Turn arbtirary NaN representations to a consistent NaN repr.
    value = std::numeric_limits<Float>::quiet_NaN();
  }
  Int res;
  static_assert(sizeof(Float) == sizeof(Int));
  memcpy(&res, &value, sizeof(Float));
  return res;
}

// Computes a 64-bit hash for a single built-in value without any combination.
// This is the core primitive used by both MurmurHashValue and
// MurmurHashCombiner::CombineOne for built-in types.
//
// NOTE: This function intentionally has no else branch for non-builtin types,
// which will cause a compile error if called with an unsupported type. Callers
// should check if the type is supported before calling this function.
template <typename T>
auto MurmurHashBuiltinValue(const T& value) {
  if constexpr (std::is_enum_v<T>) {
    return murmur_internal::MurmurHashMix(
        static_cast<uint64_t>(static_cast<std::underlying_type_t<T>>(value)));
  } else if constexpr (std::is_integral_v<T>) {
    return murmur_internal::MurmurHashMix(static_cast<uint64_t>(value));
  } else if constexpr (std::is_same_v<T, double>) {
    return murmur_internal::MurmurHashMix(
        murmur_internal::NormalizeFloatToInt<double, uint64_t>(value));
  } else if constexpr (std::is_same_v<T, float>) {
    return murmur_internal::MurmurHashMix(
        murmur_internal::NormalizeFloatToInt<float, uint32_t>(value));
  } else if constexpr (std::is_same_v<T, std::string> ||
                       std::is_same_v<T, std::string_view> ||
                       std::is_same_v<T, base::StringView>) {
    return murmur_internal::MurmurHashBytes(value.data(), value.size());
  } else if constexpr (std::is_same_v<T, const char*>) {
    std::string_view view(value);
    return murmur_internal::MurmurHashBytes(view.data(), view.size());
  } else if constexpr (std::is_pointer_v<T>) {
    return murmur_internal::MurmurHashMix(
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value)));
  } else {
    struct InvalidBuiltin {};
    return InvalidBuiltin{};
  }
}

// Helper to check if a type has a built-in MurmurHash implementation.
template <typename T>
constexpr bool HasMurmurHashBuiltinValue() {
  return std::is_same_v<decltype(MurmurHashBuiltinValue(std::declval<T>())),
                        uint64_t>;
}

// Helper to check if two types are integeral and U is convertible to T.
template <typename T, typename U>
constexpr bool IsConvertibleIntegral() {
  return std::is_integral_v<T> && std::is_integral_v<U> &&
         std::is_convertible_v<U, T>;
}

// Helper to check if a type is string-like (i.e. string, c-string or string
// views).
template <typename T>
constexpr bool IsStringLike() {
  return std::is_same_v<T, std::string> ||
         std::is_same_v<T, std::string_view> ||
         std::is_same_v<T, base::StringView> || std::is_same_v<T, const char*>;
}

// Helper to check if heterogeneous lookup is allowed between T and U.
// Only allows it for convertible integral types and string-like types.
template <typename T, typename U>
constexpr bool AllowsHeterogeneousLookup() {
  return IsConvertibleIntegral<T, U>() ||
         (IsStringLike<T>() && IsStringLike<U>());
}

// Helper to detect pointers in Combine(...).
// Hashing pointers directly is prohibited as it often indicates a misuse
// (e.g., passing ptr and len instead of a single std::string_view).
template <typename... Args>
constexpr bool HasPointerV = (std::is_pointer_v<Args> || ...);

}  // namespace murmur_internal

// ============================================================================
// MurmurHashCombiner - the core hasher state object
// ============================================================================
//
// A helper class to create a 64-bit MurmurHash from a series of
// structured fields.
//
// This class supports both the absl-style hasher API and a direct
// member Combine() method.
//
// Absl-style API (for custom types with PerfettoHashValue):
//   template <typename H>
//   friend H PerfettoHashValue(H h, const MyType& value) {
//     return H::Combine(std::move(h), value.field1, value.field2);
//   }
//
// Direct API (for simple hash combining):
//   MurmurHashCombiner combiner;
//   combiner.Combine(field1, field2, ...);
//   return combiner.digest();
//
// IMPORTANT: This is NOT a true streaming hash. It is an order-dependent
// combiner. It does not guarantee that hashing two concatenated chunks of data
// will produce the same result as hashing them separately in sequence.
class MurmurHashCombiner {
 public:
  MurmurHashCombiner() = default;

  // Static Combine - returns a new hasher with the combined state.
  // This is used by the absl-style PerfettoHashValue API.
  template <typename... Args>
  static MurmurHashCombiner Combine(MurmurHashCombiner h, const Args&... args) {
    h.Combine(args...);
    return h;
  }

  // Member Combine - combines values into this hasher's state.
  // This is a convenient API for directly combining multiple values.
  // The combination is order-dependent.
  template <typename... Args>
  void Combine(const Args&... args) {
    static_assert(!murmur_internal::HasPointerV<Args...>,
                  "MurmurHashCombiner::Combine() does not support pointers. "
                  "If you want to hash the contents of a memory range, use a "
                  "single hashable object (e.g., std::string_view). If you "
                  "want to hash a pointer address, cast it to uintptr_t.");
    // Uses a C++17 fold expression with CombineOne for each argument.
    (CombineOne(args), ...);
  }

  // Returns the digest (i.e. current state of the combiner).
  uint64_t digest() const { return hash_; }

 private:
  // Combines a single value into the hasher state.
  template <typename T>
  void CombineOne(const T& value) {
    if constexpr (murmur_internal::HasMurmurHashBuiltinValue<T>()) {
      Update(murmur_internal::MurmurHashBuiltinValue(value));
    } else {
      // For custom types, use ADL to find the PerfettoHashValue function.
      // This will cause a compile error with a clear message if the function
      // is not defined.
      hash_ = PerfettoHashValue(std::move(*this), value).digest();
    }
  }

  // Low-level update with a pre-computed hash value. This uses a fast,
  // order-dependent combination step inspired by the `hash_combine` function
  // in the Boost C++ libraries.
  void Update(uint64_t piece_hash) {
    hash_ ^= piece_hash + 0x9e3779b9 + (hash_ << 6) + (hash_ >> 2);
  }

  static constexpr uint64_t kSeed = 0xe17a1465U;
  uint64_t hash_ = kSeed;
};

// Simple wrapper function around MurmurHashCombiner to improve clarity in
// callsites to not have to instantiate the class, call Combine() then digest().
template <typename... Args>
uint64_t MurmurHashCombine(const Args&... value) {
  return MurmurHashCombiner::Combine(MurmurHashCombiner{}, value...).digest();
}

// Simple wrapper function to compute a hash value for a single value.
// This is the primitive hash operation that MurmurHash<T> delegates to.
//
// For built-in types (integers, floats, strings), this uses a fast path that
// avoids the overhead of the MurmurHashCombiner. For custom types, it delegates
// to MurmurHashCombiner which will use ADL to find the PerfettoHashValue.
template <typename T>
uint64_t MurmurHashValue(const T& value) {
  if constexpr (murmur_internal::HasMurmurHashBuiltinValue<T>()) {
    return murmur_internal::MurmurHashBuiltinValue(value);
  } else {
    return MurmurHashCombine(value);
  }
}

// std::hash<T> drop-in class which uses MurmurHashValue as the primitive.
// All specializations consistently delegate to MurmurHashValue.
template <typename T>
struct MurmurHash {
  using is_transparent = void;

  uint64_t operator()(const T& value) const { return MurmurHashValue(value); }

  // Heterogeneous lookup support. Only allowed for types where it makes sense
  // (e.g. string-like types and convertible integral types).
  template <typename U>
  auto operator()(const U& value) const
      -> std::enable_if_t<murmur_internal::AllowsHeterogeneousLookup<T, U>(),
                          uint64_t> {
    return MurmurHashValue(value);
  }
};

}  // namespace perfetto::base

#endif  // INCLUDE_PERFETTO_EXT_BASE_MURMUR_HASH_H_

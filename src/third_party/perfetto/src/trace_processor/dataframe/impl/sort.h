/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_SORT_H_
#define SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_SORT_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <vector>

#include "perfetto/ext/base/endian.h"

namespace perfetto::base {
namespace internal {

// Extracts a radix of `kRadixBits` from `key` at `byte_offset`.
template <uint32_t kRadixBits>
inline uint32_t GetRadix(const uint8_t* key, size_t byte_offset) {
  if constexpr (kRadixBits == 8) {
    return key[byte_offset];
  }
  if constexpr (kRadixBits == 16) {
    uint16_t radix;
    memcpy(&radix, key + byte_offset, sizeof(uint16_t));
    // This is important: the input to `RadixSort` is always in big-endian
    // format, so if we need to extract a 16-bit radix, we must convert it
    // to host byte order.
    return base::BE16ToHost(radix);
  }
}

// Performs a single pass of counting sort for the radix at `byte_offset`.
// This is a stable sort.
//
// The algorithm consists of three main steps:
// 1. Counting: Iterate through the source data and count the occurrences of
//    each unique radix value. The radix is a `kRadixBits`-sized portion of the
//    key at a specific `byte_offset`.
// 2. Cumulative Sum: Transform the counts array into a cumulative sum array.
//    Each element at index `i` will then represent the starting position in the
//    destination array for all elements with radix `i`.
// 3. Distribution: Iterate through the source data again. For each element,
//    use the cumulative counts array to find its correct position in the
//    destination array and place it there. The count for that radix is then
//    incremented to ensure that the next element with the same radix is placed
//    at the next position, maintaining stability.
template <uint32_t kRadixBits, typename T, typename KeyExtractor>
void CountingSortPass(T* source_begin,
                      T* source_end,
                      T* dest_begin,
                      size_t byte_offset,
                      KeyExtractor key_extractor,
                      uint32_t* counts) {
  constexpr uint32_t kRadixSize = 1u << kRadixBits;

  // 1. Count frequencies of each radix value.
  memset(counts, 0, kRadixSize * sizeof(uint32_t));
  for (T* it = source_begin; it != source_end; ++it) {
    const uint8_t* key = key_extractor(*it);
    counts[GetRadix<kRadixBits>(key, byte_offset)]++;
  }

  // 2. Calculate cumulative counts to determine positions. Each entry
  // `counts[i]` will store the starting index for elements with radix `i`.
  uint32_t total = 0;
  for (uint32_t i = 0; i < kRadixSize; ++i) {
    uint32_t old_count = counts[i];
    counts[i] = total;
    total += old_count;
  }

  // 3. Place elements into the destination buffer in sorted order. By reading
  // from `source_begin` and writing to `dest_begin` at the calculated
  // positions, we preserve the relative order of equal elements, making this
  // sort stable.
  for (T* it = source_begin; it != source_end; ++it) {
    const uint8_t* key = key_extractor(*it);
    uint32_t& pos = counts[GetRadix<kRadixBits>(key, byte_offset)];
    dest_begin[pos++] = *it;
  }
}

}  // namespace internal

// Sorts a collection of elements using a stable, in-place, Least Significant
// Digit (LSD) radix sort. This implementation is designed for fixed-width,
// unsigned integer keys.
//
// The algorithm works by sorting the elements based on their keys, one "radix"
// (a chunk of bits) at a time, starting from the least significant part of the
// key and moving to the most significant. Each sorting pass uses a stable
// counting sort, which is essential for the correctness of the overall radix
// sort.
//
// A "ping-pong" buffering strategy is employed to optimize performance. Instead
// of copying data back to the original buffer after each pass, the roles of the
// source and destination buffers are swapped. This minimizes data movement. The
// function returns a pointer to the buffer that contains the final sorted data.
//
// The sort processes the key in 16-bit chunks for efficiency. If the key width
// is not a multiple of 2 bytes, a final 8-bit pass is performed on the most
// significant byte.
//
// Stability: This sort is stable. The relative order of elements with equal
// keys is preserved. This is guaranteed because each pass uses a stable
// counting sort.
//
// @param begin Pointer to the first element of the collection to be sorted.
// @param end Pointer to one past the last element of the collection.
// @param scratch_begin Pointer to a buffer of at least `end - begin` elements,
// used as scratch space.
// @param counts A buffer of at least `1 << 16` elements, used by counting sort.
// Passed in by the user to allow for allocation caching.
// @param key_width The width of the keys in bytes.
// @param key_extractor A functor that takes an element and returns a pointer to
// the key data.
// @return A pointer to the beginning of the sorted collection, which will be
// either `begin` or `scratch_begin`.
template <typename T, typename KeyExtractor>
T* RadixSort(T* begin,
             T* end,
             T* scratch_begin,
             uint32_t* counts,
             size_t key_width,
             KeyExtractor key_extractor) {
  static_assert(std::is_trivially_copyable_v<T>,
                "T must be trivially copyable for radix sort to work.");

  T* source = begin;
  T* dest = scratch_begin;
  size_t num_elements = static_cast<size_t>(end - begin);

  // Early return for small number of elements.
  if (num_elements <= 1) {
    return source;
  }

  // Process the key from least significant to most significant bytes.
  int64_t remaining = static_cast<int64_t>(key_width);
  // Process in 16-bit (2-byte) chunks for as long as possible.
  while (remaining >= 2) {
    size_t byte_offset = static_cast<size_t>(remaining - 2);
    internal::CountingSortPass<16>(source, source + num_elements, dest,
                                   byte_offset, key_extractor, counts);
    // Swap buffers for the next pass.
    std::swap(source, dest);
    remaining -= 2;
  }
  // If there's a remaining byte, process it in a final 8-bit pass.
  if (remaining == 1) {
    internal::CountingSortPass<8>(source, source + num_elements, dest, 0,
                                  key_extractor, counts);
    std::swap(source, dest);
  }
  // The `source` pointer now points to the buffer with the sorted data.
  return source;
}

// Sorts a collection of elements using a Most Significant Digit (MSD) radix
// sort. This implementation is particularly well-suited for sorting elements
// with variable-length string keys.
//
// The algorithm operates by partitioning the data into buckets based on the
// most significant character of their keys. It then recursively sorts each
// bucket based on the next character. This "divide and conquer" approach is
// managed iteratively using an explicit stack (`WorkItem`) to avoid deep
// recursion and potential stack overflow.
//
// For performance, a cutoff (`kMsdInsertionSortCutoff`) is used. When a bucket
// becomes smaller than this threshold, the algorithm switches to a standard
// comparison-based sort (`std::sort`), which is more efficient for small
// collections.
//
// To sort a sub-array, the data is first copied into a scratch buffer.
// The elements are then written from the scratch buffer back into the original
// buffer in sorted order. This ensures that the `begin` buffer always
// contains the partially sorted data and the final result is in-place.
//
// Stability: This sort is NOT stable. The relative order of elements with
// equal keys is not guaranteed to be preserved. This is because the
// partitioning process can reorder equal elements, and the `std::sort` used
// for small buckets is also not guaranteed to be stable.
//
// @param begin Pointer to the first element of the collection to be sorted.
// @param end Pointer to one past the last element of the collection.
// @param scratch_begin Pointer to a buffer of at least `end - begin` elements,
// used as scratch space.
// @param string_extractor A functor that takes an element and returns a
// `std::string_view` representing the sort key.
// @return A pointer to the beginning of the sorted collection, which will
// always be `begin`.
template <typename T, typename StringExtractor>
T* MsdRadixSort(T* begin,
                T* end,
                T* scratch_begin,
                StringExtractor string_extractor) {
  static_assert(std::is_trivially_copyable_v<T>,
                "T must be trivially copyable for radix sort to work.");

  if (end - begin <= 1) {
    return begin;
  }

  // WorkItem represents a sub-array to be sorted.
  struct WorkItem {
    T* begin;
    T* end;
    size_t depth;  // Current character index to sort by.
  };
  std::vector<WorkItem> stack;
  stack.push_back({begin, end, 0});

  while (!stack.empty()) {
    WorkItem item = stack.back();
    stack.pop_back();

    size_t item_size = static_cast<size_t>(item.end - item.begin);

    // A cutoff for switching to std::sort; for very small counts, insertion
    // sort will be optimal (that's what std::sort will do under the hood).
    //
    // Empirically chosen by changing the value and measuring the impact on
    // the benchmark `BM_DataframeSortMsdRadix`.
    static constexpr size_t kStdSortCutoff = 24;
    if (item_size <= kStdSortCutoff) {
      std::sort(item.begin, item.end, [&](const T& a, const T& b) {
        return string_extractor(a).substr(item.depth) <
               string_extractor(b).substr(item.depth);
      });
      continue;
    }

    // --- Distribution pass (similar to counting sort) ---
    // Copy the current chunk to the scratch buffer to read from it.
    ptrdiff_t item_offset = item.begin - begin;
    T* scratch_chunk_begin = scratch_begin + item_offset;
    memcpy(scratch_chunk_begin, item.begin, item_size * sizeof(T));

    // 1. Count frequencies of each character at the current depth.
    // Index 0 is for strings that are shorter than the current depth.
    size_t counts[257] = {};
    for (T* it = scratch_chunk_begin; it != scratch_chunk_begin + item_size;
         ++it) {
      std::string_view key = string_extractor(*it);
      counts[item.depth < key.size() ? key[item.depth] + 1 : 0]++;
    }

    // 2. Calculate cumulative counts to determine bucket boundaries.
    size_t total = 0;
    for (size_t& count : counts) {
      size_t old_count = count;
      count = total;
      total += old_count;
    }

    // 3. Place elements from scratch back into the main buffer based on
    // character.
    for (T* it = scratch_chunk_begin; it != scratch_chunk_begin + item_size;
         ++it) {
      std::string_view key = string_extractor(*it);
      size_t& pos = counts[item.depth < key.size() ? key[item.depth] + 1 : 0];
      item.begin[pos++] = *it;
    }

    // Push new work items for each bucket onto the stack for the next level.
    // We iterate backwards to process buckets for smaller characters first.
    for (ptrdiff_t i = 255; i >= 0; --i) {
      T* bucket_begin = item.begin + counts[i];
      T* bucket_end = item.begin + counts[i + 1];
      if (bucket_end - bucket_begin > 1) {
        stack.push_back({bucket_begin, bucket_end, item.depth + 1});
      }
    }
  }
  return begin;
}

}  // namespace perfetto::base

#endif  // SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_SORT_H_

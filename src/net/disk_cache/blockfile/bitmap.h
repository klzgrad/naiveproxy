// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DISK_CACHE_BLOCKFILE_BITMAP_H_
#define NET_DISK_CACHE_BLOCKFILE_BITMAP_H_

#include <stdint.h>
#include <string.h>

#include <algorithm>

#include "base/check.h"
#include "base/compiler_specific.h"
#include "base/containers/heap_array.h"
#include "base/containers/span.h"
#include "base/memory/raw_span.h"
#include "net/base/net_export.h"

namespace disk_cache {

// Requires `in` to be properly aligned and have size divisible by 4 as
// precondition.
NET_EXPORT_PRIVATE base::span<uint32_t> ToUint32Span(base::span<uint8_t> in);

// This class provides support for simple maps of bits.
// The memory for the bitmap may be owned by the consumer of the class,
// or by the instance of the class itself.
class NET_EXPORT_PRIVATE Bitmap {
 public:
  Bitmap();

  // This constructor will allocate on a uint32_t boundary. If |clear_bits| is
  // false, the bitmap bits will not be initialized.
  // The memory for the bitmap will be owned by the instance of the class.
  Bitmap(int num_bits, bool clear_bits);

  // Constructs a Bitmap with the actual storage provided by the caller. |map|
  // has to be valid until this object destruction. |num_bits| is the number of
  // bits in the bitmap.
  Bitmap(base::span<uint32_t> map, size_t num_bits);

  Bitmap(const Bitmap&) = delete;
  Bitmap& operator=(const Bitmap&) = delete;

  ~Bitmap();

  // Resizes the bitmap.
  // If |num_bits| < Size(), the extra bits will be discarded.
  // If |num_bits| > Size(), the extra bits will be filled with zeros if
  // |clear_bits| is true.
  void Resize(int num_bits, bool clear_bits);

  // Returns the number of bits in the bitmap.
  int Size() const { return num_bits_; }

  // Returns the number of 32-bit words in the bitmap.
  int ArraySize() const { return map_.size(); }

  // Sets all the bits to true or false.
  void SetAll(bool value) {
    std::ranges::fill(map_, (value ? 0xFFFFFFFFu : 0x00u));
  }

  // Clears all bits in the bitmap
  void Clear() { SetAll(false); }

  // Sets the value, gets the value or toggles the value of a given bit.
  void Set(int index, bool value);
  bool Get(int index) const;
  void Toggle(int index);

  // Directly sets an element of the internal map. Requires |array_index| <
  // ArraySize();
  void SetMapElement(int array_index, uint32_t value);

  // Gets an entry of the internal map. Requires array_index <
  // ArraySize()
  uint32_t GetMapElement(int array_index) const;

  // Directly sets the internal map by copying values from `map`.
  // If `map.size() > array_size()`, it ignores the end of `map`.
  void SetMap(base::span<const uint32_t> map);

  // Gets a span describing the internal map.
  base::span<const uint32_t> GetSpan() const { return map_; }

  // Sets a range of bits to |value|.
  void SetRange(int begin, int end, bool value);

  // Returns true if any bit between begin inclusive and end exclusive is set.
  // 0 <= |begin| <= |end| <= Size() is required.
  bool TestRange(int begin, int end, bool value) const;

  // Scans bits starting at bit *|index|, looking for a bit set to |value|. If
  // it finds that bit before reaching bit index |limit|, sets *|index| to the
  // bit index and returns true. Otherwise returns false.
  // Requires |limit| <= Size().
  //
  // Note that to use these methods in a loop you must increment the index
  // after each use, as in:
  //
  //  for (int index = 0 ; map.FindNextBit(&index, limit, value) ; ++index) {
  //    DoSomethingWith(index);
  //  }
  bool FindNextBit(int* index, int limit, bool value) const;

  // Finds the first offset >= *|index| and < |limit| that has its bit set.
  // See FindNextBit() for more info.
  bool FindNextSetBitBeforeLimit(int* index, int limit) const {
    return FindNextBit(index, limit, true);
  }

  // Finds the first offset >= *|index| that has its bit set.
  // See FindNextBit() for more info.
  bool FindNextSetBit(int *index) const {
    return FindNextSetBitBeforeLimit(index, num_bits_);
  }

  // Scans bits starting at bit *|index|, looking for a bit set to |value|. If
  // it finds that bit before reaching bit index |limit|, sets *|index| to the
  // bit index and then counts the number of consecutive bits set to |value|
  // (before reaching |limit|), and returns that count. If no bit is found
  // returns 0. Requires |limit| <= Size().
  int FindBits(int* index, int limit, bool value) const;

  // Returns number of allocated words required for a bitmap of size |num_bits|.
  static size_t RequiredArraySize(size_t num_bits) {
    // Force at least one allocated word.
    if (num_bits <= kIntBits)
      return 1;

    return (num_bits + kIntBits - 1) >> kLogIntBits;
  }

  // Gets a span to the internal map memory.
  base::span<const uint8_t> GetMapForTesting() const {
    return base::as_bytes(map_);
  }
  bool HasAllocatedMapForTesting() { return !allocated_map_.empty(); }

 private:
  static const int kIntBits = sizeof(uint32_t) * 8;
  static const int kLogIntBits = 5;  // 2^5 == 32 bits per word.

  // Sets |len| bits from |start| to |value|. All the bits to be set should be
  // stored in the same word, and len < kIntBits.
  void SetWordBits(int start, int len, bool value);

  int num_bits_ = 0;    // The upper bound of the bitmap.
  base::HeapArray<uint32_t> allocated_map_;  // The allocated data.
  base::raw_span<uint32_t> map_;             // The bitmap.
};

}  // namespace disk_cache

#endif  // NET_DISK_CACHE_BLOCKFILE_BITMAP_H_

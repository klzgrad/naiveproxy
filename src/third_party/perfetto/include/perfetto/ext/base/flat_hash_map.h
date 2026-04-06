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

#ifndef INCLUDE_PERFETTO_EXT_BASE_FLAT_HASH_MAP_H_
#define INCLUDE_PERFETTO_EXT_BASE_FLAT_HASH_MAP_H_

#include "perfetto/base/build_config.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/bits.h"
#include "perfetto/ext/base/flat_hash_map_v1.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/public/compiler.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#if PERFETTO_BUILDFLAG(PERFETTO_X64_CPU_OPT)
#include <immintrin.h>
#endif

namespace perfetto::base {

// A Swiss Table-style open-addressing hashmap implementation.
// Inspired by absl::flat_hash_map, this uses a metadata array of control bytes
// to enable fast SIMD-accelerated probing.
//
// Key design choices:
// - Control bytes: Each slot has a 1-byte tag (7-bit H2 hash or special marker)
//   stored in a separate array, enabling fast group-based matching.
// - SIMD acceleration: On x64, uses SSE instructions to match 16 control bytes
//   in parallel. Falls back to SWAR (SIMD Within A Register) on other platforms
//   matching 8 bytes at a time.
// - Triangular probing: Uses the sequence 0, 16, 48, 96, ... (like Absl) to
//   probe groups of slots, ensuring good cache behavior.
// - Pointers are NOT stable: Neither keys nor values have stable addresses
//   across insertions that trigger rehashing.
//
// See also: FlatHashMapV1 in flat_hash_map_v1.h for the older implementation
// using traditional open-addressing with configurable probing strategies.

// Non-templated base class to hold helpers for FlatHashMapV2.
namespace flat_hash_map_v2_internal {

// Helper to detect if a hasher has is_transparent defined.
template <typename, typename = void>
struct HasIsTransparent : std::false_type {};

template <typename H>
struct HasIsTransparent<H, std::void_t<typename H::is_transparent>>
    : std::true_type {};

// Equality comparator trait.
template <typename T>
struct HashEq : public std::equal_to<T> {};

// Specialization for std::string to compare via std::string_view.
//
// This exists because after benchmarking, it turns out libc++ has an
// "optimization" for std::string equality that does something byte by
// byte comparision for short strings but this is slower than just memcmp.
//
// This helps close the gap to absl::flat_hash_map for string keys.
template <>
struct HashEq<std::string> {
  bool operator()(const std::string& a, const std::string& b) const {
    return std::string_view(a) == std::string_view(b);
  }
  bool operator()(const std::string& a, const std::string_view& b) const {
    return std::string_view(a) == b;
  }
  bool operator()(const std::string& a, base::StringView b) const {
    return base::StringView(a) == b;
  }
};

// Helper to check if a lookup key type K is allowed.
// Returns true if:
// 1. K can be implicitly converted to Key, OR
// 2. Hasher has is_transparent AND Hasher is invocable with K AND Key and K
// are equality comparable
template <typename K, typename Key, typename Hasher>
static constexpr bool IsLookupKeyAllowed() {
  if constexpr (HasIsTransparent<Hasher>::value) {
    return std::is_invocable_v<Hasher, const K&> &&
           std::is_same_v<decltype(std::declval<const Key&>() ==
                                   std::declval<const K&>()),
                          bool>;
  } else if constexpr (std::is_convertible_v<K, Key>) {
    return true;
  } else {
    return false;
  }
}

// Swiss Table control byte encoding:
// - Empty:   0x80 (10000000) - MSB set, easy to detect with sign bit
// - Deleted: 0xFE (11111110) - MSB set
// - Full:    0x00-0x7F - MSB clear, stores 7-bit H2 hash
static constexpr uint8_t kFreeSlot = 0x80;   // Empty slot
static constexpr uint8_t kTombstone = 0xFE;  // Deleted slot

// The default load limit percent before growing the table.
static constexpr int kDefaultLoadLimitPct = 75;

}  // namespace flat_hash_map_v2_internal

template <typename Key,
          typename Value,
          typename Hasher = base::MurmurHash<Key>,
          typename Eq = flat_hash_map_v2_internal::HashEq<Key>>
class FlatHashMapV2 {
 private:
  // Import constants from internal namespace.
  static constexpr uint8_t kFreeSlot = flat_hash_map_v2_internal::kFreeSlot;
  static constexpr uint8_t kTombstone = flat_hash_map_v2_internal::kTombstone;
  static constexpr int kDefaultLoadLimitPct =
      flat_hash_map_v2_internal::kDefaultLoadLimitPct;

  // Slot structure holds both key and value
  struct Slot {
    Key key;
    Value value;
  };

 public:
  class Iterator {
   public:
    explicit Iterator(const uint8_t* ctrl, const uint8_t* ctrl_end, Slot* slot)
        : ctrl_(ctrl), ctrl_end_(ctrl_end), slot_(slot) {
      FindNextNonFree();
    }
    ~Iterator() = default;
    Iterator(const Iterator&) = default;
    Iterator& operator=(const Iterator&) = default;
    Iterator(Iterator&&) noexcept = default;
    Iterator& operator=(Iterator&&) noexcept = default;

    const Key& key() { return slot_->key; }
    Value& value() { return slot_->value; }
    const Key& key() const { return slot_->key; }
    const Value& value() const { return slot_->value; }

    explicit operator bool() const { return ctrl_ != ctrl_end_; }
    Iterator& operator++() {
      PERFETTO_DCHECK(ctrl_ != ctrl_end_);
      ++ctrl_;
      ++slot_;
      FindNextNonFree();
      return *this;
    }

   private:
    void FindNextNonFree() {
      for (; ctrl_ != ctrl_end_; ++ctrl_, ++slot_) {
        const uint8_t cur_ctrl = *ctrl_;
        if (cur_ctrl != kFreeSlot && cur_ctrl != kTombstone)
          return;
      }
    }
    const uint8_t* ctrl_ = nullptr;
    const uint8_t* ctrl_end_ = nullptr;
    Slot* slot_ = nullptr;
  };  // Iterator

  explicit FlatHashMapV2(size_t initial_capacity = 0,
                         int load_limit_pct = kDefaultLoadLimitPct)
      : load_limit_percent_(load_limit_pct) {
    if (initial_capacity > 0) {
      Reset(initial_capacity, true);
    }
  }

  // We are calling Clear() so that the destructors for the inserted entries are
  // called (unless they are trivial, in which case it will be a no-op).
  ~FlatHashMapV2() { Clear(); }

  FlatHashMapV2(FlatHashMapV2&& other) noexcept
      : storage_(std::move(other.storage_)),
        capacity_(other.capacity_),
        size_(other.size_),
        growth_info_(other.growth_info_),
        load_limit_percent_(other.load_limit_percent_),
        ctrl_(other.ctrl_),
        slots_(other.slots_) {
    new (&other) FlatHashMapV2();
  }

  FlatHashMapV2& operator=(FlatHashMapV2&& other) noexcept {
    this->~FlatHashMapV2();
    new (this) FlatHashMapV2(std::move(other));
    return *this;
  }

  FlatHashMapV2(const FlatHashMapV2&) = delete;
  FlatHashMapV2& operator=(const FlatHashMapV2&) = delete;

  template <typename K = Key>
  PERFETTO_ALWAYS_INLINE Value* Find(const K& key) const {
    size_t key_hash = Hasher{}(key);
    uint8_t h2 = H2(key_hash);
    FindResult res = FindSlotIgnoringTombstones<false>(key, key_hash, h2);
    if (PERFETTO_UNLIKELY(res.needs_insert)) {
      return nullptr;
    }
    return &slots_[res.idx].value;
  }

  template <typename K = Key>
  bool Erase(const K& key) {
    size_t key_hash = Hasher{}(key);
    uint8_t h2 = H2(key_hash);
    FindResult res = FindSlotIgnoringTombstones<false>(key, key_hash, h2);
    if (PERFETTO_UNLIKELY(res.needs_insert)) {
      return false;
    }
    PERFETTO_DCHECK(size_ > 0);
    SetCtrl(res.idx, kTombstone);
    slots_[res.idx].key.~Key();
    slots_[res.idx].value.~Value();
    size_--;
    growth_info_.has_tombstones = 1;
    return true;
  }

  PERFETTO_ALWAYS_INLINE std::pair<Value*, bool> Insert(Key key, Value value) {
    size_t key_hash = Hasher{}(key);
    uint8_t h2 = H2(key_hash);
    FindResult res = FindSlotIgnoringTombstones<true>(key, key_hash, h2);
    if (PERFETTO_UNLIKELY(!res.needs_insert)) {
      return {&slots_[res.idx].value, false};
    }
    if (PERFETTO_UNLIKELY(growth_info_.growth_left == 0)) {
      GrowAndRehash();
      // After rehash, table has no tombstones. Find an empty slot directly
      // instead of recursing (which would prevent inlining).
      res.idx = FindFirstEmptyOrTombstone(key_hash);
    }
    PERFETTO_DCHECK(res.idx != kNotFound);
    size_t insert_idx = res.idx;
    bool is_freeslot = true;
    if (PERFETTO_UNLIKELY(growth_info_.has_tombstones)) {
      insert_idx = FindFirstEmptyOrTombstone(key_hash);
      is_freeslot = ctrl_[insert_idx] != kTombstone;
    }
    new (&slots_[insert_idx].key) Key(std::move(key));
    new (&slots_[insert_idx].value) Value(std::move(value));
    SetCtrl(insert_idx, h2);
    size_++;
    if (is_freeslot) {
      growth_info_.growth_left--;
    }
    return {&slots_[insert_idx].value, true};
  }

  Value& operator[](Key key) {
    auto it_and_inserted = Insert(std::move(key), Value{});
    return *it_and_inserted.first;
  }

  void Clear() {
    // Avoid trivial heap operations on zero-capacity std::move()-d objects.
    if (PERFETTO_UNLIKELY(capacity_ == 0)) {
      return;
    }
    for (size_t i = 0; i < capacity_; ++i) {
      const uint8_t tag = ctrl_[i];
      if (tag == kFreeSlot || tag == kTombstone) {
        continue;
      }
      slots_[i].key.~Key();
      slots_[i].value.~Value();
    }
    Reset(capacity_, false);
  }

  Iterator GetIterator() { return Iterator(ctrl_, ctrl_ + capacity_, slots_); }
  Iterator GetIterator() const {
    return Iterator(ctrl_, ctrl_ + capacity_, slots_);
  }

  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }

 private:
  // Result struct for FindOrPrepareInsert - avoids bit manipulation overhead
  struct FindResult {
    uint64_t idx : 63;
    uint64_t needs_insert : 1;
  };

  // Tracks growth capacity and whether any deletions have occurred.
  // Using bitfields like absl's GrowthInfo to avoid manual bit manipulation.
  struct GrowthInfo {
    uint64_t growth_left : 63;
    uint64_t has_tombstones : 1;
  };

  // Not found sentinel (must fit in 63-bit FindResult.idx)
  static constexpr size_t kNotFound = std::numeric_limits<size_t>::max() >> 1;

  // Abstraction over a group of control bytes that enables batch operations.
  // On x64, uses SSE to match 16 control bytes in parallel.
  // On other platforms, uses SWAR (SIMD Within A Register) for 8 bytes.
  //
  // Match() returns an iterator over slots whose control byte matches h2.
  // MatchEmpty() returns an iterator over empty slots (kFreeSlot).
  // MatchEmptyOrDeleted() returns an iterator over empty or deleted slots.
#if PERFETTO_BUILDFLAG(PERFETTO_X64_CPU_OPT)
  struct Group {
   public:
    // Group size 16 for x64 SSE
    static constexpr size_t kSize = 16;

    // Iterates over set bits in a match mask. Each set bit indicates a slot
    // in the group that matched the search criteria. Call Next() to get the
    // index of each matching slot.
    struct Iterator {
     public:
      PERFETTO_ALWAYS_INLINE explicit Iterator(uint16_t mask) : mask_(mask) {}
      PERFETTO_ALWAYS_INLINE explicit operator bool() const { return mask_; }

      PERFETTO_ALWAYS_INLINE size_t Next() {
        auto idx = static_cast<size_t>(CountTrailZeros(mask_));
        mask_ &= static_cast<uint16_t>(mask_ - 1);
        return idx;
      }

     private:
      uint16_t mask_;
    };

    PERFETTO_ALWAYS_INLINE explicit Group(const uint8_t* pos) {
      ctrl_ = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pos));
    }

    PERFETTO_ALWAYS_INLINE Iterator Match(uint8_t h2) const {
      auto match = _mm_cmpeq_epi8(ctrl_, _mm_set1_epi8(static_cast<char>(h2)));
      return Iterator(static_cast<uint16_t>(_mm_movemask_epi8(match)));
    }

    PERFETTO_ALWAYS_INLINE Iterator MatchEmpty() const {
      return Iterator(static_cast<uint16_t>(
          _mm_movemask_epi8(_mm_sign_epi8(ctrl_, ctrl_))));
    }

    PERFETTO_ALWAYS_INLINE Iterator MatchEmptyOrDeleted() const {
      return Iterator(static_cast<uint16_t>(_mm_movemask_epi8(ctrl_)));
    }

   private:
    __m128i ctrl_;
  };
#else
  // SWAR fallback: processes 8 control bytes at a time using 64-bit arithmetic.
  struct Group {
   public:
    // Group size 8 for ARM and other platforms
    static constexpr size_t kSize = 8;

    // Iterates over set bits in a sparse 64-bit mask. Each set MSB indicates
    // a matching byte in the group. Call Next() to get the index of each
    // matching slot.
    struct Iterator {
     public:
      PERFETTO_ALWAYS_INLINE explicit Iterator(uint64_t mask) : mask_(mask) {}
      PERFETTO_ALWAYS_INLINE explicit operator bool() const { return mask_; }
      PERFETTO_ALWAYS_INLINE size_t Next() {
        // Count zeros and divide by 8 (shift 3)
        // 0x80 (Byte 0) -> CTZ 7  -> 7>>3 = 0
        // 0x8000 (Byte 1) -> CTZ 15 -> 15>>3 = 1
        size_t idx = static_cast<size_t>(CountTrailZeros(mask_) >> 3);
        mask_ &= mask_ - 1;  // Clear lowest set bit
        return idx;
      }

     private:
      uint64_t mask_;
    };

    PERFETTO_ALWAYS_INLINE explicit Group(const uint8_t* pos) {
      memcpy(&ctrl_, pos, sizeof(ctrl_));
    }

    PERFETTO_ALWAYS_INLINE Iterator Match(uint8_t h2) const {
      uint64_t x = ctrl_ ^ (kLsbs * h2);
      return Iterator((x - kLsbs) & ~x & kMsbs);
    }

    PERFETTO_ALWAYS_INLINE Iterator MatchEmpty() const {
      // 0x80 check (Empty)
      return Iterator((ctrl_ & ~(ctrl_ << 6)) & kMsbs);
    }

    PERFETTO_ALWAYS_INLINE Iterator MatchEmptyOrDeleted() const {
      // 0x80 or 0xFE check (Empty or Deleted)
      return Iterator((ctrl_ & ~(ctrl_ << 7)) & kMsbs);
    }

   private:
    static constexpr uint64_t kLsbs = 0x0101010101010101ULL;
    static constexpr uint64_t kMsbs = 0x8080808080808080ULL;

    uint64_t ctrl_;
  };
#endif

  // The number of cloned control bytes after the main control byte array.
  static constexpr size_t kNumClones = Group::kSize - 1;

  // Searches for a key in the table. This function IGNORES tombstones during
  // the search - it only stops at empty slots (kFreeSlot) or matching keys.
  //
  // Why ignore tombstones? In Swiss Tables, tombstones mark deleted entries but
  // must be skipped during lookup because the key we're searching for may have
  // been inserted AFTER the tombstone was created (i.e., the key's probe
  // sequence may have skipped over that tombstone). Only an empty slot
  // definitively proves the key doesn't exist.
  //
  // Returns FindResult with idx and whether the key needs to be inserted:
  // - If key is found: {idx, false} where idx is the slot containing the key.
  // - If key not found and ForInsert=true: {empty_idx, true} where empty_idx
  //   is the index of the first EMPTY slot encountered.
  // - If key not found and ForInsert=false: {kNotFound, true}.
  //
  // IMPORTANT for insertion (ForInsert=true): The returned empty_idx is NOT
  // necessarily the best slot to insert into! There may be an earlier tombstone
  // in the probe sequence that should be reused to avoid wasting slots. When
  // has_tombstones is set, the caller must make a SECOND pass by calling
  // FindFirstEmptyOrTombstone() to find the actual insertion slot.
  template <bool ForInsert, typename K = Key>
  PERFETTO_ALWAYS_INLINE FindResult
  FindSlotIgnoringTombstones(const K& key, size_t key_hash, uint8_t h2) const {
    static_assert(
        flat_hash_map_v2_internal::IsLookupKeyAllowed<K, Key, Hasher>(),
        "Heterogeneous lookup requires Hasher to define is_transparent and "
        "support hashing the lookup key type. For same-type lookup, Key and K "
        "must match exactly.");

    if (PERFETTO_UNLIKELY(ctrl_ == nullptr)) {
      return {kNotFound, true};
    }

    const size_t cap_mask = capacity_ - 1;
    size_t offset = H1(key_hash) & cap_mask;
    size_t probe_size = 0;
    const uint8_t* ctrl = ctrl_;

    // Prefetch control bytes (like Absl's prefetch_heap_block).
    // Use locality hint 3 (high temporal locality) for better L1 cache usage.
    __builtin_prefetch(ctrl_ + offset, 0, 3);

    while (true) {
      // Prefetch slots at current probe offset (like Absl).
      __builtin_prefetch(slots_ + offset, 0, 3);

      Group group(ctrl + offset);

      // Match H2 tags in this group.
      for (auto it = group.Match(h2); PERFETTO_LIKELY(it);) {
        // Must mask because offset + it.Next() can exceed capacity when
        // group straddles the table boundary (using cloned control bytes).
        size_t idx = (offset + it.Next()) & cap_mask;
        if (PERFETTO_LIKELY(Eq{}(slots_[idx].key, key))) {
          return {idx, false};  // Found
        }
      }

      // Check for empty slot (NOT tombstones). If we find an empty slot, the
      // key cannot exist in the table (empty slots terminate probe chains).
      if (auto it = group.MatchEmpty(); PERFETTO_LIKELY(it)) {
        if constexpr (ForInsert) {
          size_t empty_idx = (offset + it.Next()) & cap_mask;
          return {empty_idx, true};  // Not found - empty slot for insertion
        } else {
          return {kNotFound, true};  // Not found - no need to compute slot
        }
      }

      // Triangular probing (like Absl): 0, 16, 48, 96, ...
      probe_size += Group::kSize;
      offset = (offset + probe_size) & cap_mask;

      // Should never happen with load limit.
      PERFETTO_DCHECK(probe_size <= capacity_);
    }
  }

  // Find first empty OR tombstone slot for insertion.
  // Called when has_tombstones is set to find an earlier tombstone that can
  // be reused instead of taking a new empty slot.
  size_t FindFirstEmptyOrTombstone(size_t key_hash) const {
    const size_t cap_mask = capacity_ - 1;
    size_t offset = H1(key_hash) & cap_mask;
    size_t probe_size = 0;
    while (true) {
      Group group(ctrl_ + offset);
      if (auto it = group.MatchEmptyOrDeleted(); PERFETTO_LIKELY(it)) {
        return (offset + it.Next()) & cap_mask;
      }
      probe_size += Group::kSize;
      offset = (offset + probe_size) & cap_mask;
    }
  }

  PERFETTO_NO_INLINE void GrowAndRehash() {
    // Grow factor must be a power of 2 because probing uses bitwise AND
    // for modulo arithmetic (capacity must remain a power of 2).
    static constexpr size_t kGrowFactor = 2;
    static_assert((kGrowFactor & (kGrowFactor - 1)) == 0,
                  "kGrowFactor must be a power of 2");

    PERFETTO_DCHECK(size_ <= capacity_);

    size_t old_capacity = capacity_;
    size_t old_size = size_;
    uint8_t* old_ctrl = ctrl_;
    Slot* old_slots = slots_;
    std::unique_ptr<uint8_t[]> old_storage(std::move(storage_));

    // This must be a CHECK (i.e. not just a DCHECK) to prevent UAF attacks on
    // 32-bit archs that try to double the size of the table until wrapping.
    size_t new_capacity = old_capacity * kGrowFactor;
    PERFETTO_CHECK(new_capacity >= old_capacity);
    Reset(new_capacity, true);

    size_t new_size = 0;
    for (size_t i = 0; i < old_capacity; ++i) {
      if (uint8_t t = old_ctrl[i]; t == kFreeSlot || t == kTombstone) {
        continue;
      }
      Insert(std::move(old_slots[i].key), std::move(old_slots[i].value));
      old_slots[i].key.~Key();  // Destroy the old objects.
      old_slots[i].value.~Value();
      new_size++;
    }
    PERFETTO_DCHECK(new_size == old_size);
    size_ = new_size;
  }

  // Doesn't call destructors. Use Clear() for that.
  PERFETTO_NO_INLINE void Reset(size_t n, bool reallocate) {
    // Must be a pow2.
    PERFETTO_CHECK((n & (n - 1)) == 0);

    // Always ensure at least 128 capacity to avoid too frequent growths.
    capacity_ = std::max<size_t>(n, 128u);
    size_ = 0;
    growth_info_.growth_left =
        (capacity_ * static_cast<size_t>(load_limit_percent_)) / 100;
    growth_info_.has_tombstones = 0;

    if (reallocate) {
      // See memory layout comment above |storage_|.
      size_t slots_offset =
          base::AlignUp(capacity_ + kNumClones, alignof(Slot));
      storage_.reset(new uint8_t[slots_offset + (capacity_ * sizeof(Slot))]);
      ctrl_ = storage_.get();
      slots_ = reinterpret_cast<Slot*>(storage_.get() + slots_offset);
    }
    if (ctrl_) {
      // Initialize all control bytes (including clones) to empty (kFreeSlot)
      memset(ctrl_, kFreeSlot, capacity_ + kNumClones);
    }
  }

  // Swiss Table hash splitting (matching absl):
  // H1 = upper bits for bucket index
  // H2 = lower 7 bits for tag
  // This ensures H1 and H2 are independent, avoiding tag collisions within
  // buckets. The seed XOR prevents clustering when hash values have patterns
  // (e.g., sequential keys)
  static constexpr size_t H1(size_t hash) { return (hash >> 7); }
  static constexpr uint8_t H2(size_t hash) { return hash & 0x7F; }

  // Set control byte and update clone if needed
  PERFETTO_ALWAYS_INLINE void SetCtrl(size_t i, uint8_t h) {
    ctrl_[i] = h;
    // Update clone if this is one of the first kNumClones entries
    if (PERFETTO_UNLIKELY(i < kNumClones)) {
      ctrl_[capacity_ + i] = h;
    }
  }

  // Owns the actual memory with the following layout:
  //
  // [Control bytes]
  //   |capacity_| bytes for control bytes.
  //   kNumClones (15 or 7) bytes for control byte clones (*).
  //   No alignment required (accessed at arbitrary byte offsets).
  //
  // [Padding for Slot alignment]
  //
  // [Slots]
  //   capacity_ * sizeof(Slot): contains key-value pairs.
  //   Must be aligned to alignof(Slot).
  //
  // (*) Control byte clones: The first kNumClones control bytes are duplicated
  // at the end of the control array. This allows SIMD operations to read a full
  // group (16 or 8 bytes) starting from any position without bounds checking,
  // even near the end of the array.
  std::unique_ptr<uint8_t[]> storage_;

  size_t capacity_ = 0;
  size_t size_ = 0;

  // Slots remaining + has_deleted flag
  GrowthInfo growth_info_{0, 0};

  // Load factor limit in % of |capacity_|.
  int load_limit_percent_ = kDefaultLoadLimitPct;

  // Cached pointers for fast access (like absl::flat_hash_map)
  // These are updated whenever storage is allocated/reallocated.
  uint8_t* ctrl_ = nullptr;  // Points to control bytes
  Slot* slots_ = nullptr;    // Points to slot array
};

// Alias FlatHashMap to FlatHashMapV1 for backward compatibility.
//
// TODO(lalitm): Once FlatHashMapV2 is fully tested and verified, switch this
// to FlatHashMapV2.
template <typename Key,
          typename Value,
          typename Hasher = MurmurHash<Key>,
          typename Probe = QuadraticProbe,
          bool AppendOnly = false>
using FlatHashMap = FlatHashMapV1<Key, Value, Hasher, Probe, AppendOnly>;

}  // namespace perfetto::base

#endif  // INCLUDE_PERFETTO_EXT_BASE_FLAT_HASH_MAP_H_

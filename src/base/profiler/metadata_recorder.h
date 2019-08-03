// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROFILER_METADATA_RECORDER_H_
#define BASE_PROFILER_METADATA_RECORDER_H_

#include <array>
#include <atomic>
#include <utility>

#include "base/synchronization/lock.h"

namespace base {

// MetadataRecorder provides a data structure to store metadata key/value pairs
// to be associated with samples taken by the sampling profiler. Whatever
// metadata is present in this map when a sample is recorded is then associated
// with the sample.
//
// Methods on this class are safe to call unsynchronized from arbitrary threads.
class BASE_EXPORT MetadataRecorder {
 public:
  MetadataRecorder();
  virtual ~MetadataRecorder();
  MetadataRecorder(const MetadataRecorder&) = delete;
  MetadataRecorder& operator=(const MetadataRecorder&) = delete;

  // Sets a name hash/value pair, overwriting any previous value set for that
  // name hash.
  void Set(uint64_t name_hash, int64_t value);

  // Removes the item with the specified name hash.
  //
  // If such an item does not exist, this has no effect.
  void Remove(uint64_t name_hash);

  struct Item {
    // The hash of the metadata name, as produced by base::HashMetricName().
    uint64_t name_hash;
    // The value of the metadata item.
    int64_t value;
  };

  static const size_t MAX_METADATA_COUNT = 50;
  typedef std::array<Item, MAX_METADATA_COUNT> ItemArray;
  // Retrieves the first |available_slots| items in the metadata recorder and
  // copies them into |items|, returning the number of metadata items that were
  // copied. To ensure that all items can be copied, |available slots| should be
  // greater than or equal to |MAX_METADATA_COUNT|.
  size_t GetItems(ItemArray* const items) const;

 private:
  // TODO(charliea): Support large quantities of metadata efficiently.
  struct ItemInternal {
    ItemInternal();
    ~ItemInternal();

    // Indicates whether the metadata item is still active (i.e. not removed).
    //
    // Requires atomic reads and writes to avoid word tearing when reading and
    // writing unsynchronized. Requires acquire/release semantics to ensure that
    // the other state in this struct is visible to the reading thread before it
    // is marked as active.
    std::atomic<bool> is_active{false};

    // Doesn't need atomicity or memory order constraints because no reader will
    // attempt to read it mid-write. Specifically, readers wait until
    // |is_active| is true to read |name_hash|. Because |is_active| is always
    // stored with a memory_order_release fence, we're guaranteed that
    // |name_hash| will be finished writing before |is_active| is set to true.
    uint64_t name_hash;
    // Requires atomic reads and writes to avoid word tearing when updating an
    // existing item unsynchronized. Does not require acquire/release semantics
    // because we rely on the |is_active| acquire/release semantics to ensure
    // that an item is fully created before we attempt to read it.
    std::atomic<int64_t> value;
  };

  // Metadata items that the recorder has seen. Rather than implementing the
  // metadata recorder as a dense array, we implement it as a sparse array where
  // removed metadata items keep their slot with their |is_active| bit set to
  // false. This avoids race conditions caused by reusing slots that might
  // otherwise cause mismatches between metadata name hashes and values.
  //
  // For the rationale behind this design (along with others considered), see
  // https://docs.google.com/document/d/18shLhVwuFbLl_jKZxCmOfRB98FmNHdKl0yZZZ3aEO4U/edit#.
  std::array<ItemInternal, MAX_METADATA_COUNT> items_;

  // The number of item slots used in the metadata map.
  //
  // Requires atomic reads and writes to avoid word tearing when reading and
  // writing unsynchronized. Requires acquire/release semantics to ensure that a
  // newly-allocated slot is fully initialized before the reader becomes aware
  // of its existence.
  std::atomic<size_t> item_slots_used_{0};

  // A lock that guards against multiple threads trying to modify the same item
  // at once.
  base::Lock write_lock_;
};

}  // namespace base

#endif  // BASE_PROFILER_METADATA_RECORDER_H_

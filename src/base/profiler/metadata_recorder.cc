// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/profiler/metadata_recorder.h"

namespace base {

MetadataRecorder::ItemInternal::ItemInternal() = default;

MetadataRecorder::ItemInternal::~ItemInternal() = default;

MetadataRecorder::MetadataRecorder() {
  // Ensure that we have necessary atomic support.
  DCHECK(items_[0].is_active.is_lock_free());
  DCHECK(items_[0].value.is_lock_free());
}

MetadataRecorder::~MetadataRecorder() = default;

void MetadataRecorder::Set(uint64_t name_hash, int64_t value) {
  base::AutoLock lock(write_lock_);

  // Acquiring the |write_lock_| guarantees that two simultaneous writes don't
  // attempt to create items in the same slot. Use of memory_order_release
  // guarantees that all writes performed by other threads to the metadata items
  // will be seen by the time we reach this point.
  size_t item_slots_used = item_slots_used_.load(std::memory_order_relaxed);
  for (size_t i = 0; i < item_slots_used; ++i) {
    auto& item = items_[i];
    if (item.name_hash == name_hash) {
      item.value.store(value, std::memory_order_relaxed);
      item.is_active.store(true, std::memory_order_release);
      return;
    }
  }

  // There should always be room in this data structure because there are more
  // reserved slots than there are unique metadata names in Chromium.
  DCHECK_NE(item_slots_used, items_.size())
      << "Cannot add a new sampling profiler metadata item to an already full "
         "map.";

  // Wait until the item is fully created before setting |is_active| to true and
  // incrementing |item_slots_used_|, which will signal to readers that the item
  // is ready.
  auto& item = items_[item_slots_used_];
  item.name_hash = name_hash;
  item.value.store(value, std::memory_order_relaxed);
  item.is_active.store(true, std::memory_order_release);
  item_slots_used_.fetch_add(1, std::memory_order_release);
}

void MetadataRecorder::Remove(uint64_t name_hash) {
  base::AutoLock lock(write_lock_);

  size_t item_slots_used = item_slots_used_.load(std::memory_order_relaxed);
  for (size_t i = 0; i < item_slots_used; ++i) {
    auto& item = items_[i];
    if (item.name_hash == name_hash) {
      // A removed item will occupy its slot indefinitely.
      item.is_active.store(false, std::memory_order_release);
    }
  }
}

size_t MetadataRecorder::GetItems(ItemArray* const items) const {
  // TODO(charliea): Defragment the item array if we can successfully acquire
  // the write lock here. This will require either making this function
  // non-const or |items_| mutable.

  // If a writer adds a new item after this load, it will be ignored.  We do
  // this instead of calling item_slots_used_.load() explicitly in the for loop
  // bounds checking, which would be expensive.
  //
  // Also note that items are snapshotted sequentially and that items can be
  // modified mid-snapshot by non-suspended threads. This means that there's a
  // small chance that some items, especially those that occur later in the
  // array, may have values slightly "in the future" from when the sample was
  // actually collected. It also means that the array as returned may have never
  // existed in its entirety, although each name/value pair represents a
  // consistent item that existed very shortly after the thread was supended.
  size_t item_slots_used = item_slots_used_.load(std::memory_order_acquire);
  size_t write_index = 0;
  for (size_t read_index = 0; read_index < item_slots_used; ++read_index) {
    const auto& item = items_[read_index];
    // Because we wait until |is_active| is set to consider an item active and
    // that field is always set last, we ignore half-created items.
    if (item.is_active.load(std::memory_order_acquire)) {
      (*items)[write_index++] =
          Item{item.name_hash, item.value.load(std::memory_order_relaxed)};
    }
  }

  return write_index;
}

}  // namespace base

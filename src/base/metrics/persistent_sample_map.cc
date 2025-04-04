// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/metrics/persistent_sample_map.h"

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <type_traits>

#include "base/check_op.h"
#include "base/containers/contains.h"
#include "base/metrics/histogram_base.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/histogram_samples.h"
#include "base/metrics/persistent_histogram_allocator.h"
#include "base/metrics/sample_map_iterator.h"
#include "base/notreached.h"
#include "build/buildflag.h"

#if !BUILDFLAG(IS_NACL)
#include "base/debug/crash_logging.h"
#endif

namespace base {

using Count32 = HistogramBase::Count32;
using Sample32 = HistogramBase::Sample32;

namespace {

// This structure holds an entry for a PersistentSampleMap within a persistent
// memory allocator. The "id" must be unique across all maps held by an
// allocator or they will get attached to the wrong sample map.
struct SampleRecord {
  // SHA1(SampleRecord): Increment this if structure changes!
  static constexpr uint32_t kPersistentTypeId = 0x8FE6A69F + 1;

  // Expected size for 32/64-bit check.
  static constexpr size_t kExpectedInstanceSize = 16;

  uint64_t id;                 // Unique identifier of owner.
  Sample32 value;              // The value for which this record holds a count.
  std::atomic<Count32> count;  // The count associated with the above value.

  // `count` may operate inter-process and so must be lock-free.
  static_assert(std::atomic<Count32>::is_always_lock_free);

  // For backwards compatibility, `std::atomic<Count>` and `Count` must have
  // the same memory layouts. If this ever changes, make sure to increment
  // `kPersistentTypeId` above.
  static_assert(std::is_standard_layout_v<std::atomic<Count32>>);
  static_assert(sizeof(std::atomic<Count32>) == sizeof(Count32));
  static_assert(alignof(std::atomic<Count32>) == alignof(Count32));
};

}  // namespace

PersistentSampleMap::PersistentSampleMap(
    uint64_t id,
    PersistentHistogramAllocator* allocator,
    Metadata* meta)
    : HistogramSamples(id, meta), allocator_(allocator) {}

PersistentSampleMap::~PersistentSampleMap() = default;

void PersistentSampleMap::Accumulate(Sample32 value, Count32 count) {
  // We have to do the following atomically, because even if the caller is using
  // a lock, a separate process (that is not aware of this lock) may
  // concurrently modify the value.
  GetOrCreateSampleCountStorage(value)->fetch_add(count,
                                                  std::memory_order_relaxed);
  IncreaseSumAndCount(int64_t{count} * value, count);
}

Count32 PersistentSampleMap::GetCount(Sample32 value) const {
  const std::atomic<Count32>* const count_pointer = GetSampleCountStorage(value);
  return count_pointer ? count_pointer->load(std::memory_order_relaxed) : 0;
}

Count32 PersistentSampleMap::TotalCount() const {
  // Make sure all samples have been loaded before trying to iterate over the
  // map.
  ImportSamples();
  Count32 count = 0;
  for (const auto& entry : sample_counts_) {
    count += entry.second->load(std::memory_order_relaxed);
  }
  return count;
}

std::unique_ptr<SampleCountIterator> PersistentSampleMap::Iterator() const {
  // Make sure all samples have been loaded before trying to iterate over the
  // map.
  ImportSamples();
  return std::make_unique<SampleMapIterator<SampleToCountMap, false>>(
      sample_counts_);
}

std::unique_ptr<SampleCountIterator> PersistentSampleMap::ExtractingIterator() {
  // Make sure all samples have been loaded before trying to iterate over the
  // map.
  ImportSamples();
  return std::make_unique<SampleMapIterator<SampleToCountMap, true>>(
      sample_counts_);
}

bool PersistentSampleMap::IsDefinitelyEmpty() const {
  // Not implemented.
  NOTREACHED();
}

// static
PersistentMemoryAllocator::Reference
PersistentSampleMap::GetNextPersistentRecord(
    PersistentMemoryAllocator::Iterator& iterator,
    uint64_t* sample_map_id,
    Sample32* value) {
  const SampleRecord* record = iterator.GetNextOfObject<SampleRecord>();
  if (!record) {
    return 0;
  }

  *sample_map_id = record->id;
  *value = record->value;
  return iterator.GetAsReference(record);
}

// static
PersistentMemoryAllocator::Reference
PersistentSampleMap::CreatePersistentRecord(
    PersistentMemoryAllocator* allocator,
    uint64_t sample_map_id,
    Sample32 value) {
  SampleRecord* record = allocator->New<SampleRecord>();
  if (record) {
    record->id = sample_map_id;
    record->value = value;
    record->count = 0;
    PersistentMemoryAllocator::Reference ref =
        allocator->GetAsReference(record);
    allocator->MakeIterable(ref);
    return ref;
  }

  if (!allocator->IsFull()) {
    const bool corrupt = allocator->IsCorrupt();
#if !BUILDFLAG(IS_NACL)
    // TODO(crbug.com/40064026): Remove.
    SCOPED_CRASH_KEY_BOOL("PersistentSampleMap", "corrupted", corrupt);
#endif  // !BUILDFLAG(IS_NACL)
    DUMP_WILL_BE_NOTREACHED() << "corrupt=" << corrupt;
  }
  return 0;
}

bool PersistentSampleMap::AddSubtractImpl(SampleCountIterator* iter,
                                          Operator op) {
  Sample32 min;
  int64_t max;
  Count32 count;
  for (; !iter->Done(); iter->Next()) {
    iter->Get(&min, &max, &count);
    if (count == 0) {
      continue;
    }
    if (int64_t{min} + 1 != max) {
      return false;  // SparseHistogram only supports bucket with size 1.
    }

    // We have to do the following atomically, because even if the caller is
    // using a lock, a separate process (that is not aware of this lock) may
    // concurrently modify the value.
    GetOrCreateSampleCountStorage(min)->fetch_add(
        (op == HistogramSamples::ADD) ? count : -count,
        std::memory_order_seq_cst);
  }
  return true;
}

std::atomic<Count32>* PersistentSampleMap::GetSampleCountStorage(
    Sample32 value) const {
  // If |value| is already in the map, just return that.
  const auto it = sample_counts_.find(value);
  return (it == sample_counts_.end()) ? ImportSamples(value) : it->second.get();
}

std::atomic<Count32>* PersistentSampleMap::GetOrCreateSampleCountStorage(
    Sample32 value) {
  // Get any existing count storage.
  std::atomic<Count32>* count_pointer = GetSampleCountStorage(value);
  if (count_pointer) {
    return count_pointer;
  }

  // Create a new record in persistent memory for the value. |records_| will
  // have been initialized by the GetSampleCountStorage() call above.
  CHECK(records_);
  PersistentMemoryAllocator::Reference ref = records_->CreateNew(value);
  if (!ref) {
    // If a new record could not be created then the underlying allocator is
    // full or corrupt. Instead, allocate the counter from the heap. This
    // sample will not be persistent, will not be shared, and will leak...
    // but it's better than crashing.
    count_pointer = new std::atomic<Count32>(0);
    sample_counts_[value] = count_pointer;
    return count_pointer;
  }

  // A race condition between two independent processes (i.e. two independent
  // histogram objects sharing the same sample data) could cause two of the
  // above records to be created. The allocator, however, forces a strict
  // ordering on iterable objects so use the import method to actually add the
  // just-created record. This ensures that all PersistentSampleMap objects
  // will always use the same record, whichever was first made iterable.
  // Thread-safety within a process where multiple threads use the same
  // histogram object is delegated to the controlling histogram object which,
  // for sparse histograms, is a lock object.
  count_pointer = ImportSamples(value);
  DCHECK(count_pointer);
  return count_pointer;
}

PersistentSampleMapRecords* PersistentSampleMap::GetRecords() const {
  // The |records_| pointer is lazily fetched from the |allocator_| only on
  // first use. Sometimes duplicate histograms are created by race conditions
  // and if both were to grab the records object, there would be a conflict.
  // Use of a histogram, and thus a call to this method, won't occur until
  // after the histogram has been de-dup'd.
  if (!records_) {
    records_ = allocator_->CreateSampleMapRecords(id());
  }
  return records_.get();
}

std::atomic<Count32>* PersistentSampleMap::ImportSamples(
    std::optional<Sample32> until_value) const {
  std::vector<PersistentMemoryAllocator::Reference> refs;
  PersistentSampleMapRecords* records = GetRecords();
  while (!(refs = records->GetNextRecords(until_value)).empty()) {
    // GetNextRecords() returns a list of new unseen records belonging to this
    // map. Iterate through them all and store them internally. Note that if
    // |until_value| was found, it will be the last element in |refs|.
    for (auto ref : refs) {
      SampleRecord* const record = records->GetAsObject<SampleRecord>(ref);
      if (!record) {
        continue;
      }

      DCHECK_EQ(id(), record->id);

      // Check if the record's value is already known.
      const auto ret = sample_counts_.insert({record->value, &record->count});
      if (!ret.second) {
        // Yes: Ignore it; it's a duplicate caused by a race condition -- see
        // code & comment in GetOrCreateSampleCountStorage() for details.
        // Check that nothing ever operated on the duplicate record.
        DCHECK_EQ(0, record->count);
      }

      // Check if it's the value being searched for and, if so, stop here.
      // Because race conditions can cause multiple records for a single value,
      // be sure to return the first one found.
      if (until_value.has_value() && record->value == until_value.value()) {
        // Ensure that this was the last value in |refs|.
        CHECK_EQ(refs.back(), ref);

        return &record->count;
      }
    }
  }

  return nullptr;
}

}  // namespace base

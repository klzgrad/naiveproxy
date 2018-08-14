// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_THREADING_SEQUENCE_LOCAL_STORAGE_SLOT_H_
#define BASE_THREADING_SEQUENCE_LOCAL_STORAGE_SLOT_H_

#include <memory>
#include <utility>

#include "base/base_export.h"
#include "base/threading/sequence_local_storage_map.h"

namespace base {

namespace internal {
BASE_EXPORT int GetNextSequenceLocalStorageSlotNumber();
}

// SequenceLocalStorageSlot allows arbitrary values to be stored and retrieved
// from a sequence. Values are deleted when the sequence is deleted.
//
// Example usage:
//
// namespace {
// base::LazyInstance<SequenceLocalStorageSlot<int>> sls_value;
// }
//
// void Read() {
//   int value = sls_value.Get().Get();
//   ...
// }
//
// void Write() {
//   sls_value.Get().Set(42);
// }
//
// void PostTasks() {
//   // Since Read() runs on the same sequence as Write(), it
//   // will read the value "42". A Read() running on a different
//   // sequence would not see that value.
//   scoped_refptr<base::SequencedTaskRunner> task_runner = ...;
//   task_runner->PostTask(FROM_HERE, base::BindOnce(&Write));
//   task_runner->PostTask(FROM_HERE, base::BindOnce(&Read));
// }
//
// SequenceLocalStorageSlot must be used within the scope of a
// ScopedSetSequenceLocalStorageMapForCurrentThread object.
// Note: this is true on all TaskScheduler workers and on threads bound to a
// MessageLoop.
template <typename T, typename Deleter = std::default_delete<T>>
class SequenceLocalStorageSlot {
 public:
  SequenceLocalStorageSlot()
      : slot_id_(internal::GetNextSequenceLocalStorageSlotNumber()) {}
  ~SequenceLocalStorageSlot() = default;

  // Get the sequence-local value stored in this slot. Returns a
  // default-constructed value if no value was previously set.
  T& Get() {
    void* value =
        internal::SequenceLocalStorageMap::GetForCurrentThread().Get(slot_id_);

    // Sets and returns a default-constructed value if no value was previously
    // set.
    if (!value) {
      Set(T());
      return Get();
    }
    return *(static_cast<T*>(value));
  }

  // Set this slot's sequence-local value to |value|.
  // Note that if T is expensive to copy, it may be more appropriate to instead
  // store a std::unique_ptr<T>. This is enforced by the
  // DISALLOW_COPY_AND_ASSIGN style rather than directly by this class however.
  void Set(T value) {
    // Allocates the |value| with new rather than std::make_unique.
    // Since SequenceLocalStorageMap needs to store values of various types
    // within the same map, the type of value_destructor_pair.value is void*
    // (std::unique_ptr<void> is invalid). Memory is freed by calling
    // |value_destructor_pair.destructor| in the destructor of
    // ValueDestructorPair which is invoked when the value is overwritten by
    // another call to SequenceLocalStorageMap::Set or when the
    // SequenceLocalStorageMap is deleted.
    T* value_ptr = new T(std::move(value));

    internal::SequenceLocalStorageMap::ValueDestructorPair::DestructorFunc*
        destructor = [](void* ptr) { Deleter()(static_cast<T*>(ptr)); };

    internal::SequenceLocalStorageMap::ValueDestructorPair
        value_destructor_pair(value_ptr, destructor);

    internal::SequenceLocalStorageMap::GetForCurrentThread().Set(
        slot_id_, std::move(value_destructor_pair));
  }

 private:
  // |slot_id_| is used as a key in SequenceLocalStorageMap
  const int slot_id_;
  DISALLOW_COPY_AND_ASSIGN(SequenceLocalStorageSlot);
};

}  // namespace base
#endif  // BASE_THREADING_SEQUENCE_LOCAL_STORAGE_SLOT_H_

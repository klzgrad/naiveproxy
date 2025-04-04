// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MEMORY_DISCARDABLE_MEMORY_H_
#define BASE_MEMORY_DISCARDABLE_MEMORY_H_

#include "base/base_export.h"
#include "build/build_config.h"

namespace base {

namespace trace_event {
class MemoryAllocatorDump;
class ProcessMemoryDump;
}  // namespace trace_event

// Discardable memory is used to cache large objects without worrying about
// blowing out memory, both on mobile devices where there is no swap, and
// desktop devices where unused free memory should be used to help the user
// experience. This is preferable to releasing memory in response to an OOM
// signal because it is simpler and provides system-wide management of
// purgable memory, though it has less flexibility as to which objects get
// discarded.
//
// Discardable memory has two states: locked and unlocked. While the memory is
// locked, it will not be discarded. Unlocking the memory allows the
// discardable memory system and the OS to reclaim it if needed. Locks do not
// nest.
//
// Notes:
//   - The paging behavior of memory while it is locked is not specified. While
//     mobile platforms will not swap it out, it may qualify for swapping
//     on desktop platforms. It is not expected that this will matter, as the
//     preferred pattern of usage for DiscardableMemory is to lock down the
//     memory, use it as quickly as possible, and then unlock it.
//   - Because of memory alignment, the amount of memory allocated can be
//     larger than the requested memory size. It is not very efficient for
//     small allocations.
//   - A discardable memory instance is not thread safe. It is the
//     responsibility of users of discardable memory to ensure there are no
//     races.
//
class BASE_EXPORT DiscardableMemory {
 public:
  DiscardableMemory();
  virtual ~DiscardableMemory();

  // Locks the memory so that it will not be purged by the system. Returns
  // true on success. If the return value is false then this object should be
  // destroyed and a new one should be created.
  [[nodiscard]] virtual bool Lock() = 0;

  // Unlocks the memory so that it can be purged by the system. Must be called
  // after every successful lock call.
  virtual void Unlock() = 0;

  // Returns the memory address held by this object. The object must be locked
  // before calling this.
  virtual void* data() const = 0;

  // Forces the memory to be purged, such that any following Lock() will fail.
  // The object must be unlocked before calling this.
  virtual void DiscardForTesting() = 0;

  // Handy method to simplify calling data() with a reinterpret_cast.
  template <typename T>
  T* data_as() const {
    return reinterpret_cast<T*>(data());
  }

  // Used for dumping the statistics of discardable memory allocated in tracing.
  // Returns a new MemoryAllocatorDump in the |pmd| with the size of the
  // discardable memory. The MemoryAllocatorDump created is owned by |pmd|. See
  // ProcessMemoryDump::CreateAllocatorDump.
  virtual trace_event::MemoryAllocatorDump* CreateMemoryAllocatorDump(
      const char* name,
      trace_event::ProcessMemoryDump* pmd) const = 0;
};

enum class DiscardableMemoryBacking { kSharedMemory, kMadvFree };
BASE_EXPORT DiscardableMemoryBacking GetDiscardableMemoryBacking();

}  // namespace base

#endif  // BASE_MEMORY_DISCARDABLE_MEMORY_H_

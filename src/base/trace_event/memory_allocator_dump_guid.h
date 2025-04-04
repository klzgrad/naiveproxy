// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_MEMORY_ALLOCATOR_DUMP_GUID_H_
#define BASE_TRACE_EVENT_MEMORY_ALLOCATOR_DUMP_GUID_H_

#include <stdint.h>

#include <string>

#include "base/base_export.h"

namespace base {
namespace trace_event {

class BASE_EXPORT MemoryAllocatorDumpGuid {
 public:
  MemoryAllocatorDumpGuid();
  explicit MemoryAllocatorDumpGuid(uint64_t guid);

  // Utility ctor to hash a GUID if the caller prefers a string. The caller
  // still has to ensure that |guid_str| is unique, per snapshot, within the
  // global scope of all the traced processes.
  explicit MemoryAllocatorDumpGuid(const std::string& guid_str);

  uint64_t ToUint64() const { return guid_; }

  // Returns a (hex-encoded) string representation of the guid.
  std::string ToString() const;

  bool empty() const { return guid_ == 0u; }

  bool operator==(const MemoryAllocatorDumpGuid& other) const {
    return guid_ == other.guid_;
  }

  bool operator!=(const MemoryAllocatorDumpGuid& other) const {
    return !(*this == other);
  }

  bool operator<(const MemoryAllocatorDumpGuid& other) const {
    return guid_ < other.guid_;
  }

 private:
  uint64_t guid_;

  // Deliberately copy-able.
};

}  // namespace trace_event
}  // namespace base

#endif  // BASE_TRACE_EVENT_MEMORY_ALLOCATOR_DUMP_GUID_H_

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

#include "perfetto/trace_processor/trace_blob.h"

#include <stdlib.h>
#include <string.h>

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
#include <sys/mman.h>
#endif

#include <algorithm>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/scoped_mmap.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/ref_counted.h"

namespace perfetto {
namespace trace_processor {

// static
TraceBlob TraceBlob::Allocate(size_t size) {
  TraceBlob blob(Ownership::kHeapBuf, new uint8_t[size], size);
  PERFETTO_CHECK(blob.data_);
  return blob;
}

// static
TraceBlob TraceBlob::CopyFrom(const void* src, size_t size) {
  TraceBlob blob = Allocate(size);
  const uint8_t* src_u8 = static_cast<const uint8_t*>(src);
  std::copy(src_u8, src_u8 + size, blob.data_);
  return blob;
}

// static
TraceBlob TraceBlob::TakeOwnership(std::unique_ptr<uint8_t[]> buf,
                                   size_t size) {
  PERFETTO_CHECK(buf);
  return TraceBlob(Ownership::kHeapBuf, buf.release(), size);
}

// static
TraceBlob TraceBlob::FromMmap(base::ScopedMmap mapped) {
  PERFETTO_CHECK(mapped.IsValid());
  TraceBlob blob(Ownership::kNullOrMmapped,
                 static_cast<uint8_t*>(mapped.data()), mapped.length());
  blob.mapping_ = std::make_unique<base::ScopedMmap>(std::move(mapped));
  return blob;
}

// static
TraceBlob TraceBlob::FromMmap(void* data, size_t size) {
#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) ||   \
    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \
    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)
  PERFETTO_CHECK(data);
  TraceBlob blob(Ownership::kNullOrMmapped, static_cast<uint8_t*>(data), size);
  blob.mapping_ = std::make_unique<base::ScopedMmap>(
      base::ScopedMmap::InheritMmappedRange(data, size));
  return blob;
#else
  base::ignore_result(data);
  base::ignore_result(size);
  PERFETTO_FATAL("mmap not supported");
#endif
}

TraceBlob::TraceBlob(Ownership ownership, uint8_t* data, size_t size)
    : ownership_(ownership), data_(data), size_(size) {}

TraceBlob::~TraceBlob() {
  switch (ownership_) {
    case Ownership::kHeapBuf:
      delete[] data_;
      break;

    case Ownership::kNullOrMmapped:
      if (mapping_) {
        PERFETTO_CHECK(mapping_->reset());
      }
      break;
  }
  data_ = nullptr;
  size_ = 0;
}

TraceBlob::TraceBlob(TraceBlob&& other) noexcept
    : RefCounted(std::move(other)) {
  static_assert(
      sizeof(*this) == base::AlignUp<sizeof(void*)>(
                           sizeof(data_) + sizeof(size_) + sizeof(ownership_) +
                           sizeof(mapping_) + sizeof(RefCounted)),
      "TraceBlob move constructor needs updating");
  data_ = other.data_;
  size_ = other.size_;
  ownership_ = other.ownership_;
  mapping_ = std::move(other.mapping_);
  other.data_ = nullptr;
  other.size_ = 0;
  other.ownership_ = Ownership::kNullOrMmapped;
  other.mapping_ = nullptr;
}

TraceBlob& TraceBlob::operator=(TraceBlob&& other) noexcept {
  if (this == &other)
    return *this;
  this->~TraceBlob();
  new (this) TraceBlob(std::move(other));
  return *this;
}

}  // namespace trace_processor
}  // namespace perfetto

/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/trace_processor/tp_metatrace.h"

namespace perfetto {
namespace trace_processor {
namespace metatrace {

namespace {

using ProtoEnum = protos::pbzero::MetatraceCategories;
ProtoEnum MetatraceCategoriesToProtoEnum(MetatraceCategories categories) {
  // Note: these are intentionally chained ifs and not else-ifs as it's possible
  // for multiple of these if statements to be true.
  ProtoEnum result = ProtoEnum::NONE;
  if (categories & MetatraceCategories::QUERY_TIMELINE)
    result = static_cast<ProtoEnum>(result | ProtoEnum::QUERY_TIMELINE);
  if (categories & MetatraceCategories::FUNCTION_CALL)
    result = static_cast<ProtoEnum>(result | ProtoEnum::FUNCTION_CALL);
  if (categories & MetatraceCategories::QUERY_DETAILED)
    result = static_cast<ProtoEnum>(result | ProtoEnum::QUERY_DETAILED);
  if (categories & MetatraceCategories::DB)
    result = static_cast<ProtoEnum>(result | ProtoEnum::DB);
  if (categories & MetatraceCategories::API_TIMELINE)
    result = static_cast<ProtoEnum>(result | ProtoEnum::API_TIMELINE);
  return result;
}

}  // namespace

thread_local Category g_enabled_categories = Category::NONE;

void Enable(MetatraceConfig config) {
  g_enabled_categories = MetatraceCategoriesToProtoEnum(config.categories);
  if (config.override_buffer_size) {
    RingBuffer::GetInstance().Resize(config.override_buffer_size);
  }
}

void DisableAndReadBuffer(std::function<void(Record*)> fn) {
  g_enabled_categories = Category::NONE;
  if (!fn)
    return;
  RingBuffer::GetInstance().ReadAll(fn);
}

RingBuffer::RingBuffer() : data_(kDefaultCapacity) {
  static_assert((kDefaultCapacity & (kDefaultCapacity - 1)) == 0,
                "Capacity should be a power of 2");
}

void RingBuffer::Resize(size_t requested_capacity) {
  size_t actual_capacity = 1;
  while (actual_capacity < requested_capacity)
    actual_capacity <<= 1;
  data_.resize(actual_capacity);
  start_idx_ = 0;
  write_idx_ = 0;
}

void RingBuffer::ReadAll(std::function<void(Record*)> fn) {
  // Mark as reading so we don't get reentrancy in obtaining new
  // trace events.
  is_reading_ = true;

  uint64_t start = (write_idx_ - start_idx_) < data_.size()
                       ? start_idx_
                       : write_idx_ - data_.size();
  uint64_t end = write_idx_;

  // Increment the write index by kCapacity + 1. This ensures that if
  // ScopedEntry is destroyed in |fn| below, we won't get overwrites
  // while reading the buffer.
  // This works because of the logic in ~ScopedEntry and
  // RingBuffer::HasOverwritten which ensures that we don't overwrite entries
  // more than kCapacity elements in the past.
  write_idx_ += data_.size() + 1;

  for (uint64_t i = start; i < end; ++i) {
    Record* record = At(i);

    // If the slice was unfinished for some reason, don't emit it.
    if (record->duration_ns != 0) {
      fn(record);
    }
  }

  // Ensure that the start pointer is updated to the write pointer.
  start_idx_ = write_idx_;

  // Remove the reading marker.
  is_reading_ = false;
}

}  // namespace metatrace
}  // namespace trace_processor
}  // namespace perfetto
